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

   To support concurrent allocation, each thread has it's own chunk in
   o2_ctx. The chunks are linked onto a master list atomically and 
   freed by o2_mem_finish().
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

long o2_mem_watch_seqno = 0; // set o2_mem_watch after this many mallocs
long o2_mem_seqno = 0; // counts allocations
void *o2_mem_watch = 0; // address to watch

#if O2MEM_DEBUG
// When O2MEM_DEBUG is non-zero, allocate space as follows:
//
// preamble_struct:
//   ~realsize or Dea110c8dDebac1e -- start sentinal (8 bytes),
//                       ~realsize is for allocated, other is for freed
//   realsize         -- size of everything allocated (8 bytes)
// user data:
//   block beginning  -- this address is the user's address of the block
//   block ending     -- last 8 bytes in the user area of the block
// postlude_struct:
//   padding          -- (O2MEM_ISOLATION - 16) bytes filled with zero
//   sequence number  -- when was this block allocated?
//   BADCAFE8DEADBEEF or 5ea1ed5caff01d   -- end sentinal (8 bytes),
//                     first is for allocated, second is for freed
// chunk_end_sentinal
//   
// zero padding after every allocated chunck
// normally this is zero, but if you suspect code is writing outside
// of the allocated memory, this will at least change the write pattern.
// This is the number of 8-byte words to pad:
#define O2MEM_ISOLATION 16
#endif

// Aids to reading/writing/checking memory structure:

typedef struct preamble_struct {
#if O2MEM_DEBUG
    int64_t start_sentinal;
#endif
    size_t size;    // usable bytes in payload
    char payload[8]; // the application memory
} preamble_t, *preamble_ptr;

typedef struct postlude_struct {
#if O2MEM_DEBUG
    int64_t padding[O2MEM_ISOLATION];
    int64_t seqno;
    int64_t end_sentinal;
#endif
} postlude_t, *postlude_ptr;
    
typedef struct chunk_struct {
    struct chunk_struct *next;
    preamble_t first;
} chunk_t, *chunk_ptr;


// find postlude from preamble
#define PREAMBLE_TO_POSTLUDE(pre) \
        ((postlude_ptr) ((pre)->payload + (pre)->size))

#define SIZE_TO_REALSIZE(size) \
    ((size) + offsetof(preamble_t, payload) + sizeof(postlude_t))

// should be *MUCH BIGGER* than MAX_LINEAR_BYTES (see o2_malloc)
#define O2MEM_CHUNK_SIZE (1 << 13) // 13 is much bigger than 9
#define MAX(x, y) ((x) > (y) ? (x) : (y))

// Given a pointer memory block, return the length
#define BLOCK_SIZE(ptr) (((int64_t *)(ptr))[-1])

static bool need_o2mem_init = true;
static bool using_o2mem = true;
static bool malloc_ok = true;
static char *initial_chunk = NULL;
static int64_t initial_chunk_size = 0;
static int total_allocated = 0;
static OSQueueHead allocated_chunk_list = OS_ATOMIC_QUEUE_INIT;

static void *o2_malloc(size_t size);
static void o2_free(void *);

void *((*o2_malloc_ptr)(size_t size)) = &o2_malloc;
void ((*o2_free_ptr)(void *)) = &o2_free;

// array of freelists for sizes from 0 to MAX_LINEAR_BYTES-8 by 8
// this takes 512 / 8 * 16 = 1024 bytes
// sizes are usable sizes not counting preamble or postlude space:
static OSQueueHead linear_free[MAX_LINEAR_BYTES / O2MEM_ALIGN];

// array of freelists with log2(size) from 0 to LOG2_MAX_EXPONENTIAL_BYTES
// this takes (25 - 9) * 16 = 256 bytes
static OSQueueHead exponential_free[LOG2_MAX_EXPONENTIAL_BYTES -
                                    LOG2_MAX_LINEAR_BYTES];

#ifndef O2_NO_DEBUG
int o2mem_get_seqno(const void *ptr);

void *o2_dbg_malloc(size_t size, const char *file, int line)
{
    O2_DBm(printf("%s O2_MALLOC %lld bytes in %s:%d", o2_debug_prefix, 
                  (long long) size, file, line));
    O2_DBm(fflush(stdout));
    void *obj = (*o2_malloc_ptr)(size);
    O2_DBm(printf(" -> #%d@%p\n", o2mem_get_seqno(obj), obj));
    assert(obj);
    return obj;
}


void o2_dbg_free(const void *obj, const char *file, int line)
{
    O2_DBm(printf("%s O2_FREE %d bytes in %s:%d : #%d@%p\n",
                  o2_debug_prefix, ((int64_t *) obj)[-1], file, line, o2mem_get_seqno(obj), obj));
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
    void *obj = (*o2_malloc_ptr)(n * s);
    O2_DBm(printf(" -> #%d@%p\n", o2mem_get_seqno(obj), obj));
    assert(obj);
    memset(obj, 0, n * s);
    return obj;
}
#endif


