/* o2mem.cpp -- real-time allocation
 *
 * Roger B. Dannenberg
 * April 2020
 */

/* Allocate large chunks of memory using malloc.  From the chunks,
   allocate memory as needed on O2ALIGNMENT boundaries.  Allocated 
   memory is freed by linking (lock-free) onto a freelist avoiding
   the lock in free().
 
   There are many ways to get memory: (1) small blocks come from
   freelists holding blocks of size 8, 24, 40, .... (multiples of 16
   less 8 to store the size). (2) medium blocks come from freelists
   that hold blocks of sizes that are powers of 2. (3) large blocks
   bigger than the biggest medium size block are allocated using
   malloc(), but only if malloc_ok flag is true. (4) take a block from
   chunk (if chunk is too small and malloc_ok, then allocate a new
   chunk of size O2MEM_CHUNK_SIZE). (5) If a medium block is needed
   and the chunk is too small, as a special case, a new chunk just for
   that block is allocated and used and what's left of the old chunk
   is retained for smaller allocations. A client can provide a
   fixed-sized chunk and does not need to implement or allow use of
   malloc().
 
   Since malloc()'d blocks do not have a portable way to determine
   the block size, we always allocate extra space for the block
   size and store it just before the returned block address. If
   debugging is on, we also allocate sentinals before and after the
   block, and optionally extra space at the end of the block but
   before the sentinal. This applies to memory blocks allocated from
   the chunk as well.

   To support concurrent allocation, each thread has its own chunk in
   o2_ctx. The chunks are linked onto a master list atomically and 
   freed by o2_mem_finish().

   The start sentinal after the last used block in a chunk is marked
   with O2MEM_UNUSED.
 */

/* Memory layout for "objects" (allocated blocks of memory):

       64-bit-debug    64-bit             32-bit-debug     32-bit
0x??00
0x??04
0x??08 start_sentinal
0x??0c
0x??10 padding
0x??14                                    start_sentinal
0x??18 size            size               padding
0x??1c                                    size            size
0x??20 PAYLOAD         PAYLOAD            PAYLOAD         PAYLOAD
0x??24    |               |                  |               |
0x??28    |               |                  |               |
0x??2c    |               |                  |               |
0x??30    |               |                  |               |
0x??34 END PAYLOAD     END PAYLOAD           |               |
0x??38 padding         begin next block   END PAYLOAD     END PAYLOAD
0x??3c    |                               padding         begin next block
...       |                                  |
0x??b0    |                                  |
0x??b4 end padding                           |
0x??b8 seqno                              end padding
0x??bc                                    seqno
0x??c0 end_sentinal                       end_sentinal
0x??c4                                    
------
0x??c8 begin next block

*/

#include <stddef.h>
#include <inttypes.h>
#include "o2internal.h"
#include "o2mem.h"
#include "o2atomic.h"

// note: O2MEM_ALIGN is defined in o2.h

// 32-bit architectures are 8-byte aligned. 64-bit architectures are
// 16-byte aligned:
#if (INTPTR_MAX == INT32_MAX)
#define O2MEM_ALIGN_LOG2 3
#define O2MEM_ALIGN_MASK 0x7
#define O2MEM_FREE_START 0xDea110c8
#define O2MEM_DATA_END 0xBADCAFE8
#define O2MEM_FREE_END 0x5caff01d
#define O2MEM_SAFETY 0xabababab
#define O2MEM_UNUSED 0xDEADDEED
#else
#define O2MEM_ALIGN_LOG2 4
#define O2MEM_ALIGN_MASK 0xF
#define O2MEM_FREE_START 0xDea110c8dDebac1eULL
#define O2MEM_DATA_END 0xBADCAFE8DEADBEEFULL
#define O2MEM_FREE_END 0x5ea1ed5caff01dULL
#define O2MEM_SAFETY 0xababababababababULL
#define O2MEM_UNUSED 0xDEADDEEDCACA0000ULL
#endif

#define LOG2_MAX_LINEAR_BYTES 9 // up to (512 - 8) byte chunks
#define MAX_LINEAR_BYTES (1 << LOG2_MAX_LINEAR_BYTES)
#define LOG2_MAX_EXPONENTIAL_BYTES 25 // up to 16MB = 2^24
#define MAX_EXPONENTIAL_BYTES (1 << LOG2_MAX_EXPONENTIAL_BYTES)

