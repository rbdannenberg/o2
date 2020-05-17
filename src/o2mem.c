/* o2mem.c -- real-time allocation
 *
 * Roger B. Dannenberg
 * April 2020
 */

/* Allocate large chunks of memory using malloc.  From the chunks,
   allocate memory as needed on long-word boundaries.  Allocated 
   memory is freed by linking (lock-free) onto a freelist avoiding
   the lock in free().
 
   There are many ways to get memory: (1) small blocks come from
   freelists holding blocks of size 8, 16, 24, 32, .... (2) medium
   blocks come from freelists that hold blocks of sizes that are
   powers of 2. (3) large blocks bigger than the biggest medium
   size block are allocated using malloc(), but only if malloc_ok
   flag is true. (4) take a block from chunk (if chunk is too small
   and malloc_ok, then allocate a new chunk). (5) If a medium block
   is needed and the chunk is too small, as a special case, a new 
   chunk just for that block is allocated and used and what's left
   of the old chunk is retained for. Until the chunk doesn't even
   have enough remaining so service a small block allocation. Then
   it is replaced with a new chunk of size O2MEM_CHUNK_SIZE (if 
   malloc_ok). A client can provide a fixed-sized chunk and does
   not need to implement or allow use of malloc().
 
   Since malloc()'d blocks do not have a portable way to determine
   the block size, we always allocate extra space for the block
   size and store it just before the returned block address. If
   debugging is on, we also allocate sentinals before and after the
   block, and optionally extra space at the end of the block but
   before the sentinal. This applies to memory blocks allocated from
   the chunk as well.
 */

#include "o2internal.h"
#include <libkern/OSAtomic.h>
#include <libkern/OSAtomicQueue.h>
#include "o2mem.h"

#define O2MEM_ALIGN_LOG2 3
#define LOG2_MAX_LINEAR_BYTES 9 // up to (512 - 8) byte chunks
#define MAX_LINEAR_BYTES (1 << LOG2_MAX_LINEAR_BYTES)
#define LOG2_MAX_EXPONENTIAL_BYTES 25 // up to 16MB = 2^24
#define MAX_EXPONENTIAL_BYTES (1 << LOG2_MAX_EXPONENTIAL_BYTES)

#ifdef O2MEM_DEBUG
// allocate unused memory after every allocated chunck
// normally this is zero, but if you suspect code is writing outside
// of the allocated memory, this will at least change the write pattern.
// If writes to object A are damaging object B, adding this "isolation"
// region might eliminate damage to object B but object A will still
// overwrite its own sentinal, detection of which will indicate that
// writing to object A is the real source of the problem.
#define O2MEM_ISOLATION 128
#endif

// should be *MUCH BIGGER* than MAX_LINEAR_BYTES (see o2_malloc)
#define O2MEM_CHUNK_SIZE (1 << 13) // 13 is much bigger than 9
#define MAX(x, y) ((x) > (y) ? (x) : (y))

// Given a pointer memory block, return the length
#define BLOCK_SIZE(ptr) (((size_t *) (ptr))[-1])

static int need_o2mem_init = TRUE;
static char *chunk; // where to get bytes
static int64_t chunk_remaining; // how many more bytes to be gotten
static int malloc_ok = TRUE;
static int total_allocated = 0;
static char *allocated_chunk_list = NULL;

static void *o2_malloc(size_t size);
static void o2_free(void *);

void *((*o2_malloc_ptr)(size_t size)) = &o2_malloc;
void ((*o2_free_ptr)(void *)) = &o2_free;

// array of freelists for sizes from 0 to MAX_LINEAR_BYTES-8 by 8
// this takes 512 / 8 * 16 = 1024 bytes
static OSQueueHead linear_free[MAX_LINEAR_BYTES / O2MEM_ALIGN];

// array of freelists with log2(size) from 0 to LOG2_MAX_EXPONENTIAL_BYTES
// this takes (25 - 9) * 16 = 256 bytes
static OSQueueHead exponential_free[LOG2_MAX_EXPONENTIAL_BYTES -
                                    LOG2_MAX_LINEAR_BYTES];

#ifndef O2_NO_DEBUG
void *o2_dbg_malloc(size_t size, const char *file, int line)
{
    O2_DBm(printf("%s O2_MALLOC %lld in %s:%d", o2_debug_prefix, 
                  (long long) size, file, line));
    fflush(stdout);
    void *obj = (*o2_malloc_ptr)(size);
    O2_DBm(printf(" -> %p\n", obj));
    assert(obj);
    return obj;
}


void o2_dbg_free(const void *obj, const char *file, int line)
{
    O2_DBm(printf("%s O2_FREE in %s:%d <- %p\n", 
                  o2_debug_prefix, file, line, obj));
    // bug in C. free should take a const void * but it doesn't
    (*o2_free_ptr)((void *) obj);
}
#endif

