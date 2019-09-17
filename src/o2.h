// o2.h -- public header file for o2 system
// Roger B. Dannenberg and Zhang Chi
// see license.txt for license
// June 2016

#ifndef O2_H
#define O2_H

#ifdef __cplusplus
extern "C" {
#endif

/** \file o2.h
\mainpage
\section Introduction

This documentation is divided into modules. Each module describes a
different area of functionality: Basics, Return Codes, Low-Level
Message Send, and Low-Level Message Parsing.

 \section Notes on Building the O2 Library

 O2 uses CMake, which can create a Linux Makefile, Xcode project, or
 Visual Studio solution. The basic procedure is run CMake (ccmake on
 Linux) to create the Makefile of project file, then build using make
 or your IDE. The main options for CMake are BUILD_MIDI_EXAMPLE:
 builds an example that depends on PortMidi (disable this option
 unless you need it), BUILD_STATIC_LIB: builds a static O2 library
 (recommended), BUILD_TESTS: builds a suite of tests (useful for
 O2 developers), and BUILD_TESTS_WITH_LIBLO: includes some tests
 that depends on the OSC library liblo (also useful for O2
 developers).

 To use O2 in your application, normally you should only need
 to include the O2 static library `o2_static.lib` into your project,
 make sure the path to o2.h is on your headers search path,
 and maybe put the path to `o2_static.lib` on your libraries
 search path.

 O2 depends on some libraries. On Windows, if your application
 includes O2, you will need to link to `winmm.lib` and
 `ws2_32.lib`. On OS X, you will need to link to the `CoreAudio`
 framework. On Linux, you will need to link to `asound` (you may
 have to install a developer package to get this), `pthread`, and
 `m` (the math library).

\section Overview

O2 is a communication protocol for interactive music and media
applications. O2 is inspired by Open Sound Control (OSC) and uses
similar means to form addresses, specify types, and encode messages.

However, in addition to providing message delivery, O2 offers a
discovery mechanism where processes automatically discover and connect
to other processes. Each process can offer zero or more named
"services," which are top-level nodes in a global, tree-structured
address space for a distributed O2 application. In O2, services
replace the notion of network addresses (e.g. 128.2.100.57) in OSC.

O2 is based on IP (Internet Protocol), but there are some mechanisms
that allow an O2 process to serve as a bridge to other networks such
as Bluetooth.

O2 addresses begin with the service name. Thus, a complete O2 address
would be written simply as "/synth/filter/cutoff," where "synth" is
the service name.

Furthermore, O2 implements a clock synchronization protocol. A single
process is designated as the "master," and other processes
automatically synchronize their local clocks to the master. All O2
messages are timestamped. Messages are delivered immediately, but
their designated operations are invoked according to the timestamp. A
timestamp of zero (0.0) means deliver the message
immediately. Messages with non-zero timestamps are only deliverable
after both the sender and receiver have synchronized clocks.

A service is created using the functions: 

    o2_service_new("service_name")

and

    o2_method_new("address," "types," handler, user_data, coerce, parse),

where o2_method_new is called to install a handler for each node, and each
"address" includes the service name as the first node.

Some major components and concepts of O2 are the following:

- **Ensemble** - a collection of collaborating processes are called
  an "ensemble." Ensembles are named by a simple ASCII string.
  In O2, all components belong to an ensemble, and O2 supports
  communication only *within* an ensemble. This allows multiple
  independent ensembles to co-exist and share the same local area
  network.

- **Host** - (conventional definition) a host is a computer (virtual
  or real) that may host multiple processes. Essentially, a Host is
  equivalent to an IP address.

- **Process** - (conventional definition) an address space and one or
  more threads. O2 assumes that all processing is performed by a single
  thread (O2 is not reentrant). A process can offer one or more O2 
  services and can serve as a client of O2 services in the same or other 
  processes. Each process using O2 has one directory of services shared
  by all O2 activity in that process (thus, this O2 implementation can 
  be thought of as a "singleton" object). The single O2 instance in a
  process belongs to one and only one ensemble. O2 does not support
  communication between ensembles. It is the responsibility of an O2 
  process to call #o2_poll() frequently, giving O2 the opportunity to 
  receive and process messages as well as run background activities for
  discovery and clock synchronization. #o2_poll() is non-blocking.

- **Service** - an O2 service is a named server that receives and acts
  upon O2 messages. A service is addressed by name. Multiple services
  can exist within one process. A service does not imply a (new) thread
  and all O2 messages are delivered sequentially from the single thread
  that calls #o2_poll(). Service names begin with a letter with the
  exception of "_o2" which denotes the local process, "_cs", which denotes
  the master clock, and `ip:port` strings, beginning with a digit, that
  denote a remote process.

- **Message** - an O2 message, similar to an OSC message, contains an 
  address pattern representing a function, a type string and a set of 
  values representing parameters. Messages are delivered to and handled
  be *services*. If there are multiple services with the same name, the
  service with the highest IP address and port number (lexicographically)
  gets the message. However, the *tap* mechanism can be used to achieve
  fan out from a single service (the *tappee*) to multiple services 
  (*tappers*).

- **Address Pattern** - O2 messages use URL-like addresses, as does OSC,
  but the top-level node in the hierarchical address space is the
  service name. Thus, to send a message to the "note" node of the
  "synth" service, the address pattern might be "/synth/note." The
  OSC pattern specification language is used unless the first character
  is "!", e.g. "!synth/note" denotes the same address as "/synth/note"
  except that O2 can assume that there are no pattern characters such
  as "*" or "[".

- **Scheduler** - O2 implements two schedulers for timed message
    delivery. Schedulers are served by the same o2_poll() call that
    runs other O2 activity. The #o2_gtsched schedules according to the
    ensemble's master clock time, but since this depends on clock
    synchronization, nothing can be scheduled until clock
    synchronization is achieved (typically within a few seconds of
    starting the process that provides the master clock). For
    local-only services, it is possible to use the #o2_ltsched
    scheduler (not with o2_send(), but by explicitly constructing
    messages and scheduling them with o2_schedule(). In any case,
    scheduling is useful *within* a service for any kind of timed
    activity.

\section Standard Messages

O2 uses O2 messages for some internal functions and also to communicate
status information with O2 processes. Since messages are dropped if there
is no handler, O2 processes need not set up handlers for all information.

In the following descriptions, if the *service name* is `ip:port` it means
the IP address and port number are used to construct a string, e.g. 
"192.200.145.8:40521" might be the actual service name.


\subsection Internal Messages

`/_o2/dy "ssiii"` *ensemble_name* *local_ip* *tcp_port* *udp_port* *dy* -
this message is normally sent to the discovery port, but it can also be sent
as a result of calling o2_hub() and providing an O2 process address.
Processes must exchange discovery messages to be connected. The *dy* 
parameter, if true, requests that the receiver reply with a discovery message.

`/_o2/hub ""` - requests the receiver to become the hub for the sender

`/_o2/cs/cs ""` - announces when clock sync is obtained.

`/_o2/ds ""` - this message invokes the sending of discovery messages. It
is used with a timestamp to schedule periodic discovery message sending.

`/_cs/get "is" *serial-no* *reply-to* - send the time. The *reply-to*
parameter is the reply prefix to which "/get-reply" is appended to create
the full address for the reply. The reply contains the type string "it"
and the parameters are the *serial-no* and the current time.

`/_o2/sv "ssbbs..."` *process-name* *service-name* *exists-flag*
*tappee-flag* *tappee-or-properties* ... - reports service creation 
or deletion. Each service is described by name, TRUE, and either TRUE
followed by a properties string or FALSE followed by the tappee 
name. If a service is deleted, then FALSE is sent rather than TRUE,
and if this is not a tap, the properties string is empty. The "..." 
notation here indicates that there can be any number of services 
described in this message, each service consisting of another "sbbs"
(string, Boolean, Boolean, string) sequence. Properties strings are
sent with escaped values and trailing ';' but no leading ';'. This is
the first message sent when a process-to-process connection is made.


\subsection API Messages

`/_o2/cs/rt "s"` *reply-to* - A process can send this message to request
round trip (to the clock master) information; *reply-to* is an address prefix.
A reply is sent to *reply-to* concatenated with "/get-reply". The reply message
has the type string "sff" and the parameters are the process name (ip:port), 
the mean round-trip time, and the minimum round-trip time.

`/_o2/ps ""` - this message invokes the sending of the `/_cs/get` message 
to request the time as part of the clock synchronization protocol.

`/_o2/si "sis"` *service_name* *status* *process-name* - Whenever an
active service status changes, this message is sent to the local process.
Note that when a local service is created, an *internal* `/sv` message is
sent to all other processes, and when that process's services table is
updated, if theservice is or becomes active, the change is reported
locally to the application by sending this `/_o2/si` message. Normally,
you should not add handlers or use the `_o2` service, but in this case,
an application is expected to add a custom handler to receive status
updates. See `o2_status()` for a list of status values. Note also that
the service `_o2` is created before you can install a handler for
`/_o2/si`, so you will not receive an `si` messages when `_o2` is
created. If the service name begins with a digit, it represents a
remote process and the name has the form IP:PORT, e.g.
`128.237.161.165:50404`. There is also an `IP:PORT` service representing
the local process, but the local process is also represented by `_o2`,
so the local `IP:PORT` service is never reported in an `/_o2/si` message.
You can get the local `IP:PORT` string by calling `o2_get_address()`.

*/

// get uint32_t, etc.:
#include <stdlib.h>
#include <stdint.h>

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

/** \defgroup debugging Debugging Support
 * @{
 */

/// \brief Enable debugging output.
///
/// Unless O2_NO_DEBUG is defined at compile time, O2 is
/// compiled with debugging code that prints information to
/// stdout, including network addresses, services discovered,
/// and clock synchronization status. Enable the debugging
/// information by calling o2_debug_flags() with a string
/// containing any of the following characters:
///   - c - for basic connection data
///   - r - for tracing non-system incoming messages
///   - s - for tracing non-system outgoing messages
///   - R - for tracing system incoming messages
///   - S - for tracing system outgoing messages
///   - k - for tracing clock synchronization protocol
///   - d - for tracing discovery messages
///   - h - for tracing hub-related activity
///   - t - for tracing user messages dispatched from schedulers
///   - T - for tracing system messages dispatched from schedulers
///   - m - trace O2_MALLOC and O2_FREE calls
///   - o - trace socket creating and closing
///   - O - open sound control messages
///   - g - print general status info
///   - n - all network flags (no malloc or scheduling)
///   - a - all debug flags except m (malloc/free)
///   - A - all debug flags except malloc and scheduling
#ifndef O2_NO_DEBUG
void o2_debug_flags(const char *flags);
#endif

/** @} */

/** \defgroup returncodes Return Codes
 * @{
 */

// Status return values used in o2 functions
#define O2_SUCCESS 0    ///< function was successful

/// \brief an error return value: a non-specific error occurred.
///
/// In general, any return value < 0 indicates an error. Testing for
/// only O2_FAIL will not detect more specific error return values
/// such as O2_SERVICE_CONFLICT, O2_NO_MEMORY, etc.
#define O2_FAIL (-1)

/// an error return value: path to handler specifies a remote service
#define O2_SERVICE_CONFLICT (-2)

/// an error return value: path to handler specifies non-existant service
#define O2_NO_SERVICE (-3)

/// an error return value: process is out of free memory
#define O2_NO_MEMORY (-4)

/// an error return value for o2_initialize(): O2 is already running.
#define O2_ALREADY_RUNNING (-5)

/// an error return value for #o2_initialize() or #o2_service_new():
/// invalid name parameter.
#define O2_BAD_NAME (-6)

/// an error return value for o2_add_vector(): invalid element type
#define O2_BAD_TYPE (-7)

/// \brief an error return value: mismatched types and arguments
/// returned by o2_message_build(), o2_send(), o2_send_cmd()
#define O2_BAD_ARGS (-8)

/// an error return value for o2_initialize(): the socket is closed.
#define O2_TCP_HUP (-9)

/// \brief an error return value indicating inet_pton() failed to convert a
/// string to an IP address
#define O2_HOSTNAME_TO_NETADDR_FAIL (-10)

/// an error return value: attempt to make a TCP connection failed
#define O2_TCP_CONNECT_FAIL (-11)

/// \brief an error return value: message was not scheduled or delivered
/// because the current time is not available
#define O2_NO_CLOCK (-12)

/// an error return value: no handler for an address
#define O2_NO_HANDLER (-13)

/// an error return value: an O2 message is invalid
#define O2_INVALID_MSG (-14)

/// an error return value: could not write to socket or send datagram
#define O2_SEND_FAIL (-15)

/// an error return value: a service name was NULL or contained a slash (/)
#define O2_BAD_SERVICE_NAME (-16)

/// an error return value: attempt to create a local service when one exists already
#define O2_SERVICE_EXISTS (-17)

/// an error return value: O2 has not been initialized
#define O2_NOT_INITIALIZED (-18)

/// TCP send would block, holding message locally to send later
#define O2_BLOCKED (-19)


// Status return codes for o2_status function:

/// \brief return value for o2_status(): local service, no clock sync yet
///
/// This is a local service
/// but clock sync has not yet been established so messages with non-zero
/// timestamps will be dropped.
#define O2_LOCAL_NOTIME 0

/// \brief return value for o2_status(): remote service but no clock sync yet
///
/// This is a remote service but clock sync has not yet been established so
/// messages with non-zero timestamps will be dropped. The remote service
/// may represent a bridge to a non-IP destination or to an OSC
/// server.
#define O2_REMOTE_NOTIME 1

/// \brief return value for o2_status(): service is connected but no
///    clock sync yet.
///
/// The service is attached to this process by a non-IP link. Clock sync
/// has not yet been established between the master clock and this
/// process, so non-zero timestamped messages to this service will be
/// dropped. Note that within other processes,
/// the status for this service will be #O2_REMOTE_NOTIME rather than
/// #O2_BRIDGE_NOTIME. Note also that O2 does not require the
/// remote bridged process to have a synchronized clock, so "NOTIME" only
/// means that *this* process is not synchronized and therefore cannot
/// (and will not) schedule a timestamped message for timed delivery.
#define O2_BRIDGE_NOTIME 2

/// \brief return value for o2_status(): service is connected but no
///    clock sync yet.
///
/// The service is local and forwards messages to an OSC server. The status
/// of the OSC server is not reported by O2 (and in the typical UDP case,
/// there is no way to determine if the OSC server is operational, so
/// "connected" may just mean that the service has been defined).
/// Clock sync has not yet been established between the master clock
/// and this process, so messages with non-zero timestamps to this service
/// will be dropped. Note that within other processes,
/// the status for this service will be #O2_REMOTE_NOTIME rather than
/// #O2_TO_OSC_NOTIME. Note also that O2 does not require the
/// OSC server to have a synchronized clock, so "NOTIME" only
/// means that *this* process is not synchronized to O2 and therefore cannot
/// (and will not) schedule a timestamped message for timed delivery.
#define O2_TO_OSC_NOTIME 3

/// \brief return value for o2_status(): local service with clock sync.
///
/// Note that even though the
/// service is local to the process and therefore shares a local
/// clock, clocks are not considered to be synchronized until the
/// local clock is synchronized to the master clock. If this process
/// provides the master clock, it is considered to be synchronized
/// immediately.
#define O2_LOCAL 4

/// \brief return value for o2_status(): remote service with clock sync.
///
/// Messages with non-zero timestamps can be sent because
/// clock sync has been established.
#define O2_REMOTE 5

/// \brief return value for o2_status(): connected with clock sync.
///
/// The service is attached by a non-IP link, and this process is synchronized.
/// If the bridged process is also synchronized, timed messages are
/// sent immediately and dispatched according to the synchronized
/// clock; if the bridged process is *not* synchronized, timed
/// messages are scheduled locally and sent according to the
/// timestamp, resulting in some added network latency.
#define O2_BRIDGE 6

/// \brief return value for o2_status(): connected with clock sync.
///
/// The service forwards messages directly from the current process
/// to an OSC server, and the process is synchronized. The status of
/// the OSC server is not reported by O2 (and in the typical UDP case,
/// there is no way to determine if the OSC server is operational).
/// Non-bundle O2 messages will be scheduled locally and sent according
/// to the timestamp to avoid creating a timestamped bundle, but this
/// will result in some added network latency. O2 bundles will be
/// converted to OSC bundles with timestamps based on Unix gettimeofday()
/// or Windows GetSystemTimeAsFileTime() which are then converted to
/// OSC-compatible NTP timestamps (this is all based on liblo; timestamped
/// message to liblo implementations of OSC will be correctly interpreted).
/// The resulting OSC bundles are sent immediately.
#define O2_TO_OSC 7

/// \brief tag value for #o2_services_list()
///
#define O2_TAP 8
    

/** @} */


// Macros for o2 protocol
/* an internal value, ignored in transmission but check against O2_MARKER in the
 * argument list. Used to do primitive bounds checking */
#define O2_MARKER_A (void *) 0xdeadbeefdeadbeefL
#define O2_MARKER_B (void *) 0xf00baa23f00baa23L
//#endif

extern void *((*o2_malloc)(size_t size));
extern void ((*o2_free)(void *));
void *o2_calloc(size_t n, size_t s);

/** \defgroup basics Basics
 * @{
 */

#ifndef O2_NO_DEBUG
void *o2_dbg_malloc(size_t size, const char *file, int line);
void o2_dbg_free(const void *obj, const char *file, int line);
#define O2_MALLOC(x) o2_dbg_malloc(x, __FILE__, __LINE__)
#define O2_FREE(x) o2_dbg_free(x, __FILE__, __LINE__)
#endif

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
#define O2_MALLOC(x) (*o2_malloc)(x)
#else
#define O2_MALLOC(x) o2_dbg_malloc(x, __FILE__, __LINE__)
#endif
#endif

/** \brief free memory allocated by #O2_MALLOC */
#ifndef O2_FREE
#ifdef NO_O2_DEBUG
#define O2_FREE(x) (*o2_free)(x)
#else
#define O2_FREE(x) (*o2_dbg_free)(x, __FILE__, __LINE__)
#endif
#endif

/** \brief allocate and zero memory (see #O2_MALLOC) */
#ifndef O2_CALLOC
#ifdef NO_O2_DEBUG
void *o2_calloc(size_t n, size_t s);
#define O2_CALLOC(n, s) o2_calloc((n), (s))
#else
void *o2_dbg_calloc(size_t n, size_t s, const char *file, int line);
#define O2_CALLOC(n, s) o2_dbg_calloc((n), (s), __FILE__, __LINE__)
#endif
#endif

/** \brief O2 timestamps are doubles representing seconds since the
 * approximate start time of the ensemble.
 */
typedef double o2_time;

/** \brief data part of an O2 message
 *
 * This data type is used to pass o2 message data to message handlers.
 * It appears many other times in the code. You should NEVER allocate
 * or free an o2_msg_data struct. Instead, create a message using 
 * o2_send_start(), o2_add_*(), and o2_message_finish() to get an 
 * o2_message_ptr. Within the o2_message, the data field is an
 * o2_msg_data structure. We would use o2_message everywhere instead
 * of o2_msg_data, but bundles can contain multiple o2_msg_data 
 * structures without the extra baggage contained in an o2_message.
 *
 * Note: it is assumed that an o2_msg_data struct is always preceded
 * by a 32-bit length. Ideally, length should therefore be in this
 * struct, but then the compiler might add padding to put the timestamp
 * on an 8-byte alignment. This could be solved with a pack pragma, but
 * that is not standard C. To be safe and portable, I decided to just 
 * leave length out of the struct. The macro MSG_DATA_LENGTH can be used
 * to access the length field.
 */
typedef struct o2_msg_data {
    o2_time timestamp;   ///< the message delivery time (0 for immediate)
    /** \brief the message address string
     *
     * Although this field is declared as 4 bytes, actual messages
     * have variable length, and the address is followed by a
     * string of type codes and the actual parameters. The length
     * of the entire message including the timestamp is given by
     * the `length` field.
     */
    char address[4];
} o2_msg_data, *o2_msg_data_ptr;

// get the length from a pointer to an o2_msg_data. This macro dereferences
// the o2_msg_data pointer to impose some mild typechecking. Not just any
// pointer will work.
#define MSG_DATA_LENGTH(m) (((int32_t *) &((m)->timestamp))[-1])


/** \brief get the type string from o2_msg_data_ptr
 *
 * Type strings begin with the comma (",") character, which is skipped
 */
#define WORD_ALIGN_PTR(p) ((char *) (((size_t) (p)) & ~3))
#define O2_MSG_TYPES(msg) \
    WORD_ALIGN_PTR((msg)->address + strlen((msg)->address) + 4) + 1;


/** \brief an O2 message container
 *
 * Note: This struct represents an O2 message that is stored on the heap.
 * The length field must preceded data with no padding (see o2_msg_data
 * declaration and the note that precedes it). To make sure there is no
 * padding between length and data, we force the next pointer to occupy
 * 8 bytes even if this is a 32-bit machine by making it part of a union
 * with an 8-byte int64_t field named "pad_if_needed."
 *
 * Note that o2_messages are on the heap and can be allocated, scheduled,
 * sent, and freed.  In contrast, o2_msg_data structures are contained 
 * within o2_messages and are passed to method handlers, but cannot be
 * allocated, scheduled, sent, or freed. They are always the data field
 * of a containing o2_message.
 */
typedef struct o2_message {
    union {
        struct o2_message *next; ///< links used for free list and scheduler
        int64_t pad_if_needed;   ///< make sure allocated is 8-byte aligned
    };
    union {
        int tcp_flag;            ///< send message by tcp?
        int64_t pad_if_needed2;  ///< make sure allocated is 8-byte aligned 
    };
    int32_t allocated;       ///< how many bytes allocated in data part
    int32_t length;          ///< the length of the message in data part
    o2_msg_data data;
} o2_message, *o2_message_ptr;


/**
 *  \brief The structure for binary large object.
 *
 *  A blob can be passed in an O2 message using the 'b' type. Created
 *  by calls to o2_blob_new().
 */
typedef struct o2_blob {
  uint32_t size;  ///< size of data
  char data[4];   ///< the data, actually of variable length
} o2_blob, *o2_blob_ptr;


/**
 *  \brief An enumeration of the O2 message types.
 */
typedef enum {
    // basic O2 types
    O2_INT32 =     'i',     ///< 32 bit signed integer.
    O2_FLOAT =     'f',     ///< 32 bit IEEE-754 float.
    O2_STRING =    's',     ///< NULL terminated string (Standard C).
    O2_BLOB =      'b',     ///< Binary Large OBject (BLOB) type.
    O2_ARRAY_START = '[',   ///< Start array or tuple
    O2_ARRAY_END = ']',     ///< End array or tuple
    
    // extended O2 types
    O2_INT64 =     'h',     ///< 64 bit signed integer.
    O2_TIME  =     't',     ///< OSC time type.
    O2_DOUBLE =    'd',     ///< 64 bit IEEE-754 double.
    O2_SYMBOL =    'S',     ///< Used in systems distinguish strings and symbols.
    O2_CHAR =      'c',     ///< 8bit char variable (Standard C).
    O2_MIDI =      'm',     ///< 4 byte MIDI packet.
    O2_TRUE =      'T',     ///< Symbol representing the value True.
    O2_FALSE =     'F',     ///< Symbol representing the value False.
    O2_NIL =       'N',     ///< Symbol representing the value Nil.
    O2_INFINITUM = 'I',     ///< Symbol representing the value Infinitum.
    
    // O2 types
    O2_BOOL =      'B',     ///< Boolean value returned as either 0 or 1
    O2_VECTOR =    'v',     ///< Prefix to indicate a vector
} o2_type, *o2_type_ptr;


/**
 * \brief union of all O2 parameter types
 *
 * An o2_arg_ptr is a pointer to an O2 message argument. If argument
 * parsing is requested (by setting the parse parameter in o2_method_new),
 * then the handler receives an array of o2_arg_ptrs. If argument parsing
 * is not requested, you have the option of parsing the message one
 * parameter at a time by calling o2_get_next(), which returns an
 * o2_arg_ptr.
 *
 * The o2_arg_ptr can then be dereferenced to obtain a value of the
 * expected type. For example, you could write
 * \code{.c}
 *     double d = o2_get_next()->d;
 * \endcode
 * to extract a parameter of type double. (This assumes that the message
 * is properly formed and the type string indicates that this parameter is
 * a double, or that type coercion was enabled by the coerce flag in
 * o2_method_new().)
 */
typedef union {
    int32_t    i32;  ///< 32 bit signed integer.
    int32_t    i;    ///< an alias for i32
    int64_t    i64;  ///< 64 bit signed integer.
    int64_t    h;    ///< an alias for i64
    float      f;    ///< 32 bit IEEE-754 float.
    float      f32;  ///< an alias for f
    double     d;    ///< 64 bit IEEE-754 double.
    double     f64;  ///< an alias for d
    char       s[4]; ///< Standard C, NULL terminated string.
    /** \brief Standard C, NULL terminated, string.
        Used in systems which distinguish strings and symbols. */
    char       S[4];
    int        c;    ///< Standard C, 8 bit, char, stored as int.
    uint32_t   m;    ///< A 4 byte MIDI packet. MSB to LSB are port id,
                     ///< status, data1, data2
    o2_time    t;    ///< TimeTag value.
    o2_blob    b;    ///< a blob (unstructured bytes)
    int32_t    B;    ///< a boolean value, either 0 or 1
    struct {
        int32_t len; ///< length of vector in bytes
        ///< IMPORTANT: divide by 4 or 8 to get length in elements
        int32_t typ; ///< type of vector elements
        union {
            int32_t    *vi;  ///< vector of 32-bit signed integers
            int64_t    *vh;  ///< vector of 64-bit signed integers
            double     *vd;  ///< vector of IEEE-754 doubles
            float      *vf;  ///< vector of IEEE-754 floats
            // note that a blob is basically a vector of bytes;
            // there is no type conversion from blob to vector though,
            // and no vector of shorts or bytes because if you converted
            // to a vector of int64_t, it would take 8x the message
            // space, forcing us to allocate very big buffers to
            // unpack messages.
        };
    } v;
} o2_arg, *o2_arg_ptr;


extern o2_arg_ptr o2_got_start_array;
extern o2_arg_ptr o2_got_end_array;


/** \brief set this flag to stop o2_run()
 *
 * Some O2 processes will initialize and call o2_run(), which is a
 * simple loop that calls o2_poll(). To exit the loop, set
 * #o2_stop_flag to #TRUE
 */
extern int o2_stop_flag;

/*
 * A collection of cooperating O2 processes forms an
 * *ensemble*. Ensembles must have unique names. This allows
 * more than one ensemble to exist within a single network without
 * conflict. For example, there could be two ensembles, "joe" and
 * "sue", each with services named "synth." Since the ensemble
 * names are different, joe's messages to the synth service go to
 * joe's synth and not to sue's synth.
 *
 * Do not set, modify or free this variable! Consider it to be
 * read-only. It is managed by O2 using o2_initialize() and o2_finish().
 */
extern const char *o2_ensemble_name; // also used to detect initialization



/**
 *  \brief callback function to receive an O2 message
 *
 * @param msg The full message in host byte order.
 * @param types If you set a type string in your method creation call,
 *              then this type string is provided here. If you did not
 *              specify a string, types will be the type string from the
 *              message (without the initial ','). If parse_args and
 *              coerce_flag were set in the method creation call,
 *              types will match the types in argv, but not necessarily
 *              the type string or types in msg.
 * @param argv An array of #o2_arg types containing the values, e.g. if the
 *             first argument of the incoming message is of type 'f' then
 *             the value will be found in argv[0]->f. (If parse_args was
 *             not set in the method creation call, argv will be NULL.)
 *             For vectors, specified in types by the sequence "vi", "vh", 
 *             "vf", or "vd", there will be one pointer in argv pointing to
 *             a vector description (the v field in o2_arg). For arrays,
 *             there are *no* pointers corresponding to '[' or ']' in the
 *             types string; but there is one pointer in argv for each array
 *             element.
 * @param argc The number of arguments received. (This is valid even if
 *             parse_args was not set in the method creation call.) This is
 *             the length of argv. Vectors count as one, array elements count
 *             as one each, and arrays themselves are not represented. For
 *             example, an empty array ("[]") in the type string adds
 *             nothing to the argc count or argv vector.
 * @param user_data This contains the user_data value passed in the call
 *             to the method creation call.
 */
typedef void (*o2_method_handler)(const o2_msg_data_ptr msg, const char *types,
                                  o2_arg_ptr *argv, int argc, void *user_data);


/**
 *  \brief Start O2.
 *
 *  If O2 has not been initialized, it is created and intialized.
 *  O2 will begin to establish connections to other instances
 *  with a matching ensemble name.
 *
 *  @param ensemble_name the name of the ensemble. O2 will attempt to
 *  discover other processes with a matching ensemble name,
 *  ignoring all processes with non-matching names.
 *
 *  @return #O2_SUCCESS if success, #O2_FAIL if an error occurs,
 *  #O2_RUNNING if already running, #O2_BAD_NAME if `ensemble_name`
 *  is NULL.
 */
int o2_initialize(const char *ensemble_name);


/**
 * \brief Tell O2 how to allocate/free memory.
 *
 * In many C library implementations, the standard implementation of
 * free() must lock a data structure. This can lead to priority
 * inversion if O2 runs at an elevated priority. Furthermore, the
 * standard `malloc()` and `free()` do not run in constant (real) time. To
 * avoid these problems, you can provide an alternate heap
 * implementation for O2 by calling this function before calling
 * o2_initialize(). For example, the provided functions can implement
 * a private heap for the thread running O2.
 *
 * @param malloc a function pointer that behaves like standard
 *     `malloc()`
 * @param free a function pointer that behaves like standard `free()`
 *
 * @return O2_SUCCESS if succeed, O2_FAIL if not.
 */
int o2_memory(void *((*malloc)(size_t size)), void ((*free)(void *)));


/**
 * \brief Set discovery period
 *
 * O2 discovery messages are broadcast periodically in case a new process
 * has joined the ensemble. The default period is 4 seconds. If there
 * are N processes, each host will receive N/4 discovery messages per 
 * second. Since there are 5 discovery ports, each process will handle
 * N/20 discovery messages per second, and a discovery message from any
 * given process will be received every 20 seconds. (Note, however, that
 * new processes send more frequently, sending 2 discovery messages to 
 * each of the 5 discovery port numbers within 2 seconds, so if messages
 * are not dropped frequently, discovery of new processes will happen much
 * faster than the worst-case 20 second polling period or even the 
 * 10 second expected wait.)
 *
 * You can change the polling period from 4s by calling this function. The
 * new polling period takes effect when the next discovery message is sent
 * at the end of the current polling period.
 *
 * @param period the requested polling period; a minimum of 0.1s is enforced; 
 *               4s is the default (recommended).
 *
 * @return the previous polling period
 */
o2_time o2_set_discovery_period(o2_time period);


/**
 * \brief Connect to a hub.
 *
 * A "hub" is an O2 process that shares discovery information with other
 * processes. This is an alternate form of discovery that is completely
 * compatible with the broadcast-based discovery protocol, except
 * (1) you do not need broadcast messages to communicate with a hub, 
 * (2) you *do* need the hub's IP address and port number. If the IP 
 * and port number can be shared, e.g. through a server or online 
 * database with a fixed address, you can work with networks that
 * disallow broadcast, and you can connect across networks (which will
 * not work with O2's normal discovery protocol if broadcast messages
 * are not delivered across networks). To use a hub, you call o2_hub()
 * with the hub's IP address and port. All O2 processes are effectively
 * hubs with no clients, and o2_hub() simply connects to the hub as
 * a client. The hub will then send discovery messages for all current
 * and future O2 processes that are discovered, either through the 
 * normal discovery protocol or by connecting with the o2_hub() call.
 *
 * After o2_hub() is called, discovery broadcasting is stopped, so if
 * o2_hub() fails to connect to another process, you will only discover
 * more processes if they initiate the exchange. You can use o2_hub()
 * specifically to disable broadcast-based discovery by passing NULL
 * as the ipaddress parameter. 
 *
 * You can call o2_hub() multiple times. Each time potentially makes
 * a remote process become a hub for this local process. This might
 * result in duplicate messages when new processes join the O2 
 * ensemble, but duplicate messages are ignored.
 * 
 * @param ipaddress the IP address of the hub or NULL
 * @param port the port number of the hub's TCP port
 *
 * @return #O2_SUCCESS if success, #O2_FAIL if not.
 */
int o2_hub(const char *ipaddress, int port);


/**
 * \brief Get IP address and TCP connection port number.
 *
 * Before calling o2_hub(), you need to know the IP address and 
 * TCP connection port of another process. This call will retrieve
 * the information, but the mechanism to transfer this information
 * to another O2 process (or all of them) must be implemented outside
 * of O2. (If the local network allows UDP broadcast and all hosts
 * are on the local network, then you do not need this function or
 * o2_hub(). Instead, let the discovery protocol exchange process
 * addresses automatically.)
 * 
 * @param ipaddress is a pointer that will be set to either NULL 
 * (on failure) or a string of the form "128.2.10.6". The string
 * should not be modified, and the string will be freed by O2 if
 * o2_finish() is called.
 *
 * @param port will be set to a pointer to the O2 TCP connection
 * port (or NULL on failure).
 *
 * @return #O2_SUCCESS if success, #O2_FAIL if not.
 */
int o2_get_address(const char **ipaddress, int *port);


/**
 *  \brief Add a service to the current process.
 *
 * Once created, services are "advertised" to other processes with
 * matching ensemble names, and messages are delivered
 * accordingly. E.g. to handle messages addressed to "/synth/volume"
 * you call
 * \code{.c}
 * o2_service_new("synth");
 * o2_method_new("/synth/volume", "f", synth_volume_handler, NULL, NULL, TRUE);
 * \endcode
 * and define `synth_volume_handler` (see the type declaration for
 * #o2_method_handler and o2_method_new())
 * User-created service names must begin with a letter.
 * Normally, services should be *unique* across the ensemble. If 
 * #service_name is already locally defined in this process (by a previous
 * call to #o2_service_new or #o2_osc_delegate), this call will fail,
 * returning #O2_SERVICE_EXISTS. If matching service names are defined
 * in two different processes, the process with the highest IP and port
 * number (lexicographically) will provide the service. However, due to
 * the distributed and asynchronous nature of O2, there may be some 
 * intervening time (typically a fraction of a second) during which a
 * service is handled by two different processes. Furthermore, the switch
 * to a new service provider could redirect a stream of messages, causing
 * unexpected behavior in the ensemble.
 *
 *  @param service_name the name of the service
 *
 *  @return #O2_SUCCESS if success, #O2_FAIL if not.
 */
int o2_service_new(const char *service_name);


/**
 * \brief list known services and taps
 *
 * Currently active services and taps can be queried by calling
 * #o2_services_list(). An internal snapshot of services and
 * taps is saved. Information can then be accessed by calling
 * #o2_service_name(), #o2_service_type(), #o2_service_process(), 
 * #o2_service_tapper(), and #o2_service_properties(). When the 
 * information is no longer needed, call #o2_services_list_free().
 * 
 * Only active services and their tappers are reported. If there are
 * two services with the same name, only the active one is reported.
 * Taps on active services are reported even if the tapper does not
 * exist.
 *
 * @return #O2_SUCCESS if success, #O2_FAIL if not.
 */
int o2_services_list();


/**
 * \brief free the list of known services and taps
 *
 * Call this function when the information captured by 
 * #o2_services_list() is no longer needed.
 *
 * @return #O2_SUCCESS if success, #O2_FAIL if not.
 */
int o2_services_list_free();


/**
 * \brief get a service name from a saved list of services
 *
 * See #o2_services_list(). Do not free the returned value. 
 * Instead, call #o2_services_list_free().
 * The pointer will be invalid after calling #o2_services_list_free().
 *
 * @param i the index of the service, starting with zero
 *
 * @return a service name if #i is in range, otherwise NULL.
 */
const char *o2_service_name(int i);


/**
 * \brief get a type from a saved list of services
 *
 * See #o2_services_list(). The return value indicates the type of the service:
 * O2_LOCAL (4) if the service is local, O2_REMOTE (5) if the service
 * is remote, and O2_TAP (8) for each tapper of the service.
 *
 * @param i the index of the service, starting with zero
 *
 * @return a service type, or zero if #i is not in range.
 */
int o2_service_type(int i);


/**
 * \brief get a process name from a saved list of services
 *
 * See #o2_services_list(). Do not free the returned value. 
 * Instead, call #o2_services_list_free().
 * The pointer will be invalid after calling #o2_services_list_free().
 *
 * @param i the index of the service
 *
 * @return a process name if #i is in range, otherwise NULL. The
 *         process name contains the IP address and TCP port number
 *         of the process, making it a unique identifier.
 */
const char *o2_service_process(int i);


/**
 * \brief get a tapper name from a saved list of services
 *
 * See #o2_services_list(). Do not free the returned value. 
 * Instead, call #o2_services_list_free().
 * The pointer will be invalid after calling #o2_services_list_free().
 *
 * @param i the index of the service
 *
 * @return a tapper name if the #i-th service is a tap, otherwise NULL.
 */
const char *o2_service_tapper(int i);


/**
 * \brief get the properties string from a saved list of services
 *
 * See #o2_services_list(). Properties have the form: 
 * "attr1:value1;attr2:value2;...", where
 * attributes are alphanumeric, and values can be any string with
 * colon represented by "\:", semicolon represented by "\;", and 
 * slash represented by "\\". Escape characters are not removed, and
 * the result should not be modified or freed.
 *
 * Do not free the returned value. Instead, call #o2_services_list_free().
 * The pointer will be invalid after calling #o2_services_list_free().
 *
 * @param i the index of the service
 *
 * @return a properties string if #i is in range. The string may be 
 *         empty if the service has no properties. The result is NULL
 *         if #i is not in range.
 */
const char *o2_service_properties(int i);

/**
 * \brief get a property value from a saved list of services
 *
 * @param i the index of the service with the properties
 *
 * @param attr an attribute to search for
 *
 * @return the value of that attribute or NULL if not found or invalid
 *         or attr is too long (limit is 64 characters). The result is
 *         owned by the caller and should be freed using O2_FREE. The
 *         returned value has escape characters removed.
 */
const char *o2_service_getprop(int i, const char *attr);

/**
 * \brief find a service matching attribute/value pair
 *
 * @param i the index from which to start searching
 *
 * @param attr the attribute to search
 * 
 * @param value the value substring that must match. To match a prefix,
 *        use ":prefix"; to match a suffix, use "suffix;"; to make an
 *        exact full match, use ":value;". Since the value itself may
 *        contain ':', ';', and '\' characters, these must be escaped
 *        with '\'. (Unfortunately, in a C or C++ literal string, the 
 *        '\' itself must also be escaped, so to search for an exact 
 *        to the value "x;y", we must escape ';' to get the 4 character
 *        string denoted in C by "x\\;y", then add ':' and ';' to indicate
 *        an exact match: ":x\\;y;")
 *
 * @return the index of the first service (at index i or above) where
 *       the property named by attr contains value as a substring or
 *       prefix or suffix, as indicated by ":" and ";" characters.
 *       If no match is found, return -1.
 */
int o2_service_search(int i, const char *attr, const char *value);


/**
 * \brief set an attribute and value property for a service
 *
 * @param service the name of a service offered by this process
 *
 * @param attribute the attribute name
 * 
 * @param value the value string; this string will be escaped. Do
 *        not include escape characters in #value
 *
 * @returns O2_SUCCESS if successful.
 *
 * Note that each call will broadcast the property change to every
 * other O2 process in the ensemble. Therefore properties are not
 * recommended for publishing values frequently to clients, expecially
 * if multiple properties are typically updated in sequence, e.g.
 * X, Y, Z coordinates, which would result in 3 messages to each 
 * other process. Consider sending X, Y, Z together in a normal O2
 * message, and consider using taps if the "publisher" does not know
 * all the "subscribers."
 */
int o2_service_set_property(const char *service, const char *attr,
                            const char *value);


/**
 * \brief remove an attribute and value property from a service
 *
 * Search for a service offered by the current process named by
 * #service. Then remove both the attribute and value of the 
 * property named by #attribute.
 *
 * @param service the name of a service offered by this process
 *
 * @param attribute the attribute name
 * 
 * @returns O2_SUCCESS if successful
 */
int o2_service_property_free(const char *service, const char *attr);


/**
 * \brief install tap to copy messages from one service to another
 *
 * @param tappee the service to be tapped
 *
 * @param tapper the existing local service to which copies are sent
 *
 * @return #O2_SUCCESS if success, #O2_FAIL if a tap is not installed.
 *
 * Have messages delivered to #tappee copied to #tapper. After
 * this call, messages to #tappee are first delivered. Then, if
 * #tappee is local and #tapper exists, the message is copied, 
 * modified by replacing the service name with #tapper, and sent.
 * There may be multiple taps on a single service, resulting in 
 * the delivery of multiple copies.
 *
 * Note that while services are normally independent of
 * processes (for example, a new service can override an existing
 * one in another process), tappers are tied to processes and cannot
 * be overridden by another service of the same name. It is not redirected
 * to another service provider. Also, unlike ordinary message delivery
 * that delivers one message even if there are multiple processes 
 * offering the service, there can be multiple tappers, even with the
 * same service names, and each tapper receives a copy of every message
 * delivered to the tappee.
 *
 * Taps can be used to implement a publish/subscribe
 * model. The publisher creates a local service and need not install
 * any message handlers. Subscribers install taps on the service to
 * receive messages. It is more efficient for the tappee to be in the
 * publisher process, but actually any process can publish to the
 * service with the added cost of sending a message to the tappee's
 * process.
 *
 * The lifetime of a tap is independent of the lifetimes of the tappee
 * and the tapper, but the tap is tied to a process, so if the process
 * is terminated, the tap is destroyed throughout the O2 ensemble. For
 * example, if the tappee's process crashes and restarts, and if the
 * tapper belongs to another process that survives, then the tap will
 * be reinstated on the tappee. Similarly, a tap may be created *before*
 * the tappee or the tapper. Calling #o2_service_free() on a tapper will
 * *not* free the tap, so tap messages will continue to be delivered to
 * the tap's process, and if the tappper service is (re)created, these
 * tap messages will be delivered to the new tapper service.
 */
int o2_tap(const char *tappee, const char *tapper);


/**
 * \brief remove tap from service
 *
 * @param tappee the service that is tapped
 *
 * @param tapper the service to which copies are sent
 *
 * @return #O2_SUCCESS if success, #O2_FAIL if no tap was removed.
 *
 * Remove a previously installed tap
 */
int o2_untap(const char *tappee, const char *tapper);


/**
 *  \brief Remove a local service
 *
 * The #service_name corresponds to the parameter previously passed to
 * #o2_service_new or #o2_osc_delegate. Note that if an OSC port 
 * forwards to this service (see #o2_osc_port_new), the port remains
 * open, but the OSC messages will be dropped. See #o2_osc_port_free().
 *
 * @param service_name the name of the service
 * 
 * @return #O2_SUCCSS if success, #O2_FAIL if not.
 */
int o2_service_free(const char *service_name);


/**
 * \brief Add a handler for an address.
 *
 * @param path      the address including the service name. If the address
 *                      is only the service name with no trailing slash,
 *                      the handler will match any message to the service.
 *                      Addresses should not conflict: An address should
 *                      not match another address, and for every pair of
 *                      addresses X and Y, X/ should not be a prefix of Y.
 * @param typespec  the types of parameters, use "" for no parameters and
 *                      NULL for no type checking
 * @param h         the handler
 * @param user_data pointer saved and passed to handler
 * @param coerce    is true if you want to allow automatic coercion of types.
 *                      Coercion is only enabled if both coerce and parse are
 *                      true.
 * @param parse     is true if you want O2 to construct an argv argument
 *                      vector to pass to the handle
 *
 * @return O2_SUCCESS if succeed, O2_FAIL if not.
 */
int o2_method_new(const char *path, const char *typespec,
                  o2_method_handler h, void *user_data, int coerce, int parse);


/**
 *  \brief Process current O2 messages.
 *
 *  Since O2 does not create a thread and O2 requires active processing
 *  to establish and maintain connections, the O2 programmer (user)
 *  should call o2_poll() periodically, even if not offering a service.
 *  o2_poll() runs a discovery protocol to find and connect to other
 *  processes, runs a clock synchronization protocol to establish valid
 *  time stamps, and handles incoming messages to all services. O2_poll()
 *  should be called at least 10 times per second. Messages can only be
 *  delivered during a call to o2_poll() so more frequent calls will
 *  generally lower the message latency as well as the accuracy of the
 *  clock synchronization (at the cost of greater CPU utilization).
 *  Human perception of timing jitter is on the order of 10ms, so
 *  polling rates of 200 to 1000 are advised in situations where
 *  rhythmic accuracy is expected.
 *
 *  @return 0 (O2_SUCCESS) if succeed, -1 (O2_FAIL) if not.
 */
int o2_poll(void);

/**
 * \brief Run O2.
 *
 * Call o2_poll() at the rate (in Hz) indicated.
 * Returns if a handler sets #o2_stop_flag to non-zero.
 */
int o2_run(int rate);

/**
 * \brief Check the status of the service.
 *
 *  @param service the name of the service
 * @return
 * - #O2_FAIL if no service is found,
 * - #O2_LOCAL_NOTIME if the service is local but we have no clock sync yet,
 * - #O2_REMOTE_NOTIME if the service is remote but we have no clock sync yet,
 * - #O2_BRIDGE_NOTIME if service is attached by a non-IP link, but we have
 *        no clock sync yet (if the non-IP connection is not handled
 *        by this process, the service status will be #O2_REMOTE_NOTIME),
 * - #O2_TO_OSC_NOTIME if service forwards to an OSC server but we
 *        have no clock sync yet (if the OSC connection is not handled
 *        by this process, the service status will be #O2_REMOTE_NOTIME),
 * - #O2_LOCAL if service is local and we have clock sync,
 * - #O2_REMOTE if service is remote and we have clock sync,
 * - #O2_BRIDGE if service is handled locally by forwarding to an
 *        attached non-IP link, and we have clock sync. (If the non-IP
 *        connection is not local, the service status will be #O2_REMOTE).
 * - #O2_TO_OSC if service is handled locally by forwarding to an OSC
 *       server and this process has clock sync. (If the OSC
 *       connection is not handled locally, the service status will be
 *       #O2_REMOTE).
 *
 *  @return Note that codes are carefully
 * ordered to allow testing for categories:
 * - to test if delivery is possible with a zero (immediate) timestamp,
 * use `o2_status(service) > O2_FAIL`, `o2_status(service) >= 0`, or
 * `o2_status(service) >= O2_LOCAL_NOTIME`.
 * - to test if delivery is possible with a non-zero timestamp, use
 * `o2_status(service) >= O2_LOCAL`. Note that status can change over
 * time, e.g. the
 * status of a remote service will be #O2_FAIL until the service is
 * discovered. It will then change to #O2_REMOTE_NOTIME until both the
 * sender and receiver achieve clock synchronization and share their
 * synchronized status, and finally the status will become #O2_REMOTE.
 *
 * In the cases with no clock sync, it is safe to send an immediate message
 * with timestamp = 0, but non-zero timestamps are meaningless because
 * either the sending process has no way to obtain a valid timestamp
 * or the receiver has no way to schedule delivery according to a
 * timestamp.
 *
 * Messages to services are *dropped* if the service has not been
 * discovered. Timestamped messages (timestamp != 0) are *dropped* if
 * the sender and receiver are not
 * clock-synchronized. (`o2_status(service) >= O2_LOCAL`).
 *
 * A special case is with `BRIDGE` and `OSC` services. In these cases,
 * the O2 process offering the service can either schedule the
 * messages locally, sending them according to the timestamp (and
 * suffering some network latency), or if the destination process is
 * synchronized, messages can be forwarded immediately for more
 * precise scheduling at their final destination. O2 does not provide
 * any way for clients/users to determine which of these methods is in
 * effect, and in the case of messages being forwarded by an
 * intermediary O2 process, the originator of the message cannot
 * determine whether the service is offered by an O2 server on the
 * local network, by an OSC server, or through a bridge to another
 * network such as Bluetooth. The status at the originator will be
 * simply #O2_REMOTE or #O2_REMOTE_NOTIME.
 *
 * When the status of a service changes, a message is sent with address
 * `!_o2/si`. The type string is "sis" and the parameters are (1) the 
 * service name, (2) the new status, and (3) the ip:port string of
 * the process that offers (or offered) the service.
 */
int o2_status(const char *service);


/**
 * \brief Test if send_cmd will block.
 *
 * @param service the name of the service.
 *
 * @return #O2_SUCCESS if sending a command to this service will not
 * block, or #O2_BLOCKED if sending a command to this service will block.
 * #O2_FAIL is returned if the service is unknown.
 *
 * If a process is streaming data to another and the TCP buffers become
 * full, the sender will block. Normally, blocking is short term, but
 * if the receiver is not reading, the sender can block indefinitely. 
 * It is possible for the receiver to be blocked sending responses, which
 * is one case where deadlock can occur. If necessary for responsiveness
 * or deadlock-avoidance, the sender should call #o2_can_send() before
 * calling #o2_send_cmd(). If #O2_BLOCKED is returned, the caller should
 * defer the call to #o2_send_cmd() but continue calling #o2_poll(),
 * and at some point in the future #o2_can_send() should return #O2_SUCCESS.
 */
int o2_can_send(const char *service);


/**
 * \brief A variable indicating that the clock is the master or is
 *        synchronized to the master.
 */
extern int o2_clock_is_synchronized;

/**
 *  \brief Get network round-trip information.
 *
 * @return If clock is synchronized, return O2_SUCCESS and set
 *   `*mean` to the mean round-trip time and `*min` to the minimum
 *   round-trip time of the last 5 (where 5 is the value of
 *   CLOCK_SYNC_HISTORY_LEN) clock sync requests. Otherwise,
 *   O2_FAIL is returned and `*mean` and `*min` are unaltered.
 *
 * Note: You can get this information from a remote process by 
 * sending a message to `!ip:port/cs/rt`, where `ip:port` is the
 * ip:port string for a process. (One way to get this is to call
 * `o2_get_address` and construct a ip:port process name from 
 * the information returned. But then you can just call 
 * `o2_roundtrip` for the local process round trip information.
 * To get remote process names, you can create a handler for 
 * `/_o2/si`. The process name is provided whenever one of its
 * services is created or otherwise changes status.) The 
 * type string for `!ip:port/cs/rt` is "s", and the parameter is
 * an O2 address prefix. When the message is received, a reply is
 * sent to an address formed by appending "/get-reply" to the 
 * address prefix. The reply message has the type string "sff",
 * and the parameters are (1) the process ip:port name, (2) the
 * mean of recent round trip times to the master clock, and
 * (3) the minimum of recent round trip times. (The clock is set
 * using the minimum, so this number is an upper bound on the
 * clock skew for this process.
 */
int o2_roundtrip(double *mean, double *min);


/** \brief signature for callback that defines the master clock
 *
 * See o2_clock_set() for details.
 */
typedef o2_time (*o2_time_callback)(void *rock);


/**
 *  \brief Provide a time reference to O2.
 *
 *  Exactly one process per O2 ensemble should provide a master
 *  clock. All other processes synchronize to the master. To become
 *  the master, call o2_clock_set(). 
 *
 *  The time reported by the gettime function will be offset to 
 *  match the current local time so that local time continues to 
 *  increase smoothly. You cannot force O2 time to match an external 
 *  absolute time, but once o2_clock_set() is called, the difference
 *  between the time reference and O2's local time (as reported by 
 *  o2_local_time()) will be fixed.
 *
 *  @param gettime function to get the time in units of seconds. The
 *  reference may be operating system time, audio system time, MIDI
 *  system time, or any other time source. The times returned by this
 *  function must be non-decreasing and must increase by one second
 *  per second of real time to close approximation. The value may be
 *  NULL, in which case a default time reference will be used.
 *
 *  @parm rock an arbitrary value that is passed to the gettime
 *  function. This may be need to provide context. Use NULL if no
 *  context is required.
 *
 *  @return #O2_SUCCESS if success, #O2_FAIL if not.
 */
int o2_clock_set(o2_time_callback gettime, void *rock);


/**
 * \brief Construct and send O2 message with best effort protocol
 *
 *  Normally, this constructs and sends an O2 message via UDP. If the
 *  destination service is reached via some other network protocol
 *  (e.g. Bluetooth), the message is delivered in the lowest latency
 *  protocol available, with no guaranteed delivery.
 *
 *  @param path an address pattern
 *  @param time when to dispatch the message, 0 means right now. In any
 *  case, the message is sent to the receiving service as soon as
 *  possible. If the message arrives early, it will be held at the
 *  service and dispatched as soon as possible after the indicated time.
 *  @param typestring the type string for the message. Each character
 *  indicates one data item. Type codes are as in OSC. Some O2 type
 *  codes are not supported (if needed, create a message and use the
 *  appropriate o2_add_... function. Allowed type characters are those
 *  in "ifsbhtdScmTFNIB".
 *  @param ...  the data of the message. There is one parameter for each
 *  character in the typestring.
 *
 *  @return #O2_SUCCESS if success, #O2_FAIL if not.
 *
 */
/** \hideinitializer */ // turn off Doxygen report on o2_send_marker()
#define o2_send(path, time, ...)         \
    o2_send_marker(path, time, FALSE,    \
                   __VA_ARGS__, O2_MARKER_A, O2_MARKER_B)

/** \cond INTERNAL */ \
int o2_send_marker(const char *path, double time, int tcp_flag, 
                   const char *typestring, ...);
/** \endcond */

/**
 * \brief Construct and send an O2 message reliably.
 *
 *  Normally, this constructs and sends an O2 message via TCP. If the
 *  destination service is reached via some other network protocol
 *  (e.g. Bluetooth), the message is delivered using the most reliable
 *  protocol available. (Thus, this call is considered a "hint" rather
 *  than an absolute requirement.)
 *
 *  If the send would block, the message is held until the socket 
 *  unblocks, allowing O2 to send the message. In this state, where
 *  one message is waiting to be sent, #o2_can_send() will return 
 *  #O2_BLOCKED. In this state, #o2_send_cmd() will block until the
 *  pending message can be sent. Then #o2_send_cmd() will try again,
 *  possibly re-entering the #O2_BLOCKED state with the new message.
 *
 *  @param path an address pattern
 *  @param time when to dispatch the message, 0 means right now. In any
 *  case, the message is sent to the receiving service as soon as
 *  possible. If the message arrives early, it will be held at the
 *  service and dispatched as soon as possible after the indicated time.
 *  @param typestring the type string for the message. Each character
 *  indicates one data item. Type codes are defined by #o2_type.
 *  @param ...  the data of the message. There is one parameter for each
 *  character in the typestring.
 *
 *  @return #O2_SUCCESS if success (including holding the message as
 *  pending until the socket unblocks). #O2_FAIL if an error occurred.
 *
 */
/** \hideinitializer */ // turn off Doxygen report on o2_send_marker()
#define o2_send_cmd(path, time, ...) \
    o2_send_marker(path, time, TRUE, \
                   __VA_ARGS__, O2_MARKER_A, O2_MARKER_B)


/**
 * \brief Send an O2 message. (See also macros #o2_send and #o2_send_cmd).
 *
 * @param msg points to an O2 message.
 *
 *  @return #O2_SUCCESS if success, #O2_FAIL if not.
 *
 * After the call, the `msg` parameter is "owned" by O2, which will
 * free it. Therefore, do *not* free msg after calling o2_message_send().
 */
int o2_message_send(o2_message_ptr msg);

/**
 * \brief Get the estimated synchronized global O2 time.
 *
 *  This function returns a valid value either after you call
 *  o2_clock_set(), making the local clock the master clock for the O2
 *  ensemble, or after O2 has finished discovering and
 *  synchronizing with the master clock. Until then, -1 is returned.
 *
 *  The clock accuracy depends upon network latency, how often
 *  o2_poll() is called, and other factors, but
 *
 *  @return the time in seconds, or -1 if global (master) time is unknown.
 */
o2_time o2_time_get(void);


/**
 * \brief Get the real time using the local O2 clock
 *
 * @return the local time in seconds
 */
o2_time o2_local_time(void);

/**
 *  \brief Return text representation of an O2 error
 *
 *  @param i error number returned from some O2 function
 *
 *  @return return the error message as a string
 */
const char *o2_error_to_string(int i);

/**
 *  \brief release the memory and shut down O2.
 *
 *  Close all sockets, free all memory, and restore critical
 *  variables so that O2 behaves as if it was never initialized.
 *
 *  @return #O2_SUCCESS if success, #O2_FAIL if not.
 */
int o2_finish(void);


// Interoperate with OSC
/**
 *  \brief Create a port to receive OSC messages.
 *
 *  OSC messages are converted to O2 messages and directed to the service.
 *  E.g. if the service is "maxmsp" and the message address is
 *  `/foo/x`, then the message is directed to and handled by
 *  `/maxmsp/foo/x`. If the #service_name does not exist at any time
 *  after calling #o2_osc_port_new, incoming OSC messages will be dropped
 *  until the service is available again. Note that this function does
 *  not automatically create a service named `service_name`.
 *
 *  @param service_name The name of the service to which messages are delivered
 *  @param port_num     Port number.
 *  @param tcp_flag     Be a TCP server for remote clients. Otherwise, use UDP
 *
 *  @return #O2_SUCCESS if success, #O2_FAIL if not.
 */
int o2_osc_port_new(const char *service_name, int port_num, int tcp_flag);

/**
 * \brief Remove a port receiving OSC messages.
 *
 * This removes a port created by #o2_osc_port_new(). If you want to 
 * remove the corresponding service, you must also call #o2_service_free()
 * with the service name. The port should be the same port passed to
 * #o2_osc_port_new(). In the case of TCP, this will close all connections
 * that were *accepted* from this server port.
 *
 * @param port_num The port number that receives OSC messages or that receives
          connect requests for TCP connections.
 * 
 * @return #O2_SUCCESS if success, #O2_FAIL if not.
 *
 */
int o2_osc_port_free(int port_num);


/**
 *  \brief Create a service that forwards O2 messages to an OSC server.
 *
 *  @param service_name The o2 service name without a '/' prefix.
 *  @param ip           The ip address of the osc server.
 *  @param port_num     The port number of the osc server.
 *  @param tcp_flag     Send OSC message via TCP protocol, in which case
 *                      port_num is the TCP server port, not a connection.
 *
 *  @return #O2_SUCCESS if success, #O2_FAIL if not.
 *
 *  If `tcp_flag` is set, a TCP connection will be established with
 *  the OSC server.
 *  When the created service receives any O2 messages, it will
 *  send the message to the OSC server. If the incoming message has
 *  a timestamp for some future time, the message will be held until
 *  that time, then sent to the OSC server. (Ideally, O2 could convert
 *  the message to an OSC timestamped bundle and send it immediately
 *  to achieve precise forward-synchronous timing, but this requires
 *  clock synchronization with the OSC server, which is normally
 *  unimplemented.)
 *
 * If this is a tcp connection, close it by calling #o2_service_free().
 */
int o2_osc_delegate(const char *service_name, const char *ip, int port_num, 
                    int tcp_flag);

/**
 *  \brief Set the OSC time offset.
 *
 * @param offset the offset between (global) O2 time and OSC time
 *
 * @return the previous offset
 *
 * O2 global time should start from 0.0 when the clock is started, whereas
 * OSC time starts at 1 Jan 1900. The offset is the OSC time corresponding
 * to O2 time 0.0. Equivalently, OSC_time = O2_time + offset.
 */
uint64_t o2_osc_time_offset(uint64_t offset);


/** @} */ // end of Basics

/**
 * \defgroup lowlevelsend Low-Level Message Send
 *
 * Rather than passing all parameters in one call or letting O2
 * extract parameters from a message before calling its handler,
 * these functions allow building messages one parameter at a time
 * and extracting message parameters one at a time.
 * The functions operate on "hidden" messages, so these functions are
 * not reentrant.
 *
 * To build a message, begin by calling o2_send_start() to allocate a
 * message. Then call one of the `o2_add_()` functions to add each
 * parameter. Finally, call either o2_send_finish() to send the 
 * message. You should not explicitly allocate or deallocate a 
 * message using this procedure.
 *
 * To extract parameters from a message, begin by calling
 * o2_extract_start() to prepare to get parameters from the
 * message. Then call o2_get_next() to get each parameter. If the
 * result is non-null, a parameter of the requested type was obtained
 * and you can read the parameter from the result. Results other than
 * strings, MIDI, and blobs may only remain valid until the next call
 * to o2_get_next(), so you should use or copy the value before reading
 * the next one. Values that are not coerced (requiring a copy) are 
 * left in the O2 message and have the same lifetime as the message.
 * You should not reuse this storage because the message may have
 * multiple destinations; thus, the message content should not be altered.
 *
 * A by-product of o2_extract_start() and o2_get_next() is an argument
 * vector (argv) that can be accessed from o2_argv. (This is the same
 * argument vector created automatically when a handler is added with
 * o2_method_new() when the parse parameter is true.) A possible
 * advantage of using a sequence of o2_get_next() calls rather than
 * simply setting the parse flag is that you can receive messages with
 * various types and numbers of parameters. Also, you can check vector
 * lengths and stop parsing if unacceptable lengths are encountered.
 * 
 * o2_get_next() will perform type conversion if possible
 * when the requested type does not match the actual type. You can
 * determine the original type by reading the type string in the
 * message. The number of parameters is determined by the length of
 * the type string, with some exceptions.
 *
 * Vectors can be coerced into arrays, in which case
 * each element will be coerced as requested. Arrays can be coerced 
 * into vectors if each element of the array can be coerced into
 * the expected vector element type. Vector lengths are provided by
 * the message; there is no way to coerce or limit vector lengths or
 * check that the length matches an expected value. (You can determine
 * the length from the return value and of course you can decide to
 * reject the message if the length is not acceptable.)
 *
 * When a vector is returned, the argument vector has a single element
 * that points to a vector descriptor (the "v" field), which contains
 * the vector element types and the length of the vector (>= 0).
 *
 * When an array is returned, the argument vector contains the value
 * o2_got_start_array followed by an o2_arg_ptr for each element of 
 * the array, followed by o2_got_end_array.
 *
 * When types T (True), F (False), I (Infinitum), or N (Nil) are in 
 * the message, there is an entry in the argument vector; however, 
 * there is no data associated with these types (other than the type
 * itself), so the pointers point to zero bytes and therefore should
 * not be used.
 *
 * In all other cases, the argument vector contains data
 * corresponding to the data item in the message. This may be a pointer
 * into the actual message or a pointer to a temporary location in case
 * the element was coerced to a different type.
 *
 * When the actual type code in the message is in "TFIN" you should
 * call o2_get_next() even though there is no corresponding data
 * stored in the message. The return value, if successful, is a
 * non-NULL pointer that points within or just after the message, but
 * you must not dereference this pointer. (NULL indicates failure as
 * with other type codes. One rationale for calling o2_get_next()
 * even when there is nothing to "get" is that you can call
 * o2_get_next('B') to retrieve 'T', 'F', or 'B' types as an int32_t
 * which is 0 or 1. The 'I' and 'N' types are never coerced.
 *
 * Normally, you should not free the message because
 * normally you are accessing the message in a handler and the message
 * will be freed by the O2 message dispatch code that called the
 * handler.
 *
 * Arrays denoted by [...] in the type string are handled in a somewhat
 * special way:
 *
 * If an array is expected, call o2_get_next('['). The return value will be
 * o2_got_start_array on success, or NULL if there is no array. The actual
 * value in the message may be an array or a vector. If it is a vector, the
 * elements of the vector will be coerced to the types requested in 
 * successive calls to o2_get_next(). After retrieving array elements, call
 * o2_get_next(']'). The return value should be o2_got_end_array. NULL is
 * returned if there is an error. For example, suppose you call o2_get_next()
 * with characters from the type string "[id]" and the actual parameter is
 * a vector integers ("vi") of length 2. The return values from o2_get_next()
 * will be o2_got_start_array, an o2_arg_ptr to an integer, an o2_arg_ptr to
 * a double (coerced from the integer vector), and finally o2_got_end_array.
 * If the vector length is 1, the third return value will be NULL. If the
 * vector length is 3 (or more), the fourth return value will be NULL rather
 * than o2_got_end_array.
 *
 * The special values o2_got_start_array and o2_got_end_array are not valid
 * structures. In other words, fields such as o2_got_start_array->i32 are 
 * never valid or meaningful. Instead, o2_got_start_array and o2_got_end_array
 * are just 'tokens' used to indicate success in type checking. These values
 * are distinct from NULL, which indicates a type incompatibility.
 *
 * Note also that vector elements cannot be retrieved directly without
 * calling o2_get_next('v') or o2_get_next('['). For example, if the actual
 * argument is a two-element integer vector ("vi"), a call to 
 * o2_get_next(O2_INT32) will fail unless it is preceded by 
 * o2_get_next(O2_VECTOR) or o2_get_next(O2_ARRAY_START).
 *
 * If a vector is expected, call o2_get_next(O2_VECTOR). The return value will
 * be a non-null o2_arg_ptr if the next argument in the actual message 
 * is a vector or array, and otherwise NULL. You should not dereference this
 * return value yet...
 *
 * You *must* then call o2_get_next() with the desired type for vector 
 * elements. The return value will be an o2_arg_ptr (which will be
 * the same value previously returned) containing v.typ set to 
 * the desired type, v.len still set to the number of elements, and v.vi,
 * v.vh, v.vd, v.vf, or v.vc pointing to the possibly coerced elements.
 *
 * Note that the sequence of calling o2_get_next() twice for vectors
 * corresponds to the two type characters used to encode them, e.g. "vi"
 * indicates a vector of integers.
 *
 * Coercion is supported as follows. If coercion is provided from
 * the type indicated on the left on some row to the types corresponding
 * to columns where an "x" appears ("*" indicates special consideration
 * described below.
 * 
 *      i h f d t s S T F B b m c N I
 *    i x x x x x     * * x            32-bit int
 *    h x x x x x     * * x            64-bit int
 *    f x x x x x     * * x            float
 *    d x x x x x     * * x            double
 *    t x x x x x                      time
 *    s           x x                  String
 *    S           x x                  Symbol
 *    T x x x x       x   x            True
 *    F x x x x         x x            False
 *    B x x x x       * * x            Boolean
 *    b                     x          blob
 *    m                       x        MIDI
 *    c                         x      character
 *    N                           x    Nil
 *    I                             x  Infinitum
 *
 *    *Entries marked with "*": Coercion succeeds 
 *    from 0 to False and from non-zero to True,
 *    otherwise coercion fails.
 */


/** \addtogroup lowlevelsend
 * @{
 */
/**
 * \brief Allocate a blob.
 *
 * Allocate a blob and initialize the size field. If the return address
 * is not NULL, copy data (up to length size) to `blob->data`. You can
 * change `blob->size`, but of course you should not set `blob->size`
 * greater than the `size` parameter originally passed to o2_blob_new().
 *
 * Caller is responsible for freeing the returned blob using O2_FREE().
 *
 * A constructed blob can be added to a message. If you add parameters to
 * a message one-at-a-time, you can use o2_add_blob_data() to copy data
 * directly to a message without first allocating a blob and copying
 * data into it.
 *
 * @param size The size of the data to be added to the blob
 *
 * @return the address of the new blob or NULL if memory cannot be allocated.
 */
o2_blob_ptr o2_blob_new(uint32_t size);


/**
 * \brief Prepare to build a message
 *
 * @return #O2_SUCCESS if success, #O2_FAIL if not.
 *
 * Allocates a "hidden" message in preparation for adding
 * parameters. After calling this, you should call `o2_add_` functions
 * such as o2_add_int32() to add parameters. Then call
 * o2_send_finish() to send the message.
 */
int o2_send_start(void);


/// \brief add a `float` to the message (see o2_send_start())
int o2_add_float(float f);

/// \brief This function suppports o2_add_symbol() and o2_add_string()
/// Normally, you should not call this directly.
int o2_add_string_or_symbol(o2_type tcode, const char *s);

/// \brief add a symbol to the message (see o2_send_start())
#define o2_add_symbol(s) o2_add_string_or_symbol(O2_SYMBOL, s)

/// \brief add a string to the message (see o2_send_start())
#define o2_add_string(s) o2_add_string_or_symbol(O2_STRING, s)

/// \brief add an `o2_blob` to the message (see o2_send_start()), where
///        the blob is given as a pointer to an #o2_blob object.
int o2_add_blob(o2_blob_ptr b);

/// \brief add an `o2_blob` to the message (see o2_send_start()), where
///        the blob is specified by a size and a data address.
int o2_add_blob_data(uint32_t size, void *data);

/// \brief add an `int64` to the message (see o2_send_start())
int o2_add_int64(int64_t i);

/// \brief This function supports o2_add_double() and o2_add_time()
/// Normally, you should not call this directly.
int o2_add_double_or_time(o2_type tchar, double d);

/// \brief add a `double` to the message (see o2_send_start())
#define o2_add_double(d) o2_add_double_or_time(O2_DOUBLE, d)

/// \brief add a time (`double`) to the message (see o2_send_start())
#define o2_add_time(t) o2_add_double_or_time(O2_TIME, t)

/// \brief This function supports o2_add_int32() and o2_add_char()
/// Normally, you should not call this directly.
int o2_add_int32_or_char(o2_type tcode, int32_t i);

/// \brief add an `int32` to the message (see o2_send_start())
#define o2_add_int32(i) o2_add_int32_or_char(O2_INT32, i)

/// \brief add a `char` to the message (see o2_send_start())
#define o2_add_char(c) o2_add_int32_or_char(O2_CHAR, c)

/// \brief add a short midi message to the message (see o2_send_start())
int o2_add_midi(uint32_t m);

/// \brief This function supports o2_add_true(), o2_add_false(), o2_add_bool(),
/// o2_add_nil(), o2_add_infinitum(), and others.
/// Normally, you should not call this directly.
int o2_add_only_typecode(o2_type typecode);

/// \brief add "true" to the message (see o2_send_start())
#define o2_add_true() o2_add_only_typecode(O2_TRUE);

/// \brief add a "false" to the message (see o2_send_start())
#define o2_add_false() o2_add_only_typecode(O2_FALSE);

/// \brief add a boolean typecode T or F
///  (this differs from `o2_add_bool` which uses typecode B and adds
///  a 0 or 1 as data)
#define o2_add_tf(x) o2_add_only_typecode((x) != 0 ? O2_TRUE : O2_FALSE)

/// \brief add 0 (false) or 1 (true) to the message (see o2_send_start())
#define o2_add_bool(x) o2_add_int32_or_char(O2_BOOL, (x) != 0)

/// \brief add "nil" to the message (see o2_send_start())
#define o2_add_nil() o2_add_only_typecode(O2_NIL);

/// \brief add "infinitum" to the message (see o2_send_start())
#define o2_add_infinitum() o2_add_only_typecode(O2_INFINITUM);

/// \brief start adding an array
#define o2_add_start_array() o2_add_only_typecode(O2_ARRAY_START);

/// \brief finish adding an array
#define o2_add_end_array() o2_add_only_typecode(O2_ARRAY_END);

/** \brief add a vector
 *
 * @param element_type the type of the vector elements
 * @param length the number of vector elements
 * @param data the vector elements; arranged sequentially in memory
 *             in a format determined by element_type. The element
 *             type is restricted to a character in "ifhtdc"
 */
int o2_add_vector(o2_type element_type,
                  int length, void *data);

/**
 * \brief add a message to a bundle
 *
 * @param msg a message or bundle to add
 *
 * @return O2_SUCCESS
 *
 * This function can be called after o2_send_start(). If you 
 * add a message to a bundle with this function, you must not
 * call any other o2_add_*() functions. E.g. do not call both
 * o2_add_int32() and o2_add_message() on the same message.
 * 
 * This function does NOT free msg. Probably you should call 
 * o2_message_free(msg) after calling o2_add_message(msg).
 */
int o2_add_message(o2_message_ptr msg);


/**
 * \brief finish and return the message.
 *
 * @param time the timestamp for the message (0 for immediate)
 * @param address the O2 address pattern for the message
 * @param tcp_flag boolean if true, send message reliably
 *
 * @return the address of the completed message, or NULL on error
 *
 * The message must be freed using o2_message_free() or by calling
 * o2_message_send(). If the message is a bundle (you have added
 * messages using o2_add_message()), the address should be '#' 
 * followed by the service name, e.g. "#service1".
 */
o2_message_ptr o2_message_finish(o2_time time, const char *address,
                                 int tcp_flag);

/**
 * \brief finish and return a message, prepending service name
 *
 * @param time the timestamp for the message (0 for immediate)
 * @param service a string to prepend to address or NULL.
 * @param address the O2 address pattern for the message.
 * @param tcp_flag boolean if true, send message reliably
 *
 * @return the address of the completed message, or NULL on error
 *
 * The message must be freed using o2_message_free() or by calling
 * o2_message_send(). This function is intended to be used to 
 * forward OSC messages to a service, but it is the implementation
 * of o2_message_finish(), which simply passes NULL for service.
 */
o2_message_ptr o2_service_message_finish(o2_time time,
             const char *service, const char *address, int tcp_flag);

/**
 * \brief free a message allocated by o2_send_start().
 *
 * This function is not normally used because O2 functions that send
 * messages take "ownership" of messages and (eventually) free them.
 */
void o2_message_free(o2_message_ptr msg);


/**
 * \brief send a message allocated by o2_send_start().
 *
 * This is similar to calling o2_send(), except you use a three-step
 * process of (1) allocate the message with o2_send_start(), (2) add
 * parameters to it using `o2_add_` functions, and (3) call
 * o2_send_finish() to send it.
 *
 * @param time the timestamp for the message
 * @param address the destination address including the service name.
 *        To send a bundle to a service named foo, use the address "#foo".
 * @param tcp_flag boolean that says to send the message reliably.
 *        Normally, true means use TCP, and false means use UDP.
 *
 * @return #O2_SUCCESS if success, #O2_FAIL if not.
 */
int o2_send_finish(o2_time time, const char *address, int tcp_flag);


/** @} */

/**
 * \defgroup lowlevelparse Low-Level Message Parsing
 *
 * These functions can retrieve message arguments one-at-a-time.
 * There are some hidden state variables to keep track of the state
 * of unpacking, so these functions are not reentrant.
 * Arguments are returned using a pointer to a union type: #o2_arg_ptr.
 *
 */

/** \addtogroup lowlevelparse
 * @{
 */

/**
 * \brief initialize internal state to parse, extract, and coerce
 * message arguments.
 *
 * @return length of the type string in msg
 *
 * To get arguments from a message, call o2_extract_start(), then for
 * each parameter, call o2_get_next().
 */
int o2_extract_start(o2_msg_data_ptr msg);

/**
 * \brief get the next message parameter
 *
 * This function is called repeatedly to obtain parameters in order
 * from the message passed to o2_extract_start().
 *
 * If the message parameter type matches the `type_code`, a pointer to
 * the parameter is returned. If the types do not match, but coercion
 * is possible, the parameter is coerced, copied to a new location,
 * and a pointer is returned. Otherwise, NULL is returned.
 *
 * The type of any non-NULL return value always matches the type
 * specified by the parameter `type_code`. To determine the
 * original type of the parameter as specified by the message, use the
 * `types` string which is passed to message handlers. (Or course,
 * this assumes that message type strings are correct. Badly formed
 * messages are detected when the type string and data imply that the
 * message is longer than the actual length, but otherwise there is no
 * way to detect errors in type strings.)
 *
 * The result points into the message or to a statically allocated
 * buffer if type coercion is required. This storage is valid
 * until the next call to `o2_extract_start`. If the value is a 
 * pointer (string, symbol, midi data, blob), then the value was
 * not copied and remains in place within the message, so there should
 * never be the need to immediately copy the data pointed to.
 * However, since the storage for the value is the message, and
 * the message will be freed when the handler returns,
 * pointers to strings, symbols, midi data, and blobs
 * *must not* be used after the handler returns.
 *
### Example 1: Simple but not completely robust

Note: call o2_method_new() with type_spec = "id", h = my_handler,
coerce = false, parse = false. In this case, since there is
no type coercion, type_spec must match the message exactly,
so o2_get_next() should always return a non-null o2_arg_ptr.
However, this code can fail if a badly formed message is sent
because there is no test for the NULL value that will be
returned by o2_get_next().
\code{.c}
    int my_handler(o2_message_ptr msg, char *types,
                   o2_arg_ptr *argv, int argc, void *user_data)
    {
        o2_extract_start(msg);
        // we expect an int32 and a double argument
        int32_t my_int = o2_get_next(O2_INT32)->i32;
        double my_double = o2_get_next(O2_DOUBLE)->d;
        ...
    }
\endcode

### Example 2: Type coercion and type checking.

Note: call o2_method_new() with type_spec = NULL, h = my_handler,
coerce = false, parse = false. In this case, even though
coerce is false, there is no type_spec, so the handler will
be called without type checking. We could check the
actual message types (given by types), but here, we will
coerce into our desired types (int32 and double) if possible.
Since type coercion can fail (e.g. string will not be converted
to number, not even "123"), we need to check the return value
from o2_get_next(), where NULL indicates incompatible types.
\code{.c}
    int my_handler(o2_message_ptr msg, char *types,
                   o2_arg_ptr *argv, int argc, void *user_data)
    {
        o2_extract_start(msg);
        // we want to get an int32 and a double argument
        o2_arg_ptr ap = o2_get_next(O2_INT32);
        if (!ap) return O2_FAIL; // parameter cannot be coerced
        int32_t my_int = ap->i32;
        o2_arg_ptr ap = o2_get_next(O2_DOUBLE);
        if (!ap) return O2_FAIL; // parameter cannot be coerced
        double my_double = ap->d;
        ...
    }
\endcode
 *
 * @param type_code the desired parameter type
 *
 * @return the next message parameter or NULL if no more parameters
*/
o2_arg_ptr o2_get_next(o2_type type_code);

/** @} */


/* Scheduling */
/** \addtogroup basics
 * @{
 */

// Messages are stored in the table modulo their timestamp, so the
// table acts sort of like a hash table (this is also called the
// timing wheel structure). Messages are stored as linked lists sorted
// by increasing timestamps when there are collisions.

/** \cond INTERNAL */ \
// Size of scheduler table.
#define O2_SCHED_TABLE_LEN 128

// Scheduler data structure.
typedef struct o2_sched {
  int64_t last_bin;
  double last_time;
  o2_message_ptr table[O2_SCHED_TABLE_LEN];
} o2_sched, *o2_sched_ptr;
/** \endcond */

/**
 * \brief Scheduler that schedules according to global (master) clock
 * time
 *
 * Scheduling on this scheduler (including sending timed messages)
 * will only work after clock synchronization is obtained. Until then,
 * timed message sends will fail and attempts to o2_schedule() will
 * fail.
 */
extern o2_sched o2_gtsched;

/**
 * \brief Scheduler that schedules according to local clock time
 *
 * It may be necessary to schedule events before clock synchronization
 * with the master clock, or you may want to schedule local processing
 * that ignores any changes in clock time or clock speed needed to
 * stay synchronized with the master clock (even though these should
 * be small). For example, O2 uses the local time scheduler to
 * schedule the clock synchronization protocol, which of course must
 * run before clock synchronization is obtained.
 *
 * In these cases, you should schedule messages using #o2_ltsched.
 */
extern o2_sched o2_ltsched;

/**
 * \brief Current scheduler.
 *
 * When a timed message is delivered by a scheduler, #o2_active_sched
 * is set to pount to the scheduler. A handler that constructs and
 * schedules a message can use this pointer to continue using the same
 * scheduler.
 */
extern o2_sched_ptr o2_active_sched; // the scheduler that should be used


/**
 * /brief Schedule a message.
 *
 * Rather than sending a message, messages can be directly
 * scheduled. This is particulary useful if you want to schedule
 * activity before clock synchronization is achieved. For example, you
 * might want to poll every second waiting for clock
 * synchronization. In that case, you need to use the local scheduler
 * (#o2_ltsched). o2_send() will use the global time scheduler
 * (#o2_gtsched), so your only option is to construct a message and
 * call o2_schedule().
 *
 * @param scheduler a pointer to a scheduler (`&o2_ltsched` or
 *        `&o2_gtsched`)
 * @param msg a pointer to the message to schedule
 *
 * The message is scheduled for delivery according to its timestamp
 * (which is interpreted as local or global time depending on the
 * scheduler).
 *
 * The message is delivered immediately if the time is zero or less
 * than the current time; however, to avoid unbounded recursion,
 * messages scheduled within handlers are appended to a "pending
 * messages" queue and delivered after the handler returns.
 */
int o2_schedule(o2_sched_ptr scheduler, o2_message_ptr msg);

/** @} */ // end of a basics group

#ifdef __cplusplus
}
#endif

#endif /* O2_H */