long o2_mem_watch_seqno = 119; // set o2_mem_watch after this many mallocs
long o2_mem_seqno = 0; // counts allocations
void *o2_mem_watch = 0; // address to watch
static bool o2_memory_mgmt = true;  // assume our memory management

#if O2MEM_DEBUG
#if EXTRA
char *chunk_base = NULL;
char *chunk_end = NULL;
#endif

#ifdef O2_NO_DEBUG
#error O2MEM_DEBUG should be 0 if O2_NO_DEBUG is defined
#endif

// When O2MEM_DEBUG is non-zero, allocate space as follows:
//
// preamble_struct: address mod O2MEM_ALIGN is (O2MEM_ALIGN / 2)
//   ~realsize or O2MEM_FREE_START -- start sentinal (4 or 8 bytes)
//                       ~realsize is for allocated, other is for freed
//   padding          -- 4 or 8 bytes of padding for 16-byte alignment
//   size             -- size of usable object memory (4 or 8 bytes)
// user data          -- this location complies with O2MEM_ALIGN_LOG
//                       total size here is realsize, which is an odd
//                       number of 32-bit or 64-bit words
// postlude_struct: address mod O2MEM_ALIGN is (O2MEM_ALIGN / 2)
//   padding          -- O2MEM_ISOLATION size_t words filled with
//                       O2MEM_SAFETY. O2MEM_ISOLATION is even.
//   sequence number  -- (4 or 8 bytes) when was this block allocated?
//   O2MEM_DATA_END or O2MEM_FREE_END -- end sentinal (4 or 8 bytes),
//                       first is for allocated, second is for freed
//   

// This is the number of words for padding after the object space
// (must be even). This can help avoid corrupting memory when an
// object writes out of bounds, as in off-by-one index bounds or
// string length errors:
#define O2MEM_ISOLATION 16

// mem_lock is for checking memory integrity -- O2 is not lock-free
// when O2MEM_DEBUG is non-zero (enabled)!
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

#endif

// Aids to reading/writing/checking memory structure:

typedef struct preamble_struct {
#if O2MEM_DEBUG
    size_t start_sentinal;
    size_t padding;  // to get 8- or 16-byte alignment
#endif
    size_t size;     // usable bytes in payload
    char payload[8]; // the application memory, aligned to O2MEM_ALIGN
} preamble_t, *preamble_ptr;

#if O2MEM_DEBUG
// the postlude_struct, used when O2MEM_DEBUG uses a multiple of O2MEM_ALIGN
// and is aligned to a multiple of O2MEM_ALIGN + O2MEM_ALIGN / 2, so the
// next location, which is the next preamble is also not aligned, leaving
// the next payload in alignment:
typedef struct postlude_struct {
    size_t padding[O2MEM_ISOLATION];
    size_t seqno;
    size_t end_sentinal;  // this will be aligned
} postlude_t, *postlude_ptr;
#define SIZEOF_POSTLUDE_T sizeof(postlude_t)
#else
#define SIZEOF_POSTLUDE_T 0
#endif

// chunks are returned from malloc on O2MEM_ALIGN boundaries, but
// we need to start allocating at O2MEM_ALIGN + O2MEM_ALIGN / 2 because
// our first word is the size field (and maybe more stuff in preamble)
// This is OK because we use the first word as the pointer to the next
// chunk.
typedef struct chunk_struct {
    struct chunk_struct *next;
    preamble_t first;  // the first block allocated from the chunk begins here
} chunk_t, *chunk_ptr;

#define IS_ALIGNED(obj) ((((uintptr_t) obj) & O2MEM_ALIGN_MASK) == 0)

#define OBJ_TO_PREAMBLE(obj)  ((preamble_ptr)  \
    (((uintptr_t) obj) - offsetof(preamble_t, payload)))

#define OBJ_SIZE(obj) (OBJ_TO_PREAMBLE(obj)->size)

// find postlude from preamble
#define PREAMBLE_TO_POSTLUDE(pre) \
        ((postlude_ptr) ((pre)->payload + (pre)->size))