#if O2MEM_DEBUG
// check validity of memory block. Allocated blocks pass test if
//     alloc_ok, freed blocks pass test if free_ok,
// returns true if there is a problem detected
//
bool o2_block_check(void *ptr, int alloc_ok, int free_ok)
{
    bool rslt = false;
    preamble_ptr preamble = (preamble_ptr)
            (ptr - offsetof(preamble_t, payload));
    int64_t size = preamble->size;
    int64_t realsize = SIZE_TO_REALSIZE(size);
    postlude_ptr postlude = PREAMBLE_TO_POSTLUDE(preamble);
    if (preamble->start_sentinal == ~realsize) {
        if (alloc_ok) goto good_sentinal;
        fprintf(stderr, "block at %p is allocated\n", ptr);
    } else if (preamble->start_sentinal == 0xDea110c8dDebac1e) {
        if (free_ok) goto good_sentinal;
        fprintf(stderr, "block at %p has sentinal of freed block\n", ptr);
    } else {
        fprintf(stderr, "block size or sentinal mismatch: %p->~%lld,%lld\n",
                    ptr, ~preamble->start_sentinal, realsize);
        fprintf(stderr, "block #%lld size %lld\n",
                postlude->seqno, realsize);
        assert(false);
    }
    fprintf(stderr, "block #%lld size %lld\n", postlude->seqno, realsize);
    rslt = true;
  good_sentinal:
    for (int i = 0; i < O2MEM_ISOLATION; i++) {
        if (postlude->padding[i] != 0xabababababababab) {
            fprintf(stderr, "block %p padding was overwritten, seqno %lld\n",
                    ptr, postlude->seqno);
            assert(false);
            return rslt;
        }
    }
    if (postlude->end_sentinal == 0x5ea1ed5caff01d) { // freed end sentinal
        if (preamble->start_sentinal != 0xDea110c8dDebac1e) {
            fprintf(stderr, "free block start sentinal but block %p start "
                    "indicates it\nis still allocated: end sentinal %lld "
                    "(0x%llx) size %lld\n", ptr, postlude->end_sentinal,
                    postlude->end_sentinal, realsize);
            assert(false);
            return rslt;
        }
    } else if (postlude->end_sentinal == 0xBADCAFE8DEADBEEF) {
        if (preamble->start_sentinal != ~realsize) { // allocated
            fprintf(stderr, "allocated block start sentinal but block %p "
                    "start indicates it\nis freed: end sentinal %lld "
                    "(0x%llx) size %lld\n", ptr, postlude->end_sentinal,
                    postlude->end_sentinal, realsize);
            assert(false);
            return rslt;
        }
    } else {
        fprintf(stderr, "block %p has invalid end sentinal %lld "
                "(0x%llx) size %lld\n", ptr, postlude->end_sentinal,
                postlude->end_sentinal, realsize);
        assert(false);
        return rslt;
    }
    return rslt;
}


void o2_mem_check(void *ptr)
{
    o2_block_check(ptr, true, false);
#if O2MEM_DEBUG > 1
    // this is much more expensive, so default O2MEM_DEBUG=1 does not call it
    o2_mem_check_all(false);
#endif
}


int o2mem_get_seqno(const void *ptr)
{
    preamble_ptr preamble = (preamble_ptr) (ptr - offsetof(preamble_t, payload));
    postlude_ptr postlude = PREAMBLE_TO_POSTLUDE(preamble);
    return postlude->seqno;
}
#else
#ifndef O2_NO_DEBUG
int o2mem_get_seqno(void *ptr)
{
    return 0;
}
#endif
#endif


#if O2MEM_DEBUG
// o2_mem_check_all - check for valid heap, optional check for leaks
//
bool o2_mem_check_all(int report_leaks)
{
    chunk_ptr chunk = (chunk_ptr) OSAtomicDequeue(&allocated_chunk_list, 0);
    int leak_found = false;
    while (chunk) { // walk the chunk list and see if everything is freed
        // within each chunk, blocks are allocated sequentially
        preamble_ptr preamble = &chunk->first;
        while (preamble->start_sentinal != 0xDEADDEEDCACA0000) {
            leak_found |= o2_block_check(preamble->payload, !report_leaks, true);
            preamble = (preamble_ptr) (PREAMBLE_TO_POSTLUDE(preamble) + 1);
        }
        chunk = (chunk_ptr) OSAtomicDequeue(&allocated_chunk_list, 0);
    }
    return leak_found;
}
#endif

