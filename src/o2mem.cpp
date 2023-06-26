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
   freelists holding blocks of size 8, 24, 40, .... (multiples of 16
   less 8 to store the size). (2) medium blocks come from freelists
   that hold blocks of sizes that are powers of 2. (3) large blocks
   bigger than the biggest medium size block are allocated using
   malloc(), but only if malloc_ok flag is true. (4) take a block from
   chunk (if chunk is too small and malloc_ok, then allocate a new
   chunk). (5) If a medium block is needed and the chunk is too small,
   as a special case, a new chunk just for that block is allocated and
   used and what's left of the old chunk is retained for. Until the
   chunk doesn't even have enough remaining so service a small block
   allocation. Then it is replaced with a new chunk of size
   O2MEM_CHUNK_SIZE (if malloc_ok). A client can provide a fixed-sized
   chunk and does not need to implement or allow use of malloc().
 
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

#include <stddef.h>
#include <inttypes.h>
#include "o2internal.h"
#include "o2mem.h"
#include "o2atomic.h"

#define O2MEM_ALIGN_LOG2 4
#define LOG2_MAX_LINEAR_BYTES 9 // up to (512 - 8) byte chunks
#define MAX_LINEAR_BYTES (1 << LOG2_MAX_LINEAR_BYTES)
#define LOG2_MAX_EXPONENTIAL_BYTES 25 // up to 16MB = 2^24
#define MAX_EXPONENTIAL_BYTES (1 << LOG2_MAX_EXPONENTIAL_BYTES)

// to deal with cross-platform issues, 32-bits is plenty for length
// and long (and %ld) are standard, so we'll store 64 bits for
// alignment but return a long, whatever that is...
#define O2_OBJ_SIZE(obj) ((long) (((int64_t *) ((obj)->data))[-1]))

long o2_mem_watch_seqno = 197; // set o2_mem_watch after this many mallocs
long o2_mem_seqno = 0; // counts allocations
void *o2_mem_watch = 0; // address to watch
static bool o2_memory_mgmt = true;  // assume our memory management

#if O2MEM_DEBUG

#ifdef O2_NO_DEBUG
#error O2MEM_DEBUG should be 0 if O2_NO_DEBUG is defined
#endif

#ifdef WIN32
#include <windows.h>
static HANDLE o2mem_lock;

static void mem_lock()
{
    DWORD rslt = WaitForSingleObject(o2mem_lock, INFINITE);
    assert(rslt == WAIT_OBJECT_0);
}

static void mem_unlock()
{
    DWORD rslt = ReleaseMutex(o2mem_lock);
    assert(rslt);
    
}
#else
#include <pthread.h>
static pthread_mutex_t o2mem_lock = PTHREAD_MUTEX_INITIALIZER;

static void mem_lock()
{
    pthread_mutex_lock(&o2mem_lock);
}

static void mem_unlock()
{
    pthread_mutex_unlock(&o2mem_lock);
}
#endif

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
//   padding          -- O2MEM_ISOLATION bytes filled with zero
//   sequence number  -- when was this block allocated?
//   BADCAFE8DEADBEEF or 5ea1ed5caff01d   -- end sentinal (8 bytes),
//                     first is for allocated, second is for freed
//   
// This is the number of 8-byte words for padding (must be odd). This can help
// avoid corrupting memory when an object writes out of bounds, as in off-by-one
// index bounds or string length errors:
#define O2MEM_ISOLATION 15

#endif

// Aids to reading/writing/checking memory structure:

typedef struct preamble_struct {
#if O2MEM_DEBUG
    int64_t start_sentinal;
#endif
    size_t size;    // usable bytes in payload, aligned to 16-byte boundary
#if (INTPTR_MAX == INT32_MAX)
    int32_t padding;  // for 32-bit machines, pad the size to 8 bytes
#endif
    char payload[8]; // the application memory, odd number of 8-byte units
} preamble_t, *preamble_ptr;