// convert the requested size to actual size of allocated object space
// not including prelude with size or postlude space. Result is always
// a multiple of 16 plus an extra 8 on 64-bit architectures (so the
// object size + sizeof(size) is a multiple of 16) and plus an
// extra 12 on 32-bit architectures so that object size + sizeof(size)
// is also a multiple of 16.
#if INTPTR_MAX == INT64_MAX
#define SIZE_REQUEST_TO_ACTUAL(size) ((((size) + 7) & ~0xF) + 8)
#elif INTPTR_MAX == INT32_MAX
#define SIZE_REQUEST_TO_ACTUAL(size) ((((size) + 3) & ~0xF) + 12)
#else
#error not 32 or 64 bit architecture?
#endif
// Compute the actual number of bytes we must get from a chunk of free
// bytes, given that size is the object size passed to o2_malloc. The
// result includes the prelude with size field, the payload and debug
// postlude (if any). Note that actual size has an extra O2MEM_ALIGN
#define SIZE_TO_REALSIZE(size) (offsetof(preamble_t, payload) + \
                                (size) + SIZEOF_POSTLUDE_T)

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
// this takes 512 / 16 * 16 = 512 bytes (256 on 32-bit architecture)
// sizes are usable sizes not counting preamble or postlude space:
// linear_free has to be 02MEM_ALIGNed, so initialize it to start within
// a suitably sized block of memory
static char lf_storage[sizeof(O2queue) * ((MAX_LINEAR_BYTES / 16) + 1)];
static O2queue* linear_free = (O2queue*) O2MEM_ALIGNUP(lf_storage);


// array of freelists with log2(size) from LOG_MAX_LINEAR_BYTES + 8 to
// LOG2_MAX_EXPONENTIAL_BYTES / 2 + 8.  This takes (25 - 9) * 16 = 256 bytes.
// same alignment as used above for linear_free
static char ef_storage[sizeof(O2queue) * ((LOG2_MAX_EXPONENTIAL_BYTES -
                                           LOG2_MAX_LINEAR_BYTES) + 1)];
static O2queue *exponential_free =
                        (O2queue*) O2MEM_ALIGNUP(ef_storage);

#ifndef O2_NO_DEBUG
static int64_t o2mem_get_seqno(const void *ptr);

void *o2_dbg_malloc(size_t size, const char *file, int line)
{
    O2_DBm(dbprintf("O2_MALLOC %zd bytes in %s:%d", size, file, line));
    O2_DBm(fflush(stdout));
    void *obj = (*o2_malloc_ptr)(size);
    O2_DBm(if (o2_memory_mgmt)
               printf(" -> #%" PRId64 "@%p act_sz %zd\n",
                      o2mem_get_seqno(obj), obj, OBJ_SIZE(obj)));
    assert(obj && IS_ALIGNED(obj));
    return obj;
}

