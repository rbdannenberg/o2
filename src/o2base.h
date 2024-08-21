// o2base.h -- core programming definitions


/** \defgroup basics Basics
 * @{
 */

#ifdef O2_EXPORT
#    error O2_EXPORT defined somewhere before o2base.h
#else
#    if (defined WIN32 || defined __CYGWIN__ || defined _WIN32) && \
        defined BUILD_SHARED_LIBS
         // note: hidden feature of CMake is it defines {libraryname}_EXPORTS
         //     when building a shared library:
#        if defined o2_EXPORTS || defined o2lite_EXPORTS
             // We are building this library
#            ifdef __GNUC__
#                define O2_EXPORT __attribute__ ((dllexport)) extern
#            elif defined _MSC_VER
#                define O2_EXPORT __declspec(dllexport) extern
#                define O2_CLASS_EXPORT __declspec(dllexport)
#            else
#                error no rule to set O2_EXPORT for this configuration
#            endif
#        else
             // We are using this library */
#            ifdef __GNUC__
#                define O2_EXPORT __attribute__ ((dllimport)) extern
#            elif defined _MSC_VER
#                define O2_EXPORT __declspec(dllimport) extern
#                define O2_CLASS_EXPORT __declspec(dllimport)
#            else
#                error no rule to set O2_EXPORT for this configuration
#            endif
#        endif
#    else
#        if __GNUC__ >= 4
#            define O2_EXPORT __attribute__ ((visibility("default")))
#        else
#            define O2_EXPORT extern
#            define O2_CLASS_EXPORT 
#        endif
#    endif
#endif

// do we really need to export any classes? O2 nominally has a C API,
// so unless someone needs to "peek" into the implementation, we should
// hide classes. Nevertheless, some classes are declared O2_CLASS_EXPORT,
// so rather than assume that's wrong, I'm going to disable it and see
// what happens. We can undo this and extend the O2_EXPORT/O2_CLASS_EXPORT
// definitions above if we really want to export any classes.
#ifdef O2_CLASS_EXPORT
#undef O2_CLASS_EXPORT
#endif
// disable O2_CLASS_EXPORT for everyone:
#define O2_CLASS_EXPORT

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>
using std::size_t;
using std::int32_t;

extern "C" {
#else
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#endif

#ifndef streql
#define streql(a, b) (strcmp((a), (b)) == 0)
#endif

/**
 * \brief Suspend for n milliseconds
 *
 * @param n number of milliseconds to sleep
 *
 */
O2_EXPORT void o2_sleep(int n);


/**
 * \brief Convert hex string to integer.
 *
 * @param hex a string of hex digits (no minus sign allowed)
 *
 * @return a positive integer
 */
unsigned int o2_hex_to_int(const char *hex);


/**
* \brief Convert from hex format to dot format IP address
*
* O2 uses 8 digit hexadecimal notation for IP addresses, mostly
* internally. To convert to the more conventional "dot" notation,
* e.g. "127.0.0.1", call #o2_hex_to_dot.
*
* @param hex is a string containing an 8 character hexadecimal IP address.
*
* @param dot is a memory area of size O2N_IP_LEN or greater where the dot
*         notation is written.
*
*/
// void o2_hex_to_dot(const char *hex, char *dot);


    
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
 * should use #O2_MALLOC, #O2_FREE, and #O2_CALLOC and their variants.
 *
 * The O2lite library shares some code with O2 but not memory allocation.
 * To simplify things, you can just define O2_MALLOC, e.g. to be malloc,
 * and define O2_FREE, e.g. to be free, and the logic here will provide
 * implementations of O2_MALLOCT, O2_MALLOCNT, etc.
 */
#ifndef O2_MALLOC
#ifdef O2_NO_DEBUG
#define O2_MALLOC(x) (*o2_malloc_ptr)(x)
#define O2_MALLOCNT(n, typ) ((typ *) ((*o2_malloc_ptr)((n) * sizeof(typ))))
#else
O2_EXPORT void *o2_dbg_malloc(size_t size, const char *file, int line);
#define O2_MALLOC(x) o2_dbg_malloc(x, __FILE__, __LINE__)
#endif
#endif

#ifndef O2_MALLOCNT
#define O2_MALLOCNT(n, typ) ((typ *) O2_MALLOC((n) * sizeof(typ)))
#endif

#ifndef O2_MALLOCT
#define O2_MALLOCT(typ) O2_MALLOCNT(1, typ)
#endif

/** \brief free memory allocated by #O2_MALLOC */
#ifndef O2_FREE
#ifdef O2_NO_DEBUG
#define O2_FREE(x) (*o2_free_ptr)(x)
#else
O2_EXPORT void o2_dbg_free(void *obj, const char *file, int line);
#define O2_FREE(x) o2_dbg_free(x, __FILE__, __LINE__)
#endif
#endif


/** \brief allocate and zero memory (see #O2_MALLOC) */
#ifndef O2_CALLOC
#ifdef O2_NO_DEBUG
void *o2_calloc(size_t n, size_t s);
#define O2_CALLOC(n, s) o2_calloc(n, s)
#define O2_CALLOCNT(n, typ) ((typ *) o2_calloc(n, sizeof(typ)))
#else
O2_EXPORT void *o2_dbg_calloc(size_t n, size_t s, const char *file, int line);
#define O2_CALLOC(n, s) o2_dbg_calloc(n, s, __FILE__, __LINE__)
#endif
#endif

#ifndef O2_CALLOCNT
#define O2_CALLOCNT(n, typ) ((typ *) O2_CALLOC(n, sizeof(typ)))
#endif

#ifndef O2_CALLOCT
#define O2_CALLOCT(typ) O2_CALLOCNT(1, typ)
#endif

// if debugging is on, default is O2MEM_DEBUG
#ifndef O2MEM_DEBUG
#ifdef O2_NO_DEBUG
// set to 0 for no memory debug mode
#define O2MEM_DEBUG 0
#else
// set this to 2 to get verbose memory information
#define O2MEM_DEBUG 2
#endif
#endif

    
// if O2MEM_DEBUG, extra checks are made for memory consistency,
// and you can check any pointer using o2_mem_check(ptr):
#if O2MEM_DEBUG
O2_EXPORT void o2_mem_check(void *ptr);
#else   // make #o2_mem_check a noop
#define o2_mem_check(ptr) 0
#endif


// get actual allocation size
// some memory optimizations are possible if we can get the actual
// allocation size, which is possible using o2_malloc:
//
O2_EXPORT size_t o2_allocation_size(void *obj, size_t minimum);

#ifdef __cplusplus
}
#endif

/** @} */ // end of Basics