#if O2MEM_DEBUG
typedef struct postlude_struct {
    int64_t padding[O2MEM_ISOLATION];
    int64_t seqno;  // this will be 16-byte aligned
    int64_t end_sentinal;
} postlude_t, *postlude_ptr;
#define SIZEOF_POSTLUDE_T sizeof(postlude_t)
#else
#define SIZEOF_POSTLUDE_T 0
#endif

typedef struct chunk_struct {
    struct chunk_struct *next;
#if O2MEM_DEBUG
    char *padding;  // used to align preamble to 16-bytes
#endif
    preamble_t first;
} chunk_t, *chunk_ptr;


// find postlude from preamble
#define PREAMBLE_TO_POSTLUDE(pre) \
        ((postlude_ptr) ((pre)->payload + (pre)->size))

#define SIZE_TO_REALSIZE(size) \
    ((size) + offsetof(preamble_t, payload) + SIZEOF_POSTLUDE_T)

// should be *MUCH BIGGER* than MAX_LINEAR_BYTES (see o2_malloc)
#define O2MEM_CHUNK_SIZE (1 << 13) // 13 is much bigger than 9
#define MAX(x, y) ((x) > (y) ? (x) : (y))

typedef enum { UNINITIALIZED, NOT_USED, INITIALIZED } O2mem_state;
static O2mem_state o2mem_state = UNINITIALIZED;

static bool malloc_ok = true;
static size_t total_allocated = 0;
static O2queue allocated_chunk_list;

static void *o2_malloc(size_t size);
static void o2_free(void *);

void *((*o2_malloc_ptr)(size_t size)) = &o2_malloc;
void ((*o2_free_ptr)(void *)) = &o2_free;

// array of freelists for sizes from 8 to MAX_LINEAR_BYTES-8 by 16
// this takes 512 / 16 * 16 = 512 bytes
// sizes are usable sizes not counting preamble or postlude space:
// linear_free has to be 16-byte aligned, so initialize it to start within
// a suitably sized block of memory
static char lf_storage[sizeof(O2queue) * (MAX_LINEAR_BYTES / O2MEM_ALIGN) + 15];
static O2queue* linear_free = (O2queue*) ((((uintptr_t)lf_storage) + 15) & ~0xF);


// array of freelists with log2(size) from MAX_LINEAR_BYTES + 8 to
// LOG2_MAX_EXPONENTIAL_BYTES / 2 + 8.  This takes (25 - 9) * 16 = 256 bytes.
// same 16-byte alignment as used above for linear_free
static char ef_storage[sizeof(O2queue) * (LOG2_MAX_EXPONENTIAL_BYTES -
                                          LOG2_MAX_LINEAR_BYTES) + 15];
static O2queue *exponential_free = 
        (O2queue*)((((uintptr_t)ef_storage) + 15) & ~0xF);

#ifndef O2_NO_DEBUG
static int64_t o2mem_get_seqno(const void *ptr);

void *o2_dbg_malloc(size_t size, const char *file, int line)
{
    O2_DBm(printf("%s O2_MALLOC %zd bytes in %s:%d", o2_debug_prefix, 
                  size, file, line));
    O2_DBm(fflush(stdout));
    void *obj = (*o2_malloc_ptr)(size);
    O2_DBm(if (o2_memory_mgmt)
               printf(" -> #%" PRId64 "@%p\n", o2mem_get_seqno(obj), obj));
    assert(obj && ((((intptr_t) obj) & 0xF) == 0));
    return obj;
}