void o2_dbg_free(void *obj, const char *file, int line)
{
    O2_DBm(dbprintf("O2_FREE %zu bytes in %s:%d : #%" PRId64 "@%p\n",
                    OBJ_SIZE(obj), file, line, o2mem_get_seqno(obj), obj));
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
    void *obj = O2_MALLOC(n * s);
    memset(obj, 0, OBJ_SIZE(obj));
    return obj;
}
#else
void *o2_dbg_calloc(size_t n, size_t s, const char *file, int line)
{
    O2_DBm(dbprintf("O2_CALLOC %zu of %zu in %s:%d", n, s, file, line));
    fflush(stdout);
    void *obj = (*o2_malloc_ptr)(n * s);
    O2_DBm(printf(" -> #%" PRId64 "@%p\n", o2mem_get_seqno(obj), obj));
    assert(obj);
    memset(obj, 0, OBJ_SIZE(obj));
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
    preamble_ptr preamble = OBJ_TO_PREAMBLE(ptr);
    int64_t size = preamble->size;
    int64_t realsize = SIZE_TO_REALSIZE(size);
    postlude_ptr postlude = PREAMBLE_TO_POSTLUDE(preamble);
    if (preamble->start_sentinal == (size_t) ~realsize) {
        if (alloc_ok) goto good_sentinal;
        fprintf(stderr, "block at %p is allocated\n", ptr);
    } else if (preamble->start_sentinal == O2MEM_FREE_START) {
        if (free_ok) goto good_sentinal;
        fprintf(stderr, "block at %p has sentinal of freed block\n", ptr);
    } else {
        fprintf(stderr, "block size or sentinal mismatch in object %p, "
                "sentinal %lld (~%lld), realsize %lld, block #%lld\n",
                ptr, (long long) preamble->start_sentinal,
                (long long) ~preamble->start_sentinal, (long long) realsize,
                (long long) postlude->seqno);
        fflush(stderr);
        assert(false);
    }
    fprintf(stderr, "block #%lld size %lld\n", (long long) postlude->seqno,
            (long long) realsize);
    rslt = true;
  good_sentinal:
    for (int i = 0; i < O2MEM_ISOLATION; i++) {
        if (postlude->padding[i] != O2MEM_SAFETY) {
            fprintf(stderr, "block %p padding was overwritten, seqno %lld\n",
                    ptr, (long long) postlude->seqno);
            fflush(stderr);
            assert(false);
            return rslt;
        }
    }
    if (postlude->end_sentinal == O2MEM_FREE_END) { // freed end sentinal
        if (preamble->start_sentinal != O2MEM_FREE_START) {
            fprintf(stderr, "free block start sentinal but block %p start "
                    "indicates it\nis still allocated: end sentinal %lld "
                    "(0x%llx) size %lld\n", ptr,
                    (long long) postlude->end_sentinal,
                    (long long) postlude->end_sentinal, (long long) realsize);
            fflush(stderr);
            assert(false);
            return rslt;
        }
    } else if (postlude->end_sentinal == O2MEM_DATA_END) {
        if (preamble->start_sentinal != (size_t) ~realsize) { // allocated
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
                "(0x%llx @ %p) size %lld\n", ptr,
                (long long) postlude->end_sentinal,
                (long long) postlude->end_sentinal, &(postlude->end_sentinal),
                (long long) realsize);
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
        while (preamble->start_sentinal != O2MEM_UNUSED) {
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
    preamble_ptr preamble = OBJ_TO_PREAMBLE(ptr);
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
    assert(o2_ctx);  // Sorry - you cannot use o2mem without fully
                     // initializing O2 with o2_initialize().
    assert(sizeof(O2queue) == O2MEM_ALIGN);
    // whether we are INITIALIZED or not, we clean up when called, so this
    // *must* only be called by o2_initialize() or o2_finish() (which calls
    // o2_mem_finish().
    for (int i = 0; i < MAX_LINEAR_BYTES / 16; i++) {
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
    assert(IS_ALIGNED(chunk));
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
#endif
        chunk_ptr chunk = (chunk_ptr) allocated_chunk_list.pop();
        while (chunk) {
            free(chunk);
            chunk = (chunk_ptr) allocated_chunk_list.pop();
        }
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
    *size = SIZE_REQUEST_TO_ACTUAL(*size);
    size_t index = *size >> 4;  // in linear range, size increment is 16
    // index is 0 for 8, 1 for 24, etc. up to 31 for 504
    // or on 32-bit machines, index 0 for 12, 1 for 28, etc. up to 31 for 508
    if (index < (MAX_LINEAR_BYTES / 16)) {
        return &linear_free[index];
    }
    // The first element of exponential_free has blocks for size 520
    // (or 524 for 32-bit machines), and
    // each element has (2^N)+(16-sizeof(uintptr_t)) usable bytes,
    // not counting the size field. So to find N, we take the log2 of
    // size - (16 - sizeof(uintptr_t))
    index = power_of_2_block_size(*size - (16 - sizeof(uintptr_t)));
    if (index < LOG2_MAX_EXPONENTIAL_BYTES) {
        // what is actually available:
        *size = ((size_t) 1 << index) + (16 - sizeof(uintptr_t));
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


#if O2MEM_DEBUG
void write_debug_info_into(preamble_ptr preamble, size_t realsize)
{
    preamble->start_sentinal = (size_t) ~realsize;
    postlude_ptr postlude = PREAMBLE_TO_POSTLUDE(preamble);
    assert(IS_ALIGNED(&postlude->end_sentinal));
    for (int i = 0; i < O2MEM_ISOLATION; i++) {
        postlude->padding[i] = O2MEM_SAFETY;
    }
    postlude->seqno = o2_mem_seqno++;
    if (o2_mem_watch_seqno == o2_mem_seqno) {
        o2_mem_watch = preamble->payload;
    }
    postlude->end_sentinal = O2MEM_DATA_END;

    if (preamble->payload == o2_mem_watch) {
        fprintf(stderr, "o2_mem_watch %p allocated at seqno %ld\n",
                preamble->payload, o2_mem_seqno);
    }
}
#endif


static preamble_ptr malloc_one_object(size_t realsize, size_t size,
                                      int need_debug_space)
{
    // allocate: chunk list pointer
    //           realsize (from preamble through postlude)
    //           sentinal after postlude (if need_debug_space)
    //           extra 8 bytes (if need_debug_space)
    chunk_ptr chunk2 = (chunk_ptr) malloc(realsize + sizeof(char *) +
                                          need_debug_space * 2);
    // add chunk2 to list
    allocated_chunk_list.push((O2list_elem *) chunk2);
    preamble_ptr preamble = &chunk2->first;
    preamble->size = size;
#if O2MEM_DEBUG
#if EXTRA
    // terrible hack: we have a new chunk, but debugging assertions think
    // the end of the chunk is in chunk_end, so make it so (restore later):
    char *temp = chunk_end;
    chunk_end = ((char *) chunk2) + realsize + sizeof(char *) +
                need_debug_space * 2;
#endif
    write_debug_info_into(preamble, realsize);
    // write end-of-chunks sentinal:
    size_t *after_obj = (size_t *) ((char *) preamble + realsize);
    *after_obj = O2MEM_UNUSED;
#if EXTRA
    assert((intptr_t) after_obj < (intptr_t) chunk_end);
    chunk_end = temp;
#endif
#endif
    return preamble;
}


void *o2_malloc(size_t size)
{
    int need_debug_space = 
#if O2MEM_DEBUG // debug needs sentinal after last allocated block:
                           sizeof(size_t);
#else
                           0;
#endif
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
    // and postlude.
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
#if EXTRA
    char *endptr = chunk_end;
#endif
#endif
    if (!p) { // too big, no corresponding freelist, maybe 
        if (malloc_ok) { // allocate directly with malloc if malloc_ok
            preamble = malloc_one_object(realsize, size, need_debug_space);
            result = preamble->payload;
        } else {
            fprintf(stderr, "o2_malloc of %zu bytes failed\n", realsize);
            result = NULL;
        }
        goto done;
    }

    result = p->pop()->data;
    // invariant: result points to block of size realsize at an offset of
    // 8 (or 16 if O2MEM_DEBUG) bytes.
    assert(IS_ALIGNED(result)); // alignment check
    if (result) {
        preamble = OBJ_TO_PREAMBLE(result);
        // make sure we get the expected freed block
        assert(preamble->size == size);
#if O2MEM_DEBUG
        assert(preamble->start_sentinal == O2MEM_FREE_START);
        postlude = PREAMBLE_TO_POSTLUDE(preamble);
        for (int i = 0; i < O2MEM_ISOLATION; i++) {
            assert(postlude->padding[i] == O2MEM_SAFETY);
        }
        assert(postlude->end_sentinal == O2MEM_FREE_END);
#endif
        goto gotit;
    }

    // we did not get memory from a freelist, so we must allocate
    // from chunk or malloc.
    preamble = (preamble_ptr) o2_ctx->chunk;

    // compare end of available space to end of needed object
    if (realsize + need_debug_space > O2MEM_CHUNK_SIZE) {
        // don't even try to allocate from chunk -- even a new chunk would
        // not have enough space to service this request
        preamble = malloc_one_object(realsize, size, need_debug_space);
#if EXTRA
        endptr = ((char *) preamble) + realsize + need_debug_space;
        printf("Needed malloc_one_object to get %p, endptr %p\n",
               result, endptr);
#endif
        goto gotnew;
    } else if (o2_ctx->chunk_remaining < realsize + need_debug_space) {
        if (!malloc_ok) {
            result = NULL; // no more memory
            goto done;
        }
        // I hate to throw away the remaining chunk if there's a lot of
        // memory left to allocate, so let's use malloc
        // for anything MAX_LINEAR_BYTES or bigger that would cause us to
        // leave MAX_LINEAR_BYTES unallocated from the current chunk.
        // Then (with current settings), we'll never lose more than 504
        // out of 8K by allocating a new chunk, so utilization is > 15/16.
        if (realsize >= MAX_LINEAR_BYTES &&
            o2_ctx->chunk_remaining > MAX_LINEAR_BYTES) {
            preamble = malloc_one_object(realsize, size, need_debug_space);
#if EXTRA
            endptr = ((char *) preamble) + realsize + need_debug_space;
            printf("Used malloc_one_object to get %p, endptr %p\n",
                   result, endptr);
#endif
            goto gotnew;
        }
        // note that we throw away remaining chunk if there isn't enough now.
        // Since we need less than MAX_LINEAR_BYTES, we know O2MEM_CHUNK_SIZE
        // is enough:
        if (!(o2_ctx->chunk = (char *) malloc(O2MEM_CHUNK_SIZE))) {
            o2_ctx->chunk_remaining = 0;
            fprintf(stderr,
                    "Warning: no more memory in o2_malloc, return NULL");
            result = NULL;  // can't allocate a chunk
            goto done;
        }
#if EXTRA
        printf("new chunk at %p of size %d\n", o2_ctx->chunk, O2MEM_CHUNK_SIZE);
        chunk_base = o2_ctx->chunk;
        chunk_end = o2_ctx->chunk + O2MEM_CHUNK_SIZE;
        endptr = chunk_end;
#endif
        
        // add allocated chunk to the list
        allocated_chunk_list.push((O2list_elem *) o2_ctx->chunk);
        o2_ctx->chunk += sizeof(char *); // skip over chunk list pointer
        o2_ctx->chunk_remaining = O2MEM_CHUNK_SIZE - sizeof(char *);
        preamble = (preamble_ptr) o2_ctx->chunk;  // old preamble wasn't good
    }
    next = ((char *) preamble) + realsize;
    o2_ctx->chunk_remaining -= realsize;
    assert(o2_ctx->chunk_remaining >= 0);
    o2_ctx->chunk = next;
  gotnew:
#if O2MEM_DEBUG
    // write end-of-chunks sentinal:
    {
        size_t *after_obj = (size_t *) ((char *) preamble + realsize);
#if EXTRA
        assert(o2_ctx->chunk + o2_ctx->chunk_remaining == chunk_end);
        assert((intptr_t) after_obj < (intptr_t) endptr);
#endif
        *after_obj = O2MEM_UNUSED;
    }
#endif
  gotit: // invariant: preamble points to base of block of size realsize
    result = preamble->payload;
    preamble->size = size; // this records the size of allocation
    total_allocated += realsize;
    
#if O2MEM_DEBUG
    write_debug_info_into(preamble, realsize);
#if O2MEM_DEBUG > 1
    printf("  allocated from chunk seqno=%ld\n", o2_mem_seqno);
#endif
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
    preamble = OBJ_TO_PREAMBLE(ptr);
    realsize = SIZE_TO_REALSIZE(preamble->size);
    if (preamble->size == 0) {
        fprintf(stderr, "o2_free block has size 0\n");
        goto done;
    }
#if O2MEM_DEBUG
    postlude = PREAMBLE_TO_POSTLUDE(preamble);
#if O2MEM_DEBUG > 1
    printf("freeing block #%lld size %lld realsize %lld\n",
           (long long) postlude->seqno, (long long) preamble->size,
           (long long) realsize);
#endif
    if (ptr == o2_mem_watch) {
        fprintf(stderr, "o2_mem_watch %p freed. Block seqno %lld\n",
                ptr, (long long) postlude->seqno);
    }
    // change sentinals to indicate a free block
    preamble->start_sentinal = O2MEM_FREE_START;
    postlude->end_sentinal = O2MEM_FREE_END;

#endif
    // head_ptr_for_size can round up size
    head_ptr = head_ptr_for_size(&preamble->size);
    if (!head_ptr) {
        fprintf(stderr, "o2_free of %zu bytes (large chunk) not possible, "
                "but memory is freed when O2 is shut down\n", preamble->size);
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


// Get actual allocation size. minimum is returned if o2_malloc is not
// in use, and should be the same byte count passed to O2_MALLOC, i.e.
// it is the largest number of bytes guaranteed to be available.
//
size_t o2_allocation_size(void *obj, size_t minimum)
{
    if (o2_malloc_ptr == &o2_malloc) {
        return OBJ_SIZE(obj);
    } else {
        return minimum;
    }
}
