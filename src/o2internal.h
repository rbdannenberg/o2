/* o2internal.h - o2.h + declarations needed for implementation */

/* Roger B. Dannenberg
 * April 2020
 */

/// \cond INTERNAL

#ifndef O2INTERNAL_H
#define O2INTERNAL_H

/* gcc doesn't know _Thread_local from C11 yet */
#ifdef __GNUC__
# define thread_local __thread
#elif __STDC_VERSION__ >= 201112L
# define thread_local _Thread_local
#elif defined(_MSC_VER)
// now thread_local seems to be a keyword in Visual Studio
// # define thread_local __declspec(thread)
#else
# error Cannot define thread_local
#endif

/**
 *  Common head for both Windows and Unix.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <assert.h>

#include "o2.h"

#include "o2obj.h"
#include "debug.h"
#include "vec.h"

// Now, we need o2_ctx before including processes.h, and we need some
// classes before that:

// hash keys are processed in 32-bit chunks so we declare a special
// string type. These are used in messages as well, where strings
// occupy a multiple of 4 bytes.
typedef const char *O2string; // string padded to 4-byte boundary

/* o2_strsize - compute the size of a string including EOS and padding
 * to next word. s can be a C string or O2string.
 */
int o2_strsize(const char *s);

/* copy src to dst, padding with zeros to a 32-bit word boundary. dst
 * must be of size NAME_BUF_LEN or bigger and src will be truncated to
 * NAME_BUF_LEN - 4 == O2_MAX_NODE_NAME_LEN == 1020 if it is longer.
 */
void o2_string_pad(char *dst, const char *src);

/* Copy path to the O2 heap, creating an O2string with zero padding to
 * a 32-bit word boundary.
 */
O2string o2_heapify(const char *path);

// o2_strcpy is like strlcpy but it does not return length.
// precisely, o2_strcpy() copies up to n characters (including EOS) from
// s to d. String s is truncated to a maximum length of n - 1 and terminated
// with a zero EOS byte. (Any remainder of d may or may not be filled with
// zeros, unlike strlcpy, which zero fills.)
#ifdef __APPLE__
#define o2_strcpy(d, s, n) ((void) strlcpy(d, s, n))
#else
void o2_strcpy(char *__restrict dst, const char *__restrict src,
              size_t dstsize);
#endif


#include "o2network.h"
#include "o2node.h"

class Proc_info;
class Bridge_info;

class O2_context {
public:
    // msg_types is used to hold type codes as message args are accumulated
    Vec<char> msg_types;
    // msg_data is used to hold data as message args are accumulated
    Vec<char> msg_data;
    O2arg_ptr *argv; // arg vector extracted by calls to o2_get_next()

    int argc; // length of argv
    bool building_message_lock;

    // O2argv_data is used to create the argv for handlers. It is expanded as
    // needed to handle the largest message and is reused.
    Vec<O2arg_ptr> argv_data;

    // O2arg_data holds parameters that are coerced from message data
    // It is referenced by O2argv_data and expanded as needed.
    Vec<char> arg_data;

    Hash_node full_path_table;
    Hash_node path_tree;

    // support for o2mem:
    char *chunk; // where to allocate bytes when freelist is empty
    size_t chunk_remaining; // how many bytes left in chunk
        
    // one and only one of the following 2 addresses should be NULL:
    Proc_info *proc; ///< the process descriptor for this process

    Bridge_info *binst; ///< the bridge descriptor if this is a
                        /// shared memory process
    O2message_ptr schedule_head;  // shared memory process timed msgs
    O2message_ptr schedule_tail;
    
    // This is a stack of messages we are delivering implemented using
    // the next fields to make a list. If the user receiving a message
    // decides to exit(), we will find un-freed messages here and free
    // them, avoiding a(n apparent) memory leak. When a message is not
    // in a data structure (e.g. pending queue, schedule, network send
    // queue, the message should be on this list).
    O2message_ptr msgs; ///< the message being delivered

    // warning callback for dropped messages
    void (*warning)(const char *warn, O2msg_data_ptr msg);
    
    // when we finish, we remove and free hash table entries, but
    // our hash tables resize themselves as they shrink. This is
    // pointless when we want to empty and free them. finishing
    // disables the downsizing of hash tables.
    bool finishing;