void o2_dbg_free(void *obj, const char *file, int line)
{
    O2_DBm(printf("%s O2_FREE %ld bytes in %s:%d : #%" PRId64 "@%p\n",
                  o2_debug_prefix, O2_OBJ_SIZE((O2list_elem *) obj),
                  file, line, o2mem_get_seqno(obj), obj));
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
#ifdef O2_NO_DEBUG
void *o2_calloc(size_t n, size_t s)
{
    void *loc = O2_MALLOC(n * s);
    memset(loc, 0, n * s);
    return loc;
}
#else
void *o2_dbg_calloc(size_t n, size_t s, const char *file, int line)
{
    O2_DBm(printf("%s O2_CALLOC %zu of %zu in %s:%d", o2_debug_prefix,
                  n, s, file, line));
    fflush(stdout);
    void *obj = (*o2_malloc_ptr)(n * s);
    O2_DBm(printf(" -> #%" PRId64 "@%p\n", o2mem_get_seqno(obj), obj));
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
static bool block_check(void *ptr, int alloc_ok, int free_ok)
{
    bool rslt = false;
    preamble_ptr preamble = (preamble_ptr)
            ((char *) ptr - offsetof(preamble_t, payload));
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
                ptr, (long long) ~preamble->start_sentinal,
                (long long) realsize);
        fprintf(stderr, "block #%lld size %lld\n",
                (long long) postlude->seqno, (long long) realsize);
        fflush(stderr);
        assert(false);
    }
    fprintf(stderr, "block #%lld size %lld\n", (long long) postlude->seqno,
            (long long) realsize);
    rslt = true;
  good_sentinal:
    for (int i = 0; i < O2MEM_ISOLATION; i++) {
        if (postlude->padding[i] != 0xabababababababab) {
            fprintf(stderr, "block %p padding was overwritten, seqno %lld\n",
                    ptr, (long long) postlude->seqno);
            fflush(stderr);
            assert(false);
            return rslt;
        }
    }
    if (postlude->end_sentinal == 0x5ea1ed5caff01d) { // freed end sentinal
        if (preamble->start_sentinal != 0xDea110c8dDebac1e) {
            fprintf(stderr, "free block start sentinal but block %p start "
                    "indicates it\nis still allocated: end sentinal %lld "
                    "(0x%llx) size %lld\n", ptr,
                    (long long) postlude->end_sentinal,
                    (long long) postlude->end_sentinal, (long long) realsize);
            fflush(stderr);
            assert(false);
            return rslt;
        }
    } else if (postlude->end_sentinal == 0xBADCAFE8DEADBEEF) {
        if (preamble->start_sentinal != ~realsize) { // allocated
            fprintf(stderr, "allocated block start sentinal but block %p "
                    "start indicates it\nis freed: end sentinal %lld "
                    "(0x%llx) size %lld\n", ptr,
                    (long long) postlude->end_sentinal,
                    (long long) postlude->end_sentinal, (long long) realsize);
            fflush(stderr);
            assert(false);
            return rslt;
        }
    } else {
        fprintf(stderr, "block %p has invalid end sentinal %lld "
                "(0x%llx) size %lld\n", ptr,
                (long long) postlude->end_sentinal,
                (long long) postlude->end_sentinal, (long long) realsize);
        fflush(stderr);
        assert(false);
        return rslt;
    }
    return rslt;
}


// mem_check_all - check for valid heap, optional check for leaks
//
static bool mem_check_all(int report_leaks)
{
    chunk_ptr chunk = (chunk_ptr) allocated_chunk_list.first();
    bool leak_found = false;
    while (chunk) { // walk the chunk list and see if everything is freed
        // within each chunk, blocks are allocated sequentially
        preamble_ptr preamble = &chunk->first;
        while (preamble->start_sentinal != 0xDEADDEEDCACA0000) {
            leak_found |= block_check(preamble->payload, !report_leaks, true);
            preamble = (preamble_ptr) (PREAMBLE_TO_POSTLUDE(preamble) + 1);
        }
        chunk = chunk->next;
    }
    return leak_found;
}


static void mem_check(void *ptr)
{
    if (!o2_memory_mgmt) return;  // not using our memory management
    block_check(ptr, true, false);
#if O2MEM_DEBUG > 1
    // this is much more expensive, so default O2MEM_DEBUG=1 does not call it
    mem_check_all(false);
#endif
}

// this is the externally visible function: it uses mutex because
// it might scan the private chunks of other threads.
void o2_mem_check(void *ptr)
{
    mem_lock();
    mem_check(ptr);
    mem_unlock();
}

static bool o2_mem_check_all(int report_leaks)
{
    mem_lock();
    bool rslt = mem_check_all(report_leaks);
    mem_unlock();
    return rslt;
}


