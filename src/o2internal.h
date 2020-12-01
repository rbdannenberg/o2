/* o2internal.h - o2.h + declarations needed for implementation */

/* Roger B. Dannenberg
 * April 2020
 */

/// \cond INTERNAL

#ifndef O2INTERNAL_H
#define O2INTERNAL_H

/**
 *  Common head for both Windows and Unix.
 */
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <assert.h>

// hash keys are processed in 32-bit chunks so we declare a special
// string type. These are used in messages as well.
typedef const char *o2string; // string padded to 4-byte boundary

#include "o2.h"
#include "debug.h"
#include "dynarray.h"
#include "hashnode.h"
#include "network.h"
#include "processes.h"
#include "stun.h"
#include "mqtt.h"
#include "mqttcomm.h"
#include "bridge.h"

/*
 typedef struct {
    o2_queue_head incoming; // messages are inserted here
    o2_message_ptr pending;  // messages in correct order are here
} o2_msg_queue, *o2_msg_queue_ptr;
*/

void o2_mem_finish(void); // implemented by o2mem.c, called to free
// any memory managed by o2mem module.

/* gcc doesn't know _Thread_local from C11 yet */
#ifdef __GNUC__
# define thread_local __thread
#elif __STDC_VERSION__ >= 201112L
# define thread_local _Thread_local
#elif defined(_MSC_VER)
# define thread_local __declspec(thread)
#else
# error Cannot define thread_local
#endif

/** Note: No struct literals in MSVC. */
#ifdef _MSC_VER

#ifndef USE_ANSI_C
#define USE_ANSI_C
#endif

#ifndef _CRT_SECURE_NO_WARNINGS
// Preclude warnings for string functions
#define _CRT_SECURE_NO_WARNINGS
#endif

// OS X and Linux call it "snprintf":
// snprintf seems to be defined Visual Studio now,
// Visual Studio 2015 is the first version which defined snprintf,
// and its _MSC_VER is 1900:
#if _MSC_VER < 1900
#define snprintf _snprintf
#endif

#else    // Linux or OS X

#define ioctlsocket ioctl
#define closesocket close

#endif   // _MSC_VER


#define RETURN_IF_ERROR(expr) { o2_err_t err = (expr); \
            if (err != O2_SUCCESS) return err; }

// define IS_BIG_ENDIAN, IS_LITTLE_ENDIAN, and swap64(i),
// swap32(i), and swap16(i)
#if WIN32
// WIN32 requires predefinition of IS_BIG_ENDIAN=1 or IS_BIG_ENDIAN=0
#else
 #ifdef __APPLE__
  // OS X endian.h is in MacOSX10.8.sdk/usr/include/machine/endian.h:
  #include "machine/endian.h" 
  #define LITTLE_ENDIAN __DARWIN_LITTLE_ENDIAN
 #else
  #include <endian.h>
  #define LITTLE_ENDIAN __LITTLE_ENDIAN
  #define BYTE_ORDER __BYTE_ORDER
 #endif
 #define IS_BIG_ENDIAN (BYTE_ORDER != LITTLE_ENDIAN)
#endif
#define IS_LITTLE_ENDIAN (!(IS_BIG_ENDIAN))
#define swap16(i) ((((i) >> 8) & 0xff) | (((i) & 0xff) << 8))
#define swap32(i) ((((i) >> 24) & 0xff) | (((i) & 0xff0000) >> 8) | \
                   (((i) & 0xff00) << 8) | (((i) & 0xff) << 24))
#define swap64(i) ((((uint64_t) swap32(i)) << 32) | swap32((i) >> 32))
#define O2_DEF_TYPE_SIZE 8
#define O2_DEF_DATA_SIZE 8

#define ROUNDUP_TO_32BIT(i) (((i) + 3) & ~3)

#define streql(a, b) (strcmp(a, b) == 0)

// o2strcpy is like strlcpy but it does not return length.
// precisely, o2strcpy() copies up to n characters (including EOS) from
// s to d. String s is truncated to a maximum length of n - 1 and terminated
// with a zero EOS byte. (Any remainder of d may or may not be filled with
// zeros, unlike strlcpy, which zero fills.)
#ifdef __APPLE__
#define o2strcpy(d, s, n) ((void) strlcpy(d, s, n))
#else
void o2strcpy(char * restrict dst, const char * restrict src,
              size_t dstsize);
#endif

extern o2_time o2_local_now;
extern o2_time o2_global_now;
extern o2_time o2_global_offset; // o2_global_now - o2_local_now
extern int o2_gtsched_started;

#define O2_ARGS_END O2_MARKER_A, O2_MARKER_B
/** Default max send and recieve buffer. */
#define MAX_BUFFER 1024

/** \brief Maximum length of address node names
 */
#define O2_MAX_NODE_NAME_LEN 1020
#define NAME_BUF_LEN ((O2_MAX_NODE_NAME_LEN) + 4)

/* \brief Maximum length of UDP messages in bytes
 */
#define O2_MAX_MSG_SIZE 32768


// shared internal functions
void o2_notify_others(const char *service_name, int added,
                      const char *tappee, const char *properties);


o2_err_t o2_tap_new(o2string tappee, proc_info_ptr process,
                      const char *tapper);

o2_err_t o2_tap_remove(o2string tappee, proc_info_ptr process,
                  const char *tapper);


typedef struct {
    // msg_types is used to hold type codes as message args are accumulated
    dyn_array msg_types;
    // msg_data is used to hold data as message args are accumulated
    dyn_array msg_data;
    o2_arg_ptr *argv; // arg vector extracted by calls to o2_get_next()

    int argc; // length of argv

    // o2_argv_data is used to create the argv for handlers. It is expanded as
    // needed to handle the largest message and is reused.
    dyn_array argv_data;

    // o2_arg_data holds parameters that are coerced from message data
    // It is referenced by o2_argv_data and expanded as needed.
    dyn_array arg_data;

    hash_node full_path_table;
    hash_node path_tree;

    // support for o2mem:
    char *chunk; // where to allocate bytes when freelist is empty
    int64_t chunk_remaining; // how many bytes left in chunk
        
    // one and only one of the following 2 addresses should be NULL:
    proc_info_ptr proc; ///< the process descriptor for this process

    bridge_inst_ptr binst; ///< the bridge descriptor for this 
                           /// shared memory process

    // This is a stack of messages we are delivering implemented using
    // the next fields to make a list. If the user receiving a message
    // decides to exit(), we will find un-freed messages here and free
    // them, avoiding a(n apparent) memory leak. When a message is not
    // in a data structure (e.g. pending queue, schedule, network send
    // queue, the message should be on this list).
    o2_message_ptr msgs; ///< the message being delivered

    // warning callback for dropped messages
    void (*warning)(const char *warn, o2_msg_data_ptr msg);

} o2_ctx_t, *o2_ctx_ptr;


void o2_ctx_init(o2_ctx_ptr context);

void o2_init_phase2();


/* O2 should not be called from multiple threads. One exception
 * is the shared memory bridged processes call functions designed
 * to run in a high-priority thread
 * (such as an audio callback) that exchanges messages with a full O2
 * process. There is a small problem that O2 message construction and
 * decoding functions use some static, preallocated storage, so sharing
 * across threads is not allowed. To avoid this, we put shared storage
 * in an o2_ctx_t structure. One structure must be allocated per
 * thread, and we use a thread-local variable o2_ctx to locate
 * the context.
 */
extern thread_local o2_ctx_ptr o2_ctx;

#endif /* O2INTERNAL_H */
/// \endcond