    //------------------ message parsing ------------------
    // these are thread-local so that the main o2 and shared-memory
    // threads can both parse messages using message.cpp
    O2msg_data_ptr mx_msg;    // the message we are extracting from
    const char *mx_types;     // the type codes
    const char *mx_type_next; // the next type code
    const char *mx_data_next; // the next data item in mx_msg
    const char *mx_barrier;   // pointer to end of message
    bool mx_vector_to_vector_pending; // expecting vector element
    // type code, will return a whole vector
    bool mx_array_to_vector_pending;  // expecting vector element
    // type code, will return whole vector from array elements
    int mx_vector_to_array;   // when non-zero, we are extracting
    // vector elements as array elements. The value will be one of "ihfd"
    // depending on the vector element type
    int mx_vector_remaining;  // when mx_vector_to_array is set, this
    // counts how many vector elements remain to be retrieved

    O2_context() {
        argv = NULL;
        argc = 0;
        building_message_lock = false;
        chunk = NULL;
        chunk_remaining = 0;
        proc = NULL;
        binst = NULL;
        msgs = NULL;
        warning = &o2_message_drop_warning;
        finishing = false;

        mx_msg = NULL;
        mx_types = NULL;
        mx_type_next = NULL;
        mx_data_next = NULL;
        mx_barrier = NULL;
        mx_vector_to_vector_pending = false;
        mx_array_to_vector_pending = false;
        mx_vector_to_array = false;
        mx_vector_remaining = 0;
    }

    ~O2_context() {
        O2_DBb(printf("%s ~O2_context@%p\n", o2_debug_prefix, this));
    }

    // deallocate everything that may have been allocated and attached
    // to o2_ctx except chunk and chunk_remaining. At this point, the
    // main process has been shut down, so we do not even have a name,
    // but there could be references in the path_tree to the main (local)
    // process. Therefore an "object oriented" shutdown, where we remove
    // objects from the path_tree and let them execute deconstructors,
    // is going to encounter dangling pointers and crash. Therefore,
    // we use finishing to alter the descontruction behavior to not
    // trace all pointers. When finishing == true, we assume that all
    // objects will be found through the path_tree and deleted, and we
    // when we delete X, we do not have to track down and clean up other
    // pointers to X, because ALL objects are getting deleted now anyway.
    void finish() {
        finishing = true;
        binst = NULL;
        path_tree.finish();
        full_path_table.finish();
        argv_data.finish();
        arg_data.finish();
        msg_types.finish();
        msg_data.finish();
        O2_DBb(printf("%s O2_context::finish@%p\n", o2_debug_prefix, this));
    }

#ifndef O2_NO_DEBUG
    void show_tree() {
        printf("%s -------- PATH TREE --------\n", o2_debug_prefix);
        path_tree.show(2);
    }
#endif
};

/* O2 should not be called from multiple threads. One exception
 * is the shared memory bridged processes call functions designed
 * to run in a high-priority thread
 * (such as an audio callback) that exchanges messages with a full O2
 * process. To avoid this, we put shared storage
 * in an O2_context structure. One structure must be allocated per
 * thread, and we use a thread-local variable o2_ctx to locate
 * the context.
 */
extern thread_local O2_context *o2_ctx;


#include "clock.h"
#include "processes.h"
#include "stun.h"
#include "mqtt.h"
#include "mqttcomm.h"
#include "bridge.h"

void o2_mem_finish(void); // implemented by o2mem.c, called to free
// any memory managed by o2mem module.


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


#define RETURN_IF_ERROR(expr) { O2err err = (expr); \
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

#define ROUNDUP_TO_32BIT(i) ((((size_t) i) + 3) & ~3)


extern O2time o2_local_now;
extern O2time o2_global_now;
extern O2time o2_global_offset; // o2_global_now - o2_local_now
extern int o2_gtsched_started;

#define O2_ARGS_END O2_MARKER_A, O2_MARKER_B
/** Default max send and recieve buffer. */
#define MAX_BUFFER 1024

/** \brief Maximum length of address node names and full path
 */
#define O2_MAX_NODE_NAME_LEN 1020
#define NAME_BUF_LEN ((O2_MAX_NODE_NAME_LEN) + 4)

/* \brief Maximum length of UDP messages in bytes
 */
#define O2_MAX_MSG_SIZE 32768


// shared internal functions
void o2_notify_others(const char *service_name, bool added, const char *tappee,
                      const char *properties, int send_mode);


O2err o2_tap_new(const char *tapper, Proxy_info *proxy,
                 O2string tappee, O2tap_send_mode send_mode);

O2err o2_tap_remove(const char *tapper, Proxy_info *process,
                    O2string tappee);

void o2_init_phase2();

int o2_parse_version(const char *vers, int vers_len);

#endif /* O2INTERNAL_H */
/// \endcond