int64_t o2mem_get_seqno(const void *ptr)
{
    preamble_ptr preamble = (preamble_ptr)
            ((char *) ptr - offsetof(preamble_t, payload));
    postlude_ptr postlude = PREAMBLE_TO_POSTLUDE(preamble);
    return postlude->seqno;
}
#else
int64_t o2mem_get_seqno(const void *ptr)
{
    return 0;
}
#endif


// The o2mem state machine:
// Initial state = UNINITIALIZED
//     o2_memory() is called: state -> NOT_USED, return O2_SUCCESS
//     o2_mem_init() is called: state -> INITIALIZED
//     o2_mem_finish() is called: state unchanged
// NOT_USED
//     o2_memory() is called: state unchanged, return O2_FAIL
//     o2_mem_init() is called: state unchanged
//     o2_mem_finish(): is called: state -> UNINITIALIZED
//
// INITIALIZED
//     o2_memory() is called: state unchanged, return O2_FAIL
//     o2_mem_init() is called: state unchanged
//     o2_mem_finish() is called: state -> UNINITIALIZED


// configure memory before o2_initialize(). first_chunk is never freed -
//    it is owned by the caller and may be freed after o2_finish().
int o2_memory(void *((*malloc)(size_t size)), void ((*free)(void *)),
              char *first_chunk, int64_t size, bool mallocp)
{
    if (o2mem_state != UNINITIALIZED) { // error to change configuration!
        return O2_FAIL;
    }
    if (malloc && free) {
        o2mem_state = NOT_USED;
        o2_malloc_ptr = malloc;
        o2_free_ptr = free;
        o2_memory_mgmt = false;  // disable some consistency checks
    } else if (!malloc && !free) {
        o2_mem_init(first_chunk, size);
        malloc_ok = mallocp;
    }        
    return O2_SUCCESS;
}


void o2_mem_init(char *chunk, int64_t size)
{
    if (o2mem_state == NOT_USED) {
        return;
    }
    assert(sizeof(O2queue) == 16);
    // whether we are INITIALIZED or not, we clean up when called, so this
    // *must* only be called by o2_initialize() or o2_finish() (which calls
    // o2_mem_finish().
    for (int i = 0; i < MAX_LINEAR_BYTES / O2MEM_ALIGN; i++) {
        linear_free[i].clear();
    }
    for (int i = 0;
        i < LOG2_MAX_EXPONENTIAL_BYTES - LOG2_MAX_LINEAR_BYTES; i++) {
        exponential_free[i].clear();
    }
    if (o2mem_state == INITIALIZED) {  // we were called by o2_mem_finish()
        return;
    }
#if O2MEM_DEBUG
#ifdef WIN32
    o2mem_lock = CreateMutex(NULL, FALSE, NULL);
#endif
#endif
    o2mem_state = INITIALIZED;
    o2_ctx->chunk = chunk;
    o2_ctx->chunk_remaining = size;
}


void o2_mem_finish()
{
    if (o2mem_state == INITIALIZED) {
#if O2MEM_DEBUG
        // this is expensive, but so is shutting down, so why not...
        printf("**** o2_mem_finish checking for memory leaks...\n");
        printf("**** o2_mem_finish detected %sleaks.\n",
               o2_mem_check_all(true) ? "" : "NO ");
#else
        chunk_ptr chunk = (chunk_ptr) allocated_chunk_list.pop();
        while (chunk) {
            free(chunk);
            chunk = (chunk_ptr) allocated_chunk_list.pop();
        }
#endif
        o2_mem_init(NULL, 0); // remove free lists
        o2mem_state = UNINITIALIZED;
    }
}


// return (log2 of size) rounded up to at least LOG2_MAX_LINEAR_BYTES,
// or return LOG2_MAX_EXPONENTIAL_BYTES if
// log2(size) is >= LOG2_MAX_EXPONENTIAL_BYTES
//
static int power_of_2_block_size(size_t size)
{
    int log = LOG2_MAX_LINEAR_BYTES;
    while (log < LOG2_MAX_EXPONENTIAL_BYTES && size > ((size_t) 1 << log))
        log++;
    return log;
}