// configure memory before o2_initialize(). first_chunk is never freed -
//    it is owned by the caller and may be freed after o2_finish().
int o2_memory(void *((*malloc)(size_t size)), void ((*free)(void *)),
              char *first_chunk, int64_t size, bool mallocp)
{
    if (!need_o2mem_init || !using_o2mem) { // error to change configuration!
        return O2_FAIL;
    }
    if (malloc && free) {
        need_o2mem_init = false;
        using_o2mem = false;
        o2_malloc_ptr = malloc;
        o2_free_ptr = free;
    } else if (!malloc && !free) {
        initial_chunk = first_chunk;
        initial_chunk_size = size;
        o2_mem_init(first_chunk, size);
        malloc_ok = mallocp;
    }        
    return O2_SUCCESS;
}


void o2_mem_init(char *chunk, int64_t size)
{
    if (!need_o2mem_init || !using_o2mem) {
        return;
    }
    memset((void *) linear_free, 0, sizeof linear_free);
    memset((void *) exponential_free, 0, sizeof exponential_free);
    need_o2mem_init = false;
    o2_ctx->chunk = chunk;
    o2_ctx->chunk_remaining = size;
}


void o2_mem_finish()
{
    printf("o2_mem_finish called\n");
    if (using_o2mem) {
#if O2MEM_DEBUG
        // this is expensive, but so is shutting down, so why not...
        printf("**** o2_mem_finish checking for memory leaks...\n");
        printf("**** o2_mem_finish detected %sleaks.\n",
               o2_mem_check_all(true) ? "" : "NO ");
#endif
        chunk_ptr chunk = (chunk_ptr) OSAtomicDequeue(&allocated_chunk_list, 0);
        while (chunk) {
            free(chunk);
            chunk = (chunk_ptr) OSAtomicDequeue(&allocated_chunk_list, 0);
        }

        need_o2mem_init = true;
        o2_mem_init(NULL, 0); // remove free lists
        need_o2mem_init = true;
    }
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
        return &linear_free[index];
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
        return &exponential_free[index - LOG2_MAX_LINEAR_BYTES];
    }
    return NULL;
}


