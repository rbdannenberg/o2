// o2base.h -- core programming definitions

#include <cstddef>
#include <cstdint>
using std::size_t;
using std::int32_t;

/** \defgroup basics Basics
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif
    
// room for IP address in dot notation and terminating EOS
#define O2_IP_LEN 16
// room for longest string/address of the form:
//   /_publicIP:localIP:port + padding_to_int32_boundary
#define O2_MAX_PROCNAME_LEN 32

// limit on ensemble name length
#define O2_MAX_NAME_LEN 63

extern void *((*o2_malloc_ptr)(size_t size));
extern void ((*o2_free_ptr)(void *));

/** \brief allocate memory
 *
 * O2 allows you to provide custom heap implementations to avoid
 * priority inversion or other real-time problems. Normally, you
 * should not need to explicitly allocate memory since O2 functions
 * are provided to allocate, construct, and deallocate messages, but
 * if you need to allocate memory, especially in an O2 message
 * handler callback, i.e. within the sphere of O2 execution, you
 * should use #O2_MALLOC, #O2_FREE, and #O2_CALLOC.
 */
#ifndef O2_MALLOC
#ifdef NO_O2_DEBUG
#define O2_MALLOC(x) (*o2_malloc_ptr)(x)
#define O2_MALLOCNT(n, typ) ((typ *) ((*o2_malloc_ptr)((n) * sizeof(typ))))
#else
void *o2_dbg_malloc(size_t size, const char *file, int line);
#define O2_MALLOC(x) o2_dbg_malloc(x, __FILE__, __LINE__)
#define O2_MALLOCNT(n, typ) \
        ((typ *) o2_dbg_malloc((n) * sizeof(typ), __FILE__, __LINE__))
#endif
#define O2_MALLOCT(typ) O2_MALLOCNT(1, typ)
#endif

/** \brief free memory allocated by #O2_MALLOC */
#ifndef O2_FREE
#ifdef NO_O2_DEBUG
#define O2_FREE(x) (*o2_free_ptr)(x)
#else
void o2_dbg_free(const void *obj, const char *file, int line);
#define O2_FREE(x) o2_dbg_free(x, __FILE__, __LINE__)
#endif
#endif

/** \brief allocate and zero memory (see #O2_MALLOC) */
#ifndef O2_CALLOC
#ifdef NO_O2_DEBUG
void *o2_calloc(size_t n, size_t s);
#define O2_CALLOC(n, s) o2_calloc(n, s)
#define O2_CALLOCNT(n, typ) ((typ *) o2_calloc(n, sizeof(typ)))
#else
void *o2_dbg_calloc(size_t n, size_t s, const char *file, int line);
#define O2_CALLOC(n, s) o2_dbg_calloc(n, s, __FILE__, __LINE__)
#define O2_CALLOCNT(n, typ) \
        ((typ *) o2_dbg_calloc(n, sizeof(typ), __FILE__, __LINE__))
#endif
#define O2_CALLOCT(typ) O2_CALLOCNT(1, typ)
#endif

// if debugging is on, default is O2MEM_DEBUG
#ifndef O2MEM_DEBUG
#ifdef NO_O2_DEBUG
// set to 0 for no memory debug mode
#define O2MEM_DEBUG 0
#else
// set this to 2 to get verbose memory information
#define O2MEM_DEBUG 1
#endif
#endif

    
// if O2_MEMDEBUG, extra checks are made for memory consistency,
// and you can check any pointer using o2_mem_check(ptr):
#ifdef O2MEM_DEBUG
void o2_mem_check(void *ptr);
#else
#define o2_mem_check(ptr) 0 // make #o2_mem_check a noop
#endif

#ifdef __cplusplus
}
#endif

/** @} */ // end of Basics