// returns a pointer to the sublist for a given size.  Sets *size to the
// actual allocation size, which is at least as great as initial value of *size
static O2queue *head_ptr_for_size(size_t *size)
{
    *size = ((*size + 7) & ~7) | 8;  // computes odd number of 8-byte units
    size_t index = *size >> O2MEM_ALIGN_LOG2;
    // index is 0 for 8, 1 for 24, etc. up to 31 for 504
    if (index < (MAX_LINEAR_BYTES / O2MEM_ALIGN)) {
        return &linear_free[index];
    }
    // The first element of exponential_free has blocks for size 520, and
    // each element has (2^N)+8 usable bytes, not counting the size field.
    // So to find N, we take the log2 of size-8.
    index = power_of_2_block_size(*size - 8);
    if (index < LOG2_MAX_EXPONENTIAL_BYTES) {
        *size = ((size_t) 1 << index) + 8;  // what is actually available
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
    int need_debug_space = 0;
    char *next;
    if (o2mem_state != INITIALIZED) {
        fprintf(stderr, "o2_malloc: o2mem_state != INITIALIZED\n");
        assert(false);  // application code could recover from this,
        return NULL;    // but it seems unlikely, so assert(false).
    }
#if O2MEM_DEBUG
    // debugging code scans all chunks, so we need exclusive access:
    mem_lock();
#if O2MEM_DEBUG > 1
    mem_check_all(false);
#endif
#endif
    // round up to odd number of 8-byte units, add room for preamble
    // and postlude, but postlude includes an extra 8-byte sentinal
    // that's not part of the allocated block, so subtract that to get
    // realsize:
    char *result; // allocate by malloc, find on freelist, or carve off chunk
    // find what really gets allocated. Large blocks especially are
    // rounded up to a power of two.
    O2queue *p = head_ptr_for_size(&size);
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
        fprintf(stderr, "o2_malloc of %zu bytes failed\n", realsize);
        result = NULL;
        goto done;
    }

    result = p->pop()->data;
    // invariant: result points to block of size realsize at an offset of
    // 8 (or 16 if O2MEM_DEBUG) bytes.
    assert(((uintptr_t) result & 0xF) == 0); // alignment check
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
    need_debug_space = sizeof(int64_t);
#endif
    
    if (o2_ctx->chunk_remaining < realsize + need_debug_space) {
        if (!malloc_ok) {
            result = NULL; // no more memory
            goto done;
        }
        // I hate to throw away the remaining chunk, so let's use malloc
        // for anything MAX_LINEAR_BYTES or bigger. Then (with current
        // settings), we'll never lose more than 504 out of 8K, so
        // utilization is 15/16 or better.
        if (realsize >= MAX_LINEAR_BYTES) {
            // make a new chunk just for realsize, but we need to link
            // this into the chunk list so we can eventually free it,
            // so allocate an extra pointer (sizeof(char *)) and if
            // O2MEM_DEBUG, we need to offset the preamble by an extra
            // 8 bytes to align with 16 bytes, and we need an
            // end-of-chunk sentinal, so add need_debug_space *
            // 2. Since realsize is a multiple of 16, we'll actually
            // allocate either realsize + 16 or realsize + 32.
            chunk_ptr chunk2 = (chunk_ptr) malloc(realsize + sizeof(char *) +
                                                  need_debug_space * 2);
            // add chunk2 to list
            allocated_chunk_list.push((O2list_elem *) chunk2);
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
            result = NULL;  // can't allocate a chunk
            goto done;
        }
        // add allocated chunk to the list
        allocated_chunk_list.push((O2list_elem *) o2_ctx->chunk);
        o2_ctx->chunk += sizeof(char *); // skip over chunk list pointer
        o2_ctx->chunk_remaining = O2MEM_CHUNK_SIZE - sizeof(char *);
#if (INTPTR_MAX == INT32_MAX)
        o2_ctx->chunk += 4; // skip another 4 bytes to get 8-byte alignment
        o2_ctx->chunk_remaining -= 4;  // lost those 4 bytes
#endif
    }
    preamble = (preamble_ptr) o2_ctx->chunk;
#if O2MEM_DEBUG
    // round up to 16-byte boundary if necessary:
    preamble = (preamble_ptr) O2MEM_ALIGNUP((uintptr_t) preamble);
#else
    // must align size to an odd number of 8-byte units so that the payload
    // is 16-byte aligned. Low-order preamble bits are either 0000 or 1000.
    // If 0000, we want to add 8, giving 1000, otherwise we want to do nothing.
    // Or'ing wtih 1000 will do it:
    preamble = (preamble_ptr) (((uintptr_t) preamble) | 8);
#endif
    next = ((char *) preamble) + realsize;
    o2_ctx->chunk_remaining -= (next - o2_ctx->chunk);
    assert(o2_ctx->chunk_remaining >= 0);
    o2_ctx->chunk = next;
  gotnew:
#if O2MEM_DEBUG
    ((preamble_ptr) ((char *) preamble + realsize))->start_sentinal =
            0xDEADDEEDCACA0000;
#endif
  gotit: // invariant: preamble points to base of block of size realsize
    result = preamble->payload;
#if O2MEM_DEBUG
    preamble->start_sentinal = ~realsize; // sentinal is complement of size
#if O2MEM_DEBUG > 1
    printf("realsize %lld, writing size %lld at %p\n", (long long) realsize,
           (long long) size, &preamble->size);
#endif
#endif
    preamble->size = size; // this records the size of allocation
    total_allocated += realsize;
    
#if O2MEM_DEBUG
    o2_mem_seqno++;  // there could be a race condition here with a sharedmem
                     // thread, but these sequence numbers don't have a lot of
                     // value when sequencing is not deterministic.
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
  done:
#if O2MEM_DEBUG
#if O2MEM_DEBUG > 1
    mem_check_all(false);
#endif
    mem_unlock();
#endif
    return result;
}