void *o2_malloc(size_t size)
{
    o2_mem_check_all(false);
    // round up to 8-byte alignment, add room for preamble and postlude,
    // but postlude includes an extra 8-byte sentinal that's not part of
    // the allocated block, so subtract that to get realsize:
    size = O2MEM_ALIGNUP(size);
    char *result; // allocate by malloc, find on freelist, or carve off chunk
    // find what really gets allocated. Large blocks especially 
    // are rounded up to a power of two.
    OSQueueHead *p = head_ptr_for_size(&size);
    // knowing the actual size allocated (or to allocate), we can compute
    // the "real size" including the preamble and postlude and payload
    size_t realsize = SIZE_TO_REALSIZE(size);
    preamble_ptr preamble;
#if O2MEM_DEBUG
    postlude_ptr postlude;
#endif
    if (!p) { // too big, no corresponding freelist, maybe 
        if (malloc_ok) { // allocate directly with malloc if malloc_ok
            preamble = (preamble_ptr) malloc(realsize);
            goto gotit;
        }
        fprintf(stderr, "o2_malloc of %lu bytes failed\n", realsize);
        return NULL;
    }

    result = (char *) OSAtomicDequeue(p, 0);
    // invariant: result points to block of size realsize at an offset of
    // 8 (or 16 if O2MEM_DEBUG) bytes.
    if (result) {
        preamble = (preamble_ptr) (result - offsetof(preamble_t, payload));
#if O2MEM_DEBUG > 1
        printf("reallocated %p preamble %p\n", result, preamble);
#endif
        // make sure we get the expected freed block
        assert(preamble->size == size);
#if O2MEM_DEBUG
        assert(preamble->start_sentinal == 0xDea110c8dDebac1e); // cute words
        postlude = PREAMBLE_TO_POSTLUDE(preamble);
        for (int i = 0; i < O2MEM_ISOLATION; i++) {
            assert(postlude->padding[i] == 0xabababababababab);
        }
        assert(postlude->end_sentinal == 0x5ea1ed5caff01d);
#endif
        goto gotit;
    }
#if O2MEM_DEBUG // debug needs sentinal after last allocated block:
    int need_debug_space = sizeof(int64_t);
#else
    int need_debug_space = 0;
#endif
    
    if (o2_ctx->chunk_remaining < realsize + need_debug_space) {
        if (!malloc_ok) return NULL; // no more memory
        // I hate to throw away the remaining chunk, so let's use malloc
        // for anything MAX_LINEAR_BYTES or bigger. Then (with current
        // settings), we'll never lose more than 504 out of 8K, so
        // utilization is 15/16 or better.
        if (realsize >= MAX_LINEAR_BYTES) {
            // make a new chunk just for realsize, but we need to link this
            // into the chunk list so we can eventually free it, and if
            // O2MEM_DEBUG, we need an end-of-chunk sentinal
            chunk_ptr chunk2 = (chunk_ptr) malloc(realsize + sizeof(char *) +
                                                  need_debug_space);
            // add chunk2 to list
            OSAtomicEnqueue(&allocated_chunk_list, chunk2, 0);
            // skip over chunk list pointer
            preamble = &chunk2->first;
            goto gotnew;
        } 
        // note that we throw away remaining chunk if there isn't enough now
        // since we need less than MAX_LINEAR_BYTES, we know O2MEM_CHUNK_SIZE
        // is enough:
        if (!(o2_ctx->chunk = (char *) malloc(O2MEM_CHUNK_SIZE))) {
            o2_ctx->chunk_remaining = 0;
            fprintf(stderr,
                    "Warning: no more memory in o2_malloc, return NULL");
            return NULL; // can't allocate a chunk
        }
        // add allocated chunk to the list
        OSAtomicEnqueue(&allocated_chunk_list, o2_ctx->chunk, 0);
        o2_ctx->chunk += sizeof(char *); // skip over chunk list pointer
        o2_ctx->chunk_remaining = O2MEM_CHUNK_SIZE - sizeof(char *);
    }
    preamble = (preamble_ptr) o2_ctx->chunk;
    o2_ctx->chunk += realsize;
    o2_ctx->chunk_remaining -= realsize;
  gotnew:
    ((preamble_ptr) ((char *) preamble + realsize))->start_sentinal =
            0xDEADDEEDCACA0000;
  gotit: // invariant: preamble points to base of block of size realsize
    result = preamble->payload;
#if O2MEM_DEBUG
    preamble->start_sentinal = ~realsize; // sentinal is complement of size
#if O2MEM_DEBUG > 1
    printf("realsize %lld, writing size %lld at %p (%lld)\n", realsize,
           size, &preamble->size);
#endif
#endif
    preamble->size = size; // this records the size of allocation
    total_allocated += realsize;
    
#if O2MEM_DEBUG
    o2_mem_seqno++;
#if O2MEM_DEBUG > 1
    printf("  allocated from chunk seqno=%ld\n", o2_mem_seqno);
#endif
    postlude = PREAMBLE_TO_POSTLUDE(preamble);
    assert(((int64_t) postlude->padding) % 8 == 0);
    for (int i = 0; i < O2MEM_ISOLATION; i++) {
        postlude->padding[i] = 0xabababababababab;
    }
    postlude->seqno = o2_mem_seqno;
    if (o2_mem_watch_seqno == o2_mem_seqno) {
        o2_mem_watch = result;
    }
    postlude->end_sentinal = 0xBADCAFE8DEADBEEF;
    if (result == o2_mem_watch) {
        fprintf(stderr, "o2_mem_watch %p allocated at seqno %ld\n",
                result, o2_mem_seqno);
    }
#endif
    o2_mem_check(result);
    o2_mem_check_all(false);
    return result;
}


void o2_free(void *ptr)
{
    if (!ptr) {
        fprintf(stderr, "o2_free NULL ignored\n");
        return;
    }
    o2_mem_check(ptr); // if O2MEM_DEBUG undefined, this is a noop
    preamble_ptr preamble = (preamble_ptr)
            (ptr - offsetof(preamble_t, payload));
    size_t realsize = SIZE_TO_REALSIZE(preamble->size);
    if (preamble->size == 0) {
        fprintf(stderr, "o2_free block has size 0\n");
        return;
    }
#if O2MEM_DEBUG
    postlude_ptr postlude = PREAMBLE_TO_POSTLUDE(preamble);
#if O2MEM_DEBUG > 1
    printf("freeing block #%lld size %lld realsize %lld\n",
           postlude->seqno, preamble->size, realsize);
#endif
    if (ptr == o2_mem_watch) {
        fprintf(stderr, "o2_mem_watch %p freed. Block seqno %lld\n",
                ptr, postlude->seqno);
    }
    // change sentinals to indicate a free block
    preamble->start_sentinal = 0xDea110c8dDebac1e; // cute words
    postlude->end_sentinal = 0x5ea1ed5caff01d;
#endif
    // head_ptr_for_size can round up size
    OSQueueHead *head_ptr = head_ptr_for_size(&preamble->size);
    if (!head_ptr) {
        if (malloc_ok) {
            ptr -= offsetof(preamble_t, payload);
            free(ptr); // now we're freeing the originally allocated address
            return;
        }
        fprintf(stderr, "o2_free of %lu bytes failed\n", preamble->size);
        return;
    }
    total_allocated -= realsize;
    OSAtomicEnqueue(head_ptr, (char *) ptr, 0);
}