/**
 * Similar to calloc, but this uses the malloc and free functions
 * provided to O2 through a call to o2_memory().
 * @param[in] n     The number of objects to allocate.
 * @param[in] size  The size of each object in bytes.
 *
 * @return The address of newly allocated and zeroed memory, or NULL.
 */
#ifdef NO_O2_DEBUG
void *o2_calloc(size_t n, size_t s)
{
    void *loc = O2_MALLOC(n * s);
    memset(loc, 0, n * s);
    return loc;
}
#else
void *o2_dbg_calloc(size_t n, size_t s, const char *file, int line)
{
    O2_DBm(printf("%s O2_CALLOC %lu of %lu in %s:%d", o2_debug_prefix,
                  n, s, file, line));
    fflush(stdout);
    void *obj = (*o2_malloc_ptr)(s);
    O2_DBm(printf(" -> %p\n", obj));
    assert(obj);
    memset(obj, 0, n * s);
    return obj;
}
#endif


int o2_memory(void *((*malloc)(size_t size)), void ((*free)(void *)))
{
    need_o2mem_init = FALSE;
    o2_malloc_ptr = malloc;
    o2_free_ptr = free;
    return O2_SUCCESS;
}


void o2_mem_init(char *first_chunk, int64_t size, int mallocp)
{
    if (!need_o2mem_init) {
        return;
    }
    memset((void *) linear_free, 0, sizeof(linear_free));
    memset((void *) exponential_free, 0, sizeof(exponential_free));
    need_o2mem_init = FALSE;
    chunk = first_chunk;
    chunk_remaining = size;
    malloc_ok = mallocp;
}


// return (log2 of actual block size) rounded up
//
static int power_of_2_block_size(size_t size)
{
    int log = LOG2_MAX_LINEAR_BYTES;
    while (size > (1 << log)) log++;
    return log;
}


// returns a pointer to the sublist for a given size.
static OSQueueHead *head_ptr_for_size(size_t *size)
{
    *size = O2MEM_ALIGNUP(*size);
    size_t index = *size >> O2MEM_ALIGN_LOG2;
    if (index < (MAX_LINEAR_BYTES / O2MEM_ALIGN)) {
        return &(linear_free[index]);
    }
    index = power_of_2_block_size(*size);
    if (index < LOG2_MAX_EXPONENTIAL_BYTES) {
        *size = 1 << index;
        // assert: index - LOG2_MAX_LINEAR_BYTES is in bounds
        // proof: (1) show index - LOG2_MAX_LINEAR_BYTES >= 0
        //    equivalent to index >= LOG2_MAX_LINEAR_BYTES
        //    this follows from power_of_2_block_size()
        //    (2) show (index - LOG2_MAX_LINEAR_BYTES) <
        //             LOG2_MAX_EXPONENTIAL_BYTES - LOG2_MAX_LINEAR_BYTE
        //    equivalent to index < LOG2_MAX_EXPONENTIAL_BYTES
        //    this follows from the condition
        return &(exponential_free[index - LOG2_MAX_LINEAR_BYTES]);
    }
    return NULL;
}