void o2_free(void *ptr)
{
    size_t realsize;
    preamble_ptr preamble;
    O2queue *head_ptr;
    if (o2mem_state != INITIALIZED) {
        fprintf(stderr, "o2_free: o2mem_state != INITIALIZED\n");
        return;
    }
#if O2MEM_DEBUG
    postlude_ptr postlude;
    mem_lock();
#endif
    if (!ptr) {
        fprintf(stderr, "o2_free NULL ignored\n");
        goto done;
    }
#if O2MEM_DEBUG
    mem_check(ptr); // if O2MEM_DEBUG undefined, this is a noop
#endif
    preamble = (preamble_ptr) ((char *) ptr - offsetof(preamble_t, payload));
    realsize = SIZE_TO_REALSIZE(preamble->size);
    if (preamble->size == 0) {
        fprintf(stderr, "o2_free block has size 0\n");
        goto done;
    }
#if O2MEM_DEBUG
    postlude = PREAMBLE_TO_POSTLUDE(preamble);
#if O2MEM_DEBUG > 1
    printf("freeing block #%lld size %lld realsize %lld\n",
           postlude->seqno, (long long) preamble->size, (long long) realsize);
#endif
    if (ptr == o2_mem_watch) {
        fprintf(stderr, "o2_mem_watch %p freed. Block seqno %lld\n",
                ptr, (long long) postlude->seqno);
    }
    // change sentinals to indicate a free block
    preamble->start_sentinal = 0xDea110c8dDebac1e; // cute words
    postlude->end_sentinal = 0x5ea1ed5caff01d;
#endif
    // head_ptr_for_size can round up size
    head_ptr = head_ptr_for_size(&preamble->size);
    if (!head_ptr) {
        if (malloc_ok) {
            ptr = (char *) ptr - offsetof(preamble_t, payload);
            free(ptr); // now we're freeing the originally allocated address
            goto done;
        }
        fprintf(stderr, "o2_free of %zu bytes failed\n", preamble->size);
        goto done;
    }
    total_allocated -= realsize;
    head_ptr->push((O2list_elem *) ptr);
  done:
#if O2MEM_DEBUG
    mem_unlock();
#endif
    return;
}


