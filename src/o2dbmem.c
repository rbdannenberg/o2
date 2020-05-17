#include "o2internal.h"
#include <libkern/OSAtomic.h>
#include <libkern/OSAtomicQueue.h>
#include "o2mem.h"

#define TRACE_ADDR 0x100802318

static void *o2_malloc(size_t size, const char *file, int line);
static void o2_free(void *, const char *file, int line);

void *((*o2_malloc_ptr)(size_t size)) = NULL;
void ((*o2_free_ptr)(void *)) = NULL;

#ifndef O2_NO_DEBUG
void *o2_dbg_malloc(size_t size, const char *file, int line)
{
    O2_DBm(printf("%s O2_MALLOC %lld in %s:%d", o2_debug_prefix, 
                  (long long) size, file, line));
    fflush(stdout);
    void *obj = o2_malloc(size, file, line);
    O2_DBm(printf(" -> %p\n", obj));
    assert(obj);
    return obj;
}


void o2_dbg_free(const void *obj, const char *file, int line)
{
    O2_DBm(printf("%s O2_FREE in %s:%d <- %p\n", 
                  o2_debug_prefix, file, line, obj));
    // bug in C. free should take a const void * but it doesn't
    o2_free((void *) obj, file, line);
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
    void *obj = o2_malloc(s, file, line);
    O2_DBm(printf(" -> %p\n", obj));
    assert(obj);
    memset(obj, 0, n * s);
    return obj;
}
#endif


int o2_memory(void *((*malloc)(size_t size)), void ((*free)(void *)))
{
    o2_malloc_ptr = malloc;
    o2_free_ptr = free;
    return O2_SUCCESS;
}

#define PADSIZE 32
typedef struct memchunk {
    struct memchunk *next;
    size_t size;
    const char *file;
    int line;
    int64_t free_flag;
    int64_t prepad[PADSIZE];
    int64_t thechunk[PADSIZE];
} memchunk, *memchunk_ptr;

memchunk_ptr memlist = NULL;

void o2_mem_init(char *first_chunk, int64_t size, int mallocp)
{
    return;
}

void *o2_malloc(size_t size, const char *file, int line)
{
    memchunk_ptr p = (memchunk_ptr) malloc(size + sizeof(memchunk) + 16);
    p->next = memlist;
    p->size = size;
    p->file = file;
    p->line = line;
    p->free_flag = FALSE;
    memlist = p;
    int64_t *postpad = p->thechunk + ((size + 7) / 8);
    for (int i = 0; i < PADSIZE; i++) {
        p->prepad[i] = ((int64_t) p) + 1;
        postpad[i] = ((int64_t) p) + 3;
    }
    if (p->thechunk == (int64_t *) TRACE_ADDR) {
        printf("allocating TRACE_ADDR %p\n", p->thechunk);
    }
    return p->thechunk;
}


void o2_free(void *ptr, const char *file, int line)
{
    if (!ptr) return;
    char *cptr = (char *) ptr;
    ssize_t offset = (char *) (((memchunk_ptr) ptr)->thechunk) -
                     (char *) ((memchunk_ptr) ptr);
    cptr -= offset;
    memchunk_ptr p = (memchunk_ptr) cptr;
    if (p->thechunk == (int64_t *) TRACE_ADDR) {
        printf("freeing TRACE_ADDR %p file %s line %d\n",
               p->thechunk, p->file, p->line);
    }
    assert(p->free_flag == FALSE);
    p->free_flag = TRUE;
}


void o2_mem_check(void *ptr)
{
    int foundit = FALSE;
    memchunk_ptr p = memlist;
    while (p) {
        if (!(p->free_flag) && ((void *) (p->thechunk)) == ptr) {
            foundit = TRUE;
        }
        int64_t *postpad = p->thechunk + ((p->size + 7) / 8);
        for (int i = 0; i < PADSIZE; i++) {
            assert(p->prepad[i] == ((int64_t) p) + 1);
            assert(postpad[i] == ((int64_t) p) + 3);
        }
        p = p->next;
    }
    assert(foundit);
}


void o2_mem_finish()
{
    while (memlist) {
        memchunk_ptr p = memlist;        
        memlist = memlist->next;
        if (!p->free_flag) {
            printf("o2_mem_finish: O2 did not free %p size %ld file %s line %d%s\n",
                   p->thechunk, p->size, p->file, p->line,
                   p->thechunk == (int64_t *) TRACE_ADDR ? " (TRACE_ADDR)" : "");
        }
        free(p);
    }
    printf("o2_mem_finish complete\n");
}