void *o2_malloc(size_t size)
{
    // realsize is within 8 bytes of requested size. When debugging,
    // write a magic token after realsize to allow future checks for 
    // stray writes into heap memory outside of allocated blocks
    size_t realsize = O2MEM_ALIGNUP(size) + sizeof(size_t); // room for length
#   ifdef O2MEM_DEBUG
        // room to store overwrite area
        realsize += 2 * sizeof(int64_t);
        realsize += O2MEM_ISOLATION;
#   endif
    char *result; // allocate by malloc, find on freelist, or carve off chunk
    // find what really gets allocated. Large blocks especially 
    // are rounded up to a power of two.
    OSQueueHead *p = head_ptr_for_size(&realsize);
    if (!p) {
        if (malloc_ok) {
            result = malloc(realsize);
            goto gotit;
        }
        fprintf(stderr, "o2_malloc of %lu bytes failed\n", realsize);
        return NULL;
    }

    result = (char *) OSAtomicDequeue(p, 0);
    // invariant: result points to block of size realsize at an offset of
    // 8 (or 16 if O2MEM_DEBUG) bytes.
    if (result) {
        // make sure we get the expected freed block
        assert(BLOCK_SIZE(result) == realsize);
#ifdef O2MEM_DEBUG
        // result is 2 * 8 bytes beyond the beginning of the allocated block
        assert(((size_t *) result)[-2] == 0xDea110c8dDebac1e); // cute words
        assert(((int64_t *)((char *) result + realsize))[-3] == 0x5ea1ed5caff01d);
        result -= sizeof(size_t);
#endif
        result -= sizeof(int64_t);
        goto gotit;
    }
    // 
    if (chunk_remaining < realsize) {
        if (!malloc_ok) return NULL; // no more memory
        // I hate to throw away the remaining chunk, so let's use malloc
        // for anything MAX_LINEAR_BYTES or bigger. Then (with current
        // settings), we'll never lose more than 504 out of 8K, so
        // utilization is 15/16 or better.
        if (realsize >= MAX_LINEAR_BYTES) {
            // make a new chunk just for realsize
            char *chunk2 = (char *) malloc(realsize + sizeof(char *));
            // add chunk2 to list
            *((char **) chunk2) = allocated_chunk_list;
            allocated_chunk_list = chunk2;
            result = chunk2 + sizeof(char *); // skip over chunk list pointer
            goto gotit;
        } 
        // note that we throw away remaining chunk if there isn't enough now
        // since we need less than MAX_LINEAR_BYTES, we know O2MEM_CHUNK_SIZE
        // is enough:
        if (!(chunk = (char *) malloc(O2MEM_CHUNK_SIZE))) {
            chunk_remaining = 0;
            fprintf(stderr, "Warning: no more memory in o2_malloc, return NULL");
            return NULL; // can't allocate a chunk
        }
        // add allocated chunk to the list
        *((char **) chunk) = allocated_chunk_list;
        allocated_chunk_list = chunk;
        chunk += sizeof(char *); // skip over chunk list pointer
        chunk_remaining = O2MEM_CHUNK_SIZE - sizeof(char *);
    }
    result = chunk;
    chunk += realsize;
    chunk_remaining -= realsize;
  gotit: // invariant: result points to block of size realsize
    result += sizeof(size_t); // offset to allow size at [-1]
#ifdef O2MEM_DEBUG
    result += sizeof(int64_t); // shift to allow sentinal at [-2]
    ((size_t *) result)[-2] = ~realsize; // sentinal is complement of size
#endif
    BLOCK_SIZE(result) = realsize; // store requested size in first 8 bytes
    total_allocated += realsize;
    
#ifdef O2MEM_DEBUG
    // realsize includes start sentinal, size field and end sentinal
    realsize = realsize - sizeof(size_t) * 3;
    *(int64_t *)(result + realsize) = 0xBADCAFE8DEADBEEF;
#endif
    o2_mem_check(result);
    return result;
}


#ifdef O2MEM_DEBUG

void o2_mem_check(void *ptr)
{
    if (((size_t *) ptr)[-2] != ~(((size_t *) ptr)[-1])) {
        fprintf(stderr, "block size or sentinal mismatch: %p->~%zu,%zu\n",
                ptr, ~((size_t *) ptr)[-2], ((size_t *) ptr)[-1]);
        assert(false);
    }
    size_t realsize = BLOCK_SIZE(ptr);
    if (((int64_t *)((char *) ptr + realsize))[-3] != 0xBADCAFE8DEADBEEF) {
        fprintf(stderr, "block was overwritten beyond realsize %zu: %p\n",
                realsize, ptr);
        assert(false);
    }
}
#else
#define o2_mem_check(x)
#endif


void o2_free(void *ptr)
{
    if (!ptr) {
        fprintf(stderr, "o2_free NULL ignored\n");
        return;
    }
    o2_mem_check(ptr); // if O2MEM_DEBUG undefined, this is a noop
    size_t realsize = BLOCK_SIZE(ptr);
    if (realsize == 0) {
        fprintf(stderr, "o2_free block has size 0\n");
        return;
    }
#ifdef O2MEM_DEBUG
    // change sentinals to indicate a free block
    ((size_t *) ptr)[-2] = 0xDea110c8dDebac1e; // cute words
    ((int64_t *)((char *) ptr + realsize))[-3] = 0x5ea1ed5caff01d;
#endif
    // head_ptr_for_size can round up realsize
    OSQueueHead *head_ptr = head_ptr_for_size(&realsize);
    if (!head_ptr) {
        if (malloc_ok) {
#ifdef O2MEM_DEBUG
            ptr -= sizeof(int64_t); // remove offset for sentinal
#endif
            ptr -= sizeof(int64_t); // remove offset for size
            free(ptr); // now we're freeing the originally allocated address
            return;
        }
        fprintf(stderr, "o2_free of %lu bytes failed\n", realsize);
        return;
    }
    total_allocated -= realsize;
    OSAtomicEnqueue(head_ptr, (char *) ptr, 0);
}

void o2_mem_finish()
{
    while (allocated_chunk_list) {
        chunk = allocated_chunk_list;
        allocated_chunk_list = *((char **) allocated_chunk_list); // next
        free(chunk);
    }
    need_o2mem_init = TRUE;
    o2_mem_init(NULL, 0, FALSE); // remove free lists
    need_o2mem_init = TRUE;
}
