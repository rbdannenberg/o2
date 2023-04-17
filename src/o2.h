// o2.h -- public header file for o2 system
// Roger B. Dannenberg and Zhang Chi
// see license.txt for license
// June 2016

#ifndef O2_H
#define O2_H

// Version is 2.0.1:
#define O2_VERSION 0x020001

#ifdef O2_NO_O2DISCOVERY
#define O2_NO_HUB 1
#endif
#if defined(O2_NO_O2DISCOVERY) && defined(O2_NO_ZEROCONF)
#error O2_NO_O2DISCOVERY and O2_NO_ZEROCONF are defined, therefore no discovery
#endif
#if !defined(O2_NO_O2DISCOVERY) && !defined(O2_NO_ZEROCONF)
#warning O2DISCOVERY and ZEROCONF are *both* enabled
#warning You should compile with -DO2_NO_O2DISCOVERY
#endif

#include "o2base.h"
#include "hostip.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \file o2.h
\mainpage
\section Introduction

This documentation is divided into modules. Each module describes a
different area of functionality: Basics, Return Codes, Low-Level
Message Send, and Low-Level Message Parsing.

 \section  Notes on Building the O2 Library

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

O2 is based on IP (Internet Protocol), but there is a mechanism that
allows an O2 process to serve as a bridge to other protocols including
WebSockets, shared memory, and MQTT. OSC is also supported: An O2
service can be delegated to an OSC server address, and O2 can offer an
OSC server that forwards to an O2 service.

O2 addresses begin with the service name. Thus, a complete O2 address
would be written simply as "/synth/filterFtoff," where "synth" is
the service name.

Furthermore, O2 implements a clock synchronization protocol. A single
process is designated as the "reference," and other processes
automatically synchronize their local clocks to the reference. All O2
messages are timestamped. Messages are delivered immediately, but
their designated operations are invoked according to the timestamp. A
timestamp of zero (0.0) means deliver the message
immediately. Messages with non-zero timestamps are only deliverable
after both the sender and receiver have synchronized clocks.

A service is created using the functions: 

    o2_service_new("service_name")

and

    o2_method_new("address," "types," handler, user_data, coerce, parse)

, where #o2_method_new is called to install a handler for each node, and each
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
  process to call #o2_poll frequently, giving O2 the opportunity to 
  receive and process messages as well as run background activities for
  discovery and clock synchronization. #o2_poll is non-blocking.

- **Service** - an O2 service is a named server that receives and acts
  upon O2 messages. A service is addressed by name. Multiple services
  can exist within one process. A service does not imply a (new) thread
  and all O2 messages are delivered sequentially from the single thread
  that calls #o2_poll. Service names begin with a letter with the
  exception of "_o2" which denotes the local process, "_cs", which denotes
  the reference clock, and `@public:internal:port` strings that
  denote a remote process.

- **Message** - an O2 message, similar to an OSC message, contains an 
  address pattern representing a function, a type string and a set of 
  values representing parameters. Messages are delivered to and handled
  by *services*. If there are multiple services with the same name, the
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
  as "*" or "[". The first part of an address is the service name; pattern
  characters are not allowed in the service name, so the service name
  must match exactly. A message may be delivered to the service itself,
  e.g. with address pattern "/synth". To receive such a message, the 
  service must have a single handler that handles every message to the
  service (see #o2_method_new).

- **Scheduler** - O2 implements two schedulers for timed message
  delivery. Schedulers are served by the same #o2_poll call that
  runs other O2 activity. The #o2_gtsched schedules according to the
  ensemble's reference clock time, but since this depends on clock
  synchronization, nothing can be scheduled until clock
  synchronization is achieved (typically within a few seconds of
  starting the process that provides the reference clock). For
  local-only services, it is possible to use the #o2_ltsched
  scheduler (not with #o2_send, but by explicitly constructing
  messages and scheduling them with #o2_schedule_msg. In any case,
  scheduling is useful *within* a service for any kind of timed
  activity.

\section Standard Messages

O2 uses O2 messages for some internal functions and also to communicate
status information with O2 processes. Since messages are dropped if there
is no handler, O2 processes need not set up handlers for all information.

In the following descriptions, if the *service name* is `@public:internal:port` it means
the IP address and port number are used to construct a string, e.g. 
"@4a6dfb76:c0a801a6:fc1d" might be the actual service name.


\subsection Internal Messages

`/_o2/dy "sissiii"` *ensemble_name* *version* *public_ip* *internal_ip* *tcp_port* *udp_port* *dy* -
this message is normally sent to the discovery port, but it can also be sent
as a result of calling #o2_hub and providing an O2 process address.
Processes must exchange discovery messages to be connected. The *version*
number encodes the O2 version number of the sender as 
((<major-vers> * 256) + <minor-vers>) * 256 + <patch-vers>, where 
<major-vers>, <minor-vers> and <patch-vers> follow the O2 semantic 
versioning scheme, e.g. version 2.3.4 would have version number 0x020304.
Thus, each component of the 3-part version number ranges from 0 through 255.
Connections are completed only if the major version numbers match.
The *dy* 
parameter is O2_DY_INFO, O2_DY_HUB, O2_DY_REPLY, O2_DY_CALLBACK, 
O2_DY_CONNECT, or O2_DY_O2LITE, depending on the expected response.

`/_o2/hub ""` - requests the receiver to become the hub for the sender

`/_o2/cs/cs ""` - announces when clock sync is obtained.

`/_o2/cs/ps ""` - this message (short for "ping send") invokes the
sending of the `/_cs/get` message to request the time as part of the
clock synchronization protocol.

`/_o2/ds ""` - this message invokes the sending of discovery messages. It
is used with a timestamp to schedule periodic discovery message sending.

`/_cs/get "is" *serial-no* *reply-to* - send the time. The *reply-to*
parameter is the full address for the reply. The reply contains the type
string "it" and the parameters are the *serial-no* and the current time.

`/_o2/sv "ssbbsi..."` *process-name* *service-name* *add-flag*
*service-flag* *tappee-or-properties* *send_mode*... - reports service
creation or deletion. Each service is described by name, true, and
either true followed by a properties string and zero or false followed
by the tappee name and tap send_mode. If a service is deleted, then
false is sent rather than true, and if this is not a tap, the
properties string is empty. The "..."  notation here indicates that
there can be any number of services described in this message, each
service consisting of another "sbbsi" (string, Boolean, Boolean,
string, int32) sequence. Properties strings are sent with escaped
values, a leading ';' and a trailing ';'. This is the first message
sent when a process-to-process connection is made.


\subsection API Messages

`/_o2/cs/rt "s"` *reply-to* - A process can send this message to
request round trip (to the clock reference) information; *reply-to*
is the full address of the reply. The reply message has the type
string "sff" and the parameters are the process name (@public:internal:port),
the mean round-trip time, and the minimum round-trip time.

`/_o2/si "siss"` *service_name* *status* *process-name* *properties* -
Whenever an active service status changes or service properties
change, this message is sent to the local process. Note that when a
local service is created, an *internal* `/sv` message is sent to all
other processes, and when that process's services table is updated, if
the service is or becomes active, the change is reported locally to
the application by sending this `/_o2/si` message. Normally, you
should not add handlers or use the `_o2` service, but in this case, an
application is expected to add a custom handler to receive status
updates.
    See #o2_status for a list of status values. O2_UNKNOWN is reported
when the current provider of a service is removed.  This may be
followed immediately by another message to `/_o2/si` if another
provider offers the service.
    The service `_o2` is created before you can install a handler for
`/_o2/si`, so you will not receive an `si` message when `_o2` is
created.
     If the service name begins with '@', it represents a remote
process and the name has the form @public:internal:port, e.g.
`@c0a35801:80e8a1a5:d67e`.  There is also a
`@public:internal:port` service representing the local process, but
the local process is also represented by `_o2`, so the local
`@public:internal:port` service is never reported in an `/_o2/si`
message. Instead, the string `"_o2"` indicates that *process* is the
local one. You can get the local `@public:internal:port` string by
calling #o2_get_addresses or #o2_get_proc_name.
    The *properties* string contains the service properties in the
form "attr1:value1;attr2:value2;" with no leading ";". If there are no
properties, the string is empty. Values are escaped: within a value,
the characters ":;\" are preceded by "\" since ":" and ";" are
separator characters. Attribute names are alphanumeric.
*/

/** \defgroup returncodes Return Codes
 * @{
 */

/// \brief return values used generally by O2 functions
typedef enum {
    /// \brief function was successful
    //
    O2_SUCCESS = 0,

    /// \brief a non-specific error occurred.
    //
    /// In general, any return value < 0 indicates an error. Testing
    /// for only O2_FAIL will not detect more specific error return
    /// values such as O2_SERVICE_EXISTS, O2_NO_MEMORY, etc.
    O2_FAIL = -1,
        
    /// \brief not (re)creating service
    //
    /// attempt to create or delegate a local service when one exists
    /// already
    O2_SERVICE_EXISTS = -2,
    
    /// \brief path to handler specifies non-existant service
    //
    O2_NO_SERVICE = -3,
    
    /// process is out of free memory
    //
    O2_NO_MEMORY = -4,
    
    /// \brief #o2_initialize called, but O2 is already running.
    //
    O2_ALREADY_RUNNING = -5,
    
    /// \brief invalid ensemble name parameter
    //
    /// A service name was NULL or contained a slash (/) or was too
    /// long (#o2_service_new). The path was NULL or did not start
    /// with a slash or the service name did not start with a letter
    /// (#o2_method_new).  (Exception: #o2_method_new can be called
    /// with path "/_o2/si".)
    O2_BAD_NAME = -6,
    
    /// \brief in #o2_add_vector, invalid element type
    //
    O2_BAD_TYPE = -7,
    
    /// \brief mismatched types and arguments
    //
    /// returned by #o2_message_build, #o2_send, #o2_send_cmd
    O2_BAD_ARGS = -8,
    
    /// \brief in #o2_initialize, the socket is closed.
    //
    O2_TCP_HUP = -9,
    
    /// \brief `inet_pton()` failed to convert a string to an IP address
    //
    O2_HOSTNAME_TO_NETADDR_FAIL = -10,
    
    /// \brief attempt to make a TCP connection failed
    //
    O2_TCP_CONNECT_FAIL = -11,
    
    /// \brief timed message but time is unknown
    //
    /// Messages with non-zero timestamps are not scheduled or
    /// delivered until the current time is not available through the
    /// clock synchronization protocol.
    O2_NO_CLOCK = -12,
    
    /// \brief no handler for an address
    //
    O2_NO_HANDLER = -13,
    
    /// \brief an O2 message is invalid
    //
    O2_INVALID_MSG = -14,
    
    /// \brief could not write to socket or send datagram
    //
    O2_SEND_FAIL = -15,
    
    /// \brief SOCKET_ERROR in select call
    //
    O2_SOCKET_ERROR = -16,

    /// \brief O2 has not been initialized
    //
    O2_NOT_INITIALIZED = -17,
    
    /// \brief TCP send would block
    //
    /// O2 is holding the message locally to send later
    O2_BLOCKED = -18,
    
    /// \brief unable to allocate a discovery port
    //
    O2_NO_PORT = -19,

    /// \brief networking is disabled
    //
    /// Either a network connection was not found or no public IP IP
    /// address found.
    O2_NO_NETWORK = -20

} O2err;


/// \brief Status return codes for the #o2_status function.
typedef enum {
    /// \brief status is unknown, e.g. service does not exist
    //
    O2_UNKNOWN = -1,
    
    /// \brief local service, no clock sync yet.
    //
    /// This is a local service but clock sync has not yet been
    /// established so messages with non-zero timestamps will be
    /// dropped.
    O2_LOCAL_NOTIME = 0,

    /// \brief remote service but no clock sync yet
    //
    /// This is a remote service but clock sync has not yet been
    /// established so messages with non-zero timestamps will be
    /// dropped. The remote service may represent a bridge to a non-IP
    /// destination or to an OSC server.
    O2_REMOTE_NOTIME = 1,

    /// \brief service is connected but no
    ///    clock sync yet.
    //
    /// The service is attached to this process by a non-IP
    /// link. Clock sync has not yet been established between the
    /// reference clock and this process, so non-zero timestamped
    /// messages to this service will be dropped. Note that within
    /// other processes, the status for this service will be
    /// #O2_REMOTE_NOTIME rather than #O2_BRIDGE_NOTIME. Note also
    /// that O2 does not require the remote bridged process to have a
    /// synchronized clock, so "NOTIME" only means that *this* process
    /// is not synchronized and therefore cannot (and will not)
    /// schedule a timestamped message for timed delivery.
    O2_BRIDGE_NOTIME = 2,

    /// \brief service is connected but no
    ///    clock sync yet.
    //
    /// The service is local and forwards messages to an OSC
    /// server. The status of the OSC server is not reported by O2
    /// (and in the typical UDP case, there is no way to determine if
    /// the OSC server is operational, so "connected" may just mean
    /// that the service has been defined).  Clock sync has not yet
    /// been established between the reference clock and this process,
    /// so messages with non-zero timestamps to this service will be
    /// dropped. Note that within other processes, the status for this
    /// service will be #O2_REMOTE_NOTIME rather than
    /// #O2_TO_OSC_NOTIME. Note also that O2 does not require the OSC
    /// server to have a synchronized clock, so "NOTIME" only means
    /// that *this* process is not synchronized to O2 and therefore
    /// cannot (and will not) schedule a timestamped message for timed
    /// delivery.
    O2_TO_OSC_NOTIME = 3,

    /// \brief local service with clock sync.
    //
    /// Note that even though the service is local to the process and
    /// therefore shares a local clock, clocks are not considered to
    /// be synchronized until the local clock is synchronized to the
    /// reference clock. If this process provides the reference clock,
    /// it is considered to be synchronized immediately.
    O2_LOCAL = 4,

    /// \brief remote service with clock sync.
    //
    /// Messages with non-zero timestamps can be sent because clock
    /// sync has been established.
    O2_REMOTE = 5,
    
    /// \brief connected with clock sync.
    //
    /// The service is attached by a non-IP link, and this process is
    /// synchronized. If the bridged process is also synchronized,
    /// timed messages are sent immediately and dispatched according
    /// to the synchronized clock; if the bridged process is *not*
    /// synchronized, timed messages are scheduled locally and sent
    /// according to the timestamp, resulting in some added network latency.
    O2_BRIDGE = 6,
    
    /// \brief connected with clock sync.
    //
    /// The service forwards messages directly from the current process
    /// to an OSC server, and the process is synchronized. The status of
    /// the OSC server is not reported by O2 (and in the typical UDP case,
    /// there is no way to determine if the OSC server is operational).
    /// Non-bundle O2 messages will be scheduled locally and sent according
    /// to the timestamp to avoid creating a timestamped bundle, but this
    /// will result in some added network latency. O2 bundles will be
    /// converted to OSC bundles with timestamps based on Unix `gettimeofday()`
    /// or Windows `GetSystemTimeAsFileTime()` which are then converted to
    /// OSC-compatible NTP timestamps (this is all based on liblo; timestamped
    /// message to liblo implementations of OSC will be correctly interpreted).
    /// The resulting OSC bundles are sent immediately.
    O2_TO_OSC = 7,
    
    /// \brief tag value for #o2_services_list
    //
    O2_TAP = 8
} O2status;    

/** @} */


/** \defgroup debugging Debugging Support
 * @{
 */

/// \brief Enable debugging output.
///
/// Unless O2_NO_DEBUG is defined at compile time, O2 is
/// compiled with debugging code that prints information to
/// stdout, including network addresses, services discovered,
/// and clock synchronization status. Enable the debugging
/// information by calling #o2_debug_flags with a string
/// containing any of the following characters:
///   - a - enable all debug flags except m (malloc/free)
///   - A - enable all debug flags except malloc and scheduling
///   - B - trace shared memory bridge messages
///   - b - trace bridge setup and take-down
///   - c - print basic connection data
///   - d - trace discovery messages
///   - g - print general status info, not much detail
///   - h - trace hub-related activity
///   - I - disable Internet connections, e.g., do not acquire public IP.
///   - k - trace clock synchronization protocol
///   - l - trace messages to #o2_msg_deliver
///   - m - trace O2_MALLOC and O2_FREE calls
///   - n - enable all network flags (no malloc or scheduling): rRsS
///   - N - disable network if flags are set before o2_initialize() is
///         called. Internal IP becomes 127.0.0.1, public IP is
///         0.0.0.0 (signifying no Internet connection). Interprocess
///         communication on the host is supported.
///   - O - open sound control messages
///   - o - trace socket creating and closing
///   - p - trace tap messages and operations
///   - q - show MQTT messages
///   - R - trace system incoming messages
///   - r - trace non-system incoming messages
///   - S - trace system outgoing messages
///   - s - trace non-system outgoing messages
///   - t - trace user messages dispatched from schedulers
///   - T - trace system messages dispatched from schedulers
///   - W - trace all web page and socket binary data
///   - w - trace websockets activity
///   - z - trace Zeroconf (Bonjour, Avahi)
O2_EXPORT void o2_debug_flags(const char *flags);

/**
 *  \brief Return text representation of an O2 error
 *
 *  @param i error value returned from some O2 function
 *
 *  @return return the error code as a string
 */
const char *o2_error_to_string(O2err i);

/** @} */


// Macros for o2 protocol
/* an internal value, ignored in transmission but check against O2_MARKER in the
 * argument list. Used to do primitive bounds checking */
#define O2_MARKER_A (void *) 0xdeadbeefdeadbeefL
#define O2_MARKER_B (void *) 0xf00baa23f00baa23L
//#endif

/** \defgroup basics Basics
 * @{
 */

/** \brief Disable (or Enable) Internet connections.
 *
 * Set the default to enable or disable Internet connections to other
 * hosts. This setting can only be changed before O2 is started with
 * #o2_initialize() or between calls to #o2_finish() and
 * #o2_initialize.  When #o2_initialize() is called and network
 * connections are enabled, O2 will try to obtain a local (internal)
 * IP address. If found, O2 will then try to obtain a public IP
 * address. This may result in a long delay if the Internet cannot
 * be reached. See #o2_network_enable for more detail. If Internet
 * connections are disabled, the delay can be avoided and the 
 * public IP address is immediately set to 0.0.0.0. Even then, O2 
 * will still attempt to open Internet connections for OSC. OSC 
 * connections depend only on network connectivity and ignore the
 * #o2_internet_enable setting.
 *
 * When Internet connections are disabled, O2 processes can still
 * interconnect on the local area network. If an ensemble is expected
 * to run only within the local area network, Internet connections
 * (including possible security threats as well as some overhead in
 * finding the public IP address and setting up MQTT connections)
 * can be blocked using this option.
 *
 * Internet connections can also be disabled by passing 'I' as a
 * character in the string parameter of #o2_debug_flags.
 *
 * @param enable Use true to enable or false to disable Internet connections.
 *
 * @return O2_SUCCESS if setting is accepted, otherwise O2 is already 
 * running and O2_ALREADY_RUNNING is returned. If the network has been
 * disabled (see #o2_network_enable), O2_NO_NETWORK is returned.
 */
O2_EXPORT O2err o2_internet_enable(bool enable);

/** \brief Disable external nework connections.
 *
 * Set the default to enable or disable network connections to other
 * hosts.  This setting can only be changed before O2 is started with
 * #o2_initialize() or between calls to #o2_finish() and
 * #o2_initialize.  When #o2_initialize() is called and network
 * connections are enabled, O2 will try to obtain a local (internal)
 * IP address. If found, O2 will then try to obtain a public IP
 * address (assuming Internet connections are enabled, see 
 * #o2_internet_enable). At the conclusion of this 2-stage 
 * initialization, the process will receive an O2 name of the form
 * @public:internal:port. If a public IP address is not found, the 
 * public port is 0.0.0.0, which indicates no Internet
 * connection. If no local IP address is found, or if the only known
 * address is 127.0.0.1 (localhost), then the local IP address becomes
 * 127.0.0.1 with the public IP address 0.0.0.0. If the default for
 * network connections is set to false with #o2_network_enable,
 * initialization immediately sets the local IP to 127.0.0.1 and the
 * public IP to 0.0.0.0. In any case, if the local IP is 127.0.0.1,
 * then discovery messages are only sent to localhost, and no
 * discovery messages are broadcast. Even then, O2 can still open
 * connections for OSC. OSC connections depend only on network
 * connectivity and ignore the #o2_network_enable setting.
 * 
 * When the network is disabled, O2 processes can still interconnect
 * within the local host. If an ensemble is expected to run only within
 * local host processes, external connections (including possible
 * security threats) can be blocked using this option.
 *
 * Network access can also be disabled by passing 'N' as a
 * character in the string parameter of #o2_debug_flags.
 *
 * @param enable Use true to enable or false to disable networking.
 *
 * @return O2_SUCCESS if setting is accepted, otherwise O2 is already 
 * running and O2_ALREADY_RUNNING is returned.
 */
O2_EXPORT O2err o2_network_enable(bool enable);

/** \brief O2 timestamps are doubles representing seconds since the
 * approximate start time of the ensemble.
 */
typedef double O2time;

/** \brief data part of an O2 message
 *
 * This data type is used to pass o2 message data to message handlers.
 * It appears many other times in the code. You should NEVER allocate
 * or free an O2msg_data struct. Instead, create a message using 
 * #o2_send_start, `o2_add_*()`, and #o2_message_finish to get an 
 * O2message_ptr. Within the O2message, the data field is an
 * O2msg_data structure. We would use O2message everywhere instead
 * of O2msg_data, but bundles can contain multiple O2msg_data 
 * structures without the extra baggage contained in an O2message.
 */
typedef struct O2msg_data {
    int32_t length; // msg length, not including this length field
    // we could put a nice structure here, but alignment and network vs host
    // byte ordering is confusing, so we use an int: flags are low-order bits,
    // and ttl is (misc >> 8). Note: ttl starts at zero and is incremented
    // each time the message is copied and forwarded to a tap. The message
    // is not forwarded if the count exceeds 3 (O2_MAX_TAP_FORWARDING).
    int32_t misc; // flags and ttl
    O2time timestamp;   ///< the message delivery time (0 for immediate)
    /** \brief the message address string
     *
     * Although this field is declared as 4 bytes, actual messages
     * have variable length, and the address is followed by a
     * string of type codes and the actual parameters. The length
     * of the entire message including flags and timestamp, not including
     * the length field itself is given by the `length` field.
     */
    char address[];
} O2msg_data, *O2msg_data_ptr;


/** \brief get the type string from O2msg_data_ptr
 *
 * Type strings begin with the comma (",") character, which is skipped
 */
#define O2MEM_ALIGN 16
#define O2MEM_ALIGNUP(s) ( ((s)+(O2MEM_ALIGN-1)) & ~(O2MEM_ALIGN-1) )
#define O2MEM_BIT32_ALIGN_PTR(p) ((char *) (((size_t) (p)) & ~3))
// returns the address of the byte AFTER the message
#define O2_MSG_DATA_END(data) (PTR(&(data)->misc) + (data)->length)

// find the next word-aligned string after str in a message:
const char *o2_next_o2string(const char *str);
// find the type string from O2msg_data_ptr, skips initial ',':
#define o2_msg_data_types(data) (o2_next_o2string((data)->address) + 1)
#define o2_msg_types(msg) o2_msg_data_types(&msg->data)
// find the first parameter of the message, given the type string address
#define o2_msg_data_params(types) \
    o2_next_o2string((const char *) ((intptr_t) (types) & ~3))

/** \brief an O2 message container
 *
 * Note: This struct represents an O2 message that is stored on the heap.
 * O2message is an alias for O2netmsg. At the o2n (network)
 * abstraction, there is no O2msg_data type.
 *
 * Note that O2messages are on the heap and can be allocated, scheduled,
 * sent, and freed.  In contrast, O2msg_data structures are contained 
 * within O2messages and are passed to method handlers, but cannot be
 * allocated, scheduled, sent, or freed. They are always the data field
 * of a containing O2message.
 */
typedef struct O2message {
    union {
        struct O2message *next; ///< links used for free list and scheduler
        int64_t pad_if_needed;   ///< make sure allocated is 8-byte aligned
    };
    O2msg_data data;
} O2message, *O2message_ptr;

/// The address of the actual message, not including the length field:
#define O2_MSG_PAYLOAD(msg) PTR(&(msg)->data.misc)


/**
 *  \brief The structure for binary large object.
 *
 *  A blob can be passed in an O2 message using the 'b' type. Created
 *  by calls to #o2_blob_new.
 */
typedef struct O2blob {
  uint32_t size;  ///< size of data
  char data[4];   ///< the data, actually of variable length
} O2blob, *O2blob_ptr;


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
    O2_SYMBOL =    'S',     ///< Used to distinguish strings and symbols.
    O2_CHAR =      'c',     ///< 8bit char variable (Standard C).
    O2_MIDI =      'm',     ///< 4 byte MIDI packet.
    O2_TRUE =      'T',     ///< Symbol representing the value True.
    O2_FALSE =     'F',     ///< Symbol representing the value False.
    O2_NIL =       'N',     ///< Symbol representing the value Nil.
    O2_INFINITUM = 'I',     ///< Symbol representing the value Infinitum.
    
    // O2 types
    O2_BOOL =      'B',     ///< Boolean value returned as either 0 or 1
    O2_VECTOR =    'v',     ///< Prefix to indicate a vector
} O2type, *O2type_ptr;


/**
 * \brief union of all O2 parameter types
 *
 * An O2arg_ptr is a pointer to an O2 message argument. If argument
 * parsing is requested (by setting the parse parameter in o2_method_new),
 * then the handler receives an array of O2arg_ptrs. If argument parsing
 * is not requested, you have the option of parsing the message one
 * parameter at a time by calling #o2_get_next, which returns an
 * O2arg_ptr.
 *
 * The O2arg_ptr can then be dereferenced to obtain a value of the
 * expected type. For example, you could write
 * \code{.c}
 *     double d = o2_get_next(O2_DOUBLE)->d;
 * \endcode
 * to extract a parameter of type double. (This assumes that the message
 * is properly formed and the type string indicates that this parameter is
 * a double, or that type coercion was enabled by the coerce flag in
 * #o2_method_new.)
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
    O2time    t;    ///< TimeTag value.
    O2blob     b;    ///< a blob (unstructured bytes)
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
} O2arg, *O2arg_ptr;


O2_EXPORT O2arg_ptr o2_got_start_array;
O2_EXPORT O2arg_ptr o2_got_end_array;


/** \brief set this flag to stop #o2_run
 *
 * Some O2 processes will initialize and call #o2_run, which is a
 * simple loop that calls #o2_poll. To exit the loop, set
 * #o2_stop_flag to #true
 */
O2_EXPORT bool o2_stop_flag;

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
 * read-only. It is managed by O2 using #o2_initialize and #o2_finish.
 */
O2_EXPORT const char *o2_ensemble_name; // also used to detect initialization


/** \brief print an O2 message to stdout
 *
 * For debugging, this function will print a human-readable representation
 * of a message and its parameters.
 *
 * @param msg a message to be printed
 */
void o2_message_print(O2message_ptr msg);


/** \brief print a message from msg_data_ptr to stdout
 *
 * For debugging, this function will print a human-readable representation
 * of a message and its parameters.
 *
 * @param msg a message to be printed
 */
void o2_msg_data_print(O2msg_data_ptr msg);


/**
 * \typedef O2method_handler
 * \brief callback function to receive an O2 message
 *
 * @param msg The full message in host byte order.
 * @param types If you set a type string in your method creation call,
 *              then this type string is provided here. If you did not
 *              specify a string, types will be the type string from the
 *              message (without the initial ','). If parse_args and
 *              coerce_flag were set in the method creation call,
 *              types will match the types in argv, but not necessarily
 *              the type string or types in msg.
 * @param argv An array of #O2arg types containing the values, e.g. if the
 *             first argument of the incoming message is of type 'f' then
 *             the value will be found in argv[0]->f. (If parse_args was
 *             not set in the method creation call, argv will be NULL.)
 *             For vectors, specified in types by the sequence "vi", "vh", 
 *             "vf", or "vd", there will be one pointer in argv pointing to
 *             a vector description (the v field in O2arg). For arrays,
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
 *
 * Since the parameter list repeats in every handler implementation, the
 * macro `O2_HANDLER_ARGS` is provided to avoid some typing. Handlers can
 * be implemented in the form `void my_handler(O2_HANDLER_ARGS) { ... }`
 */
#define O2_HANDLER_ARGS const O2msg_data_ptr msg, const char *types, \
                              O2arg_ptr *argv, int argc, const void *user_data
typedef void (*O2method_handler)(O2_HANDLER_ARGS);

/**
 * \brief Start O2.
 *
 * If O2 has not been initialized, it is created and intialized.
 * O2 will begin to establish connections to other instances
 * with a matching ensemble name.
 *
 * @param ensemble_name the name of the ensemble. O2 will attempt to
 * discover other processes with a matching ensemble name,
 * ignoring all processes with non-matching names.
 *
 * @return #O2_SUCCESS if success, #O2_FAIL if an error occurs,
 * #O2_RUNNING if already running, #O2_BAD_NAME if `ensemble_name`
 * is NULL.
 */
O2_EXPORT O2err o2_initialize(const char *ensemble_name);


/**
 * \brief get O2 version number
 *
 * If version is not NULL, a version string is written in the form 
 * major.minor.patch, where fields are up to 3 digits. Thus, the length
 * of version must be at least 12 bytes (including EOS).
 *
 * @return integer representation, e.g. version 2.160.3 is represented by 
 *     0x02A003.
 */
O2_EXPORT int o2_version(char *version);


/**
 * \brief Tell O2 how to allocate/free memory.
 *
 * In many C library implementations, the standard implementation of
 * `free()` must lock a data structure. This can lead to priority
 * inversion if O2 runs at an elevated priority. Furthermore, the
 * standard `malloc()` and `free()` do not run in constant (real)
 * time. To avoid these problems, O2 uses it's own memory allocation
 * by default, or you can provide an alternate heap implementation for
 * O2 by calling this function before calling #o2_initialize. 
 *
 * The application should *always* use #O2_MALLOC, #O2_MALLOCT,
 * #O2_MALLOCNT, #O2_CALLOC, and #O2_CALLOCT functions, which add a
 * layer of debugging support and call either the default O2
 * `malloc()` and `free()` functions or the ones provided in this
 * call.
 * 
 * The default configuration corresponds to parameters: NULL, NULL,
 * NULL, 0, true. In other words, O2 will allocate chunks of heap
 * space using the C-library #malloc function as needed.
 *
 * #o2_memory can be called *one* time before the first call to
 * #o2_initialize. The configuration is retained and reused, including
 * #chunk and #size, even if O2 is restarted by calling #o2_finish
 * #followed by #o2_intialize.
 *
 * @param malloc a function pointer that behaves like standard
 *     `malloc()` or NULL to use O2's default memory allocator.
 * @param free a function pointer that behaves like standard
 *     `free()` or NULL to use O2's default memory free function.
 *     (The value should be NULL iff #malloc is NULL.)
 * @param first_chunk if #malloc and #free are NULL, #first_chunk
 *     provides a memory area from which O2 can allocate as
 *     needed. This value may be NULL. In non-NULL, #first_chunk is
 *     owned by O2 until the last call to #o2_finish, after which the
 *     caller should free it (or not -- it is possible to use a large
 *     static block of memory rather than the heap).
 * @param size is the size in bytes of #first_chunk. Specify 0 if
 *     #first_chunk is NULL.
 * @param mallocp if #malloc and #free are NULL, and O2 runs out of
 *     memory, either because #first_chunk is NULL to begin with or
 *     because it was not big enough to meet the allocation needs of
 *     O2, then O2 can be directed to use the C-library #malloc to
 *     allocate another chunk of memory by passing #true for #mallocp.
 * @return O2_SUCCESS if succeed, O2_FAIL if not.
 */
O2_EXPORT int o2_memory(void *((*malloc)(size_t size)), void ((*free)(void *)),
              char *first_chunk, int64_t size, bool mallocp);


/**
 * \brief Set discovery period
 *
 * O2 discovery messages are broadcast periodically in case a new process
 * has joined the ensemble. The default period is 4 seconds. If there
 * are N processes, each host will receive N/4 discovery messages per 
 * second. Since there are 16 discovery ports, a discovery message from
 * given process could be received every 64 seconds. (Note, however, that
 * new processes send more frequently, so if messages are not dropped,
 * discovery of new processes will happen much faster than the worst-case.)
 *
 * You can change the polling period from 4s by calling this function. The
 * new polling period takes effect when the next discovery message is sent
 * at the end of the current polling period.
 * 
 * However, discovery is limited to approximately 10 incoming
 * messages/second based on the number of known processes, so when more 
 * remote processes are discovered, the polling period may increase.
 *
 * @param period the requested polling period; a minimum of 0.1s is enforced; 
 *               4s is the default (recommended).
 *
 * @return the previous polling period
 */
O2_EXPORT O2time o2_set_discovery_period(O2time period);

#ifndef O2_NO_HUB
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
 * are not delivered across networks). To use a hub, you call #o2_hub
 * with the hub's IP address and port. All O2 processes are effectively
 * hubs with no clients, and #o2_hub simply connects to the hub as
 * a client. The hub will then send discovery messages for all current
 * and future O2 processes that are discovered, either through the 
 * normal discovery protocol or by connecting with the #o2_hub call.
 *
 * #o2_hub only works if there is an Internet connection, so connecting
 * through MQTT is likely to be a better solution.
 *
 * After #o2_hub is called, discovery broadcasting is stopped, so if
 * #o2_hub fails to connect to another process, you will only discover
 * more processes if they initiate the exchange. You can use #o2_hub
 * specifically to disable broadcast-based discovery by passing NULL
 * as the public_ip parameter.
 *
 * You can call #o2_hub multiple times, but the call may simply
 * return #O2_FAIL if #o2_hub is called more than once before a public
 * IP address is obtained. Each call potentially makes
 * a remote process become a hub for this local process. This might
 * result in duplicate messages when new processes join the O2 
 * ensemble, but duplicate messages are ignored.
 * 
 * @param version is the version number of of the hub, e.g. 0x20103 for 2.1.3
 * @param public_ip the public IP address of the hub or NULL
 * @param internal_ip the local IP address of the hub
 * @param tcp_port the port number of the hub's TCP port
 * @param udp_port the port number of the hub's UDP port
 *
 * @return #O2_SUCCESS if success, #O2_FAIL if not,
 *         #O2_NOT_INITIALIZED if O2 is not initialized.
 */
O2_EXPORT O2err o2_hub(int version, const char *public_ip, const char *internal_ip,
             int tcp_port, int udp_port);
#endif


/**
 * \brief Get IP address and TCP connection port number.
 *

 * IP addresses can be used to facilitate configuring OSC and HTTP
 * connections. Because O2 must query a STUN server to obtain the
 * public IP address, this function will return O2_FAIL for some time
 * after `o2_initialize` is called. Typically, the public IP address
 * will be available in less than 1 second, but the STUN calls will
 * continue for 10 seconds before O2 gives up and concludes that the
 * Internet is unreachable, in which case the public_ip is set to
 * 00000000 and this call returns O2_SUCCESS.
 *
 * Alternatively, to get the internal IP address only, pass in NULL
 * for `public_ip`. Neither `public_ip` nor `port` will be returned.
 *
 * If there is no network at all, the internal ip is 7f000001 (localhost)
 * and O2 will still operate, connecting to other O2 processes on the
 * same host.
 * 

 * @param public_ip is NULL or the address of a pointer that will be
 * set to either NULL (on failure) or a string of the form
 * "80100a06". The string should not be modified, and the string will
 * be freed by O2 if #o2_finish is called. If the parameter is NULL
 * (not a pointer to a pointer), no public IP will be returned.
 *
 * @param internal_ip is either NULL or a pointer that will be set to
 * either NULL (on failure) or a string of the form "c0000006". The
 * string should not be modified, and the string will be freed by O2
 * if #o2_finish is called.
 *
 * @param port is either NULL or a pointer to a port number that will
 * be set to the O2 TCP connection port (or 0 on failure).
 *
 * @return #O2_SUCCESS if success, #O2_FAIL if not. Failure will 
 * occur if public_ip is non-NULL and the STUN protocol has not yet
 * completed. Failure will occur if #o2_initialize() was not called.
 */
O2_EXPORT O2err o2_get_addresses(const char **public_ip, 
                                 const char **internal_ip, int *port);

/**
 * \brief Get Public:Local:Port service name string.
 *
 * An O2 process is identified by its IP addresses and Port number.
 * An O2 process is a special service name that is automatically
 * created when O2 is initialized. For example,
 * "@6a960032:c0a801b6:55630".
 * By convention, you should always use "_o2" instead. This is an
 * alias for the local process. The full @public:local:port string for a
 * process is used, for example, in status messages ("/_o2/si") to
 * identify a remote process that is providing the service.
 *
 * @return pointer to the Public:Local:Port string. The result will be NULL if
 *         O2 is unable to obtain the host's IP address or unable to
 *         obtain a TCP server port.
 */
const char *o2_get_proc_name(void);

/**
 * \brief Parse Public:Local:Port string and extract fields
 *
 * @param name the full PublicIP:LocalIP:PortNumber string
 *
 * @param public_ip address where public_ip is written, at least O2N_IP_LEN long
 *
 * @param internal_ip address where internal_ip is written, at least O2N_IP_LEN long
 *
 * @param port address where the port number is written
 *
 *  @return O2_SUCCESS if all parameters were written, otherwise O2_FAIL
 */
int o2_parse_name(const char *name, char *public_ip,
                  char *internal_ip, int *port);
    
/**
 *  \brief Add a service to the current process.
 *
 * Once created, services are "advertised" to other processes with
 * matching ensemble names, and messages are delivered
 * accordingly. E.g. to handle messages addressed to "/synth/volume"
 * you call
 * \code{.c}
 * o2_service_new("synth");
 * o2_method_new("/synth/volume", "f", synth_volume_handler, NULL, NULL, true);
 * \endcode
 * and define `synth_volume_handler` (see the type declaration for
 * #O2method_handler and #o2_method_new)
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
O2_EXPORT O2err o2_service_new(const char *service_name);


/**
 * \brief list known services and taps
 *
 * Currently active services and taps can be queried by calling
 * #o2_services_list. An internal snapshot of services and
 * taps is saved. Information can then be accessed by calling
 * #o2_service_name, #o2_service_type, #o2_service_process, 
 * #o2_service_tapper, and #o2_service_properties. When the 
 * information is no longer needed, call #o2_services_list_free.
 * 
 * Only active services and their tappers are reported. If there are
 * two services with the same name, only the active one is reported.
 * Taps on active services are reported even if the tapper does not
 * exist.
 *
 * @return #O2_SUCCESS if success, #O2_FAIL if not.
 */
O2_EXPORT O2err o2_services_list(void);


/**
 * \brief free the list of known services and taps
 *
 * Call this function when the information captured by 
 * #o2_services_list is no longer needed.
 *
 * @return #O2_SUCCESS if success, #O2_FAIL if not.
 */
O2_EXPORT O2err o2_services_list_free(void);


/**
 * \brief get a service name from a saved list of services
 *
 * See #o2_services_list. Do not free the returned value. 
 * Instead, call #o2_services_list_free.
 * The pointer will be invalid after calling #o2_services_list_free.
 *
 * @param i the index of the service, starting with zero
 *
 * @return a service name if #i is in range, otherwise NULL.
 *         if #i-th service is a tap, then the name returned
 *         is the name of the *tapped* service.
 */
O2_EXPORT const char *o2_service_name(int i);


/**
 * \brief get a type from a saved list of services
 *
 * See #o2_services_list. The return value indicates the type of the
 * service: O2_LOCAL (4) if the service is local, O2_REMOTE (5) if the
 * service is remote, O2_BRIDGE (6) if the service is a bridge to an
 * o2lite, websocket or shared memory process (or some other bridge
 * protocol), O2_TO_OSC (7) if the service delegates to an OSC server,
 * and O2_TAP (8) for each tapper of the service.
 *
 * @param i the index of the service, starting with zero
 *
 * @return a service type, or O2_FAIL if #i is not in range.
 */
O2_EXPORT int o2_service_type(int i);


/**
 * \brief get a process name from a saved list of services
 *
 * See #o2_services_list. Do not free the returned value. 
 * Instead, call #o2_services_list_free.
 * The pointer will be invalid after calling #o2_services_list_free.
 *
 * @param i the index of the service
 *
 * @return a process name if #i is in range, otherwise NULL. The
 *         process name contains the IP address and TCP port number
 *         of the process, making it a unique identifier. If the
 *         #i-th service is a tap, the returned value is the 
 *         tapping process name.
 */
O2_EXPORT const char *o2_service_process(int i);


/**
 * \brief get a tapper name from a saved list of services
 *
 * See #o2_services_list. Do not free the returned value. 
 * Instead, call #o2_services_list_free.
 * The pointer will be invalid after calling #o2_services_list_free.
 *
 * @param i the index of the service
 *
 * @return if the #i-th service is a tap, return the service name 
 *         to which tapped messages are sent, otherwise NULL.
 */
O2_EXPORT const char *o2_service_tapper(int i);


/**
 * \brief get the properties string from a saved list of services
 *
 * See #o2_services_list. Properties have the form: 
 * "attr1:value1;attr2:value2;...", where
 * attributes are alphanumeric, and values can be any string with
 * colon represented by "\:", semicolon represented by "\;", and 
 * slash represented by "\\". Escape characters are not removed, and
 * the result should not be modified or freed. Properties end in ";".
 *
 * Do not free the returned value. Instead, call #o2_services_list_free.
 * The pointer will be invalid after calling #o2_services_list_free.
 *
 * @param i the index of the service
 *
 * @return a properties string if #i is in range and the #i-th
 *         service is not a tap, otherwise NULL. The string may
 *         be empty if the service has no properties.
 */
O2_EXPORT const char *o2_service_properties(int i);


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
O2_EXPORT const char *o2_service_getprop(int i, const char *attr);

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
 *        '\' itself must also be escaped, so to search for an exact match
 *        to the value "x;y", escape ';' to get the 4 character
 *        string denoted in C by "x\\;y", then add ':' and ';' to indicate
 *        an exact match: ":x\\;y;")
 *
 * @return the index of the first service (at index i or above) where
 *       the property named by attr contains value as a substring or
 *       prefix or suffix, as indicated by ":" and ";" characters.
 *       If no match is found, return -1.
 */
O2_EXPORT int o2_service_search(int i, const char *attr, const char *value);


/**
 * \brief set an attribute and value property for a service
 *
 * @param service the name of a service offered by this process
 *
 * @param attr the attribute name
 * 
 * @param value the value string; this string will be escaped. Do
 *        not include escape characters in #value
 *
 * @returns O2_SUCCESS if successful. O2_FAIL otherwise, for example
 *          if the service is not offered by this process.
 *
 * Note that each call will broadcast the property change to every
 * other O2 process in the ensemble. Therefore properties are not
 * recommended for publishing values frequently to clients, expecially
 * if multiple properties are typically updated in sequence, e.g.
 * X, Y, Z coordinates, which would result in 3 messages to each 
 * other process. Consider sending X, Y, Z together in a normal O2
 * message, and consider using taps if the "publisher" does not know
 * all the "subscribers."
 * Also note that properties can be written by multiple service 
 * providers, all offering services with the same name, but properties
 * are only readable from the current service provider. If the current
 * service provider changes to a new process, there can be temporary
 * inconsistent views of service properties across the O2 ensemble.
 */
O2_EXPORT O2err o2_service_set_property(const char *service, const char *attr,
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
 * @param attr the attribute name
 * 
 * @returns O2_SUCCESS if successful
 */
O2_EXPORT O2err o2_service_property_free(const char *service, const char *attr);

typedef enum {TAP_KEEP, TAP_RELIABLE, TAP_BEST_EFFORT} O2tap_send_mode;

/**
 * \brief install tap to copy messages from one service to another
 *
 * @param tappee the service to be tapped
 *
 * @param tapper the existing local service to which copies are sent
 *
 * @param send_mode Send the tap message using the same method as the original
 *    message  with TAP_KEEP, by reliable (TCP) method with TAP_RELIABLE, or by
 *    best effort (UDP) method with TAP_BEST_EFFORT.
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
 * Taps can be used to implement a publish/subscribe
 * model. The publisher creates a local service and need not install
 * any message handlers. Subscribers install taps on the service to
 * receive messages. It is more efficient for the tappee to be in the
 * publisher process, but actually any process can publish to the
 * service with the added cost of sending a message to the tappee's
 * process.
 *
 * Messages with timestamps are held according to their timestamps.
 * Tappers receive the messages *after* the actual message dispatch.
 * Therefore, publish/subscribe using taps cannot achieve precise
 * timing with timestamps. (The recommended approach to timed 
 * publish/subscribe is to use properties: A service subscribing to
 * X sets a property such as "X-subscribe:yes". Publishers for X
 * periodically search for services with the ";X-subscribe:" attribute
 * with value "yes" and (re)build a subscriber list. To publish, the 
 * publisher explicitly sends a timed message to each subscriber.)
 *
 * Bundles are first unbundled before delivery, so tappers will not
 * receive bundles. Note that bundled messages each contain a timestamp
 * and service name which might differ from those of the bundle, so 
 * tapping is only applied individually to each bundled message when it
 * is actually delivered.
 *
 * While services are normally independent of processes (for example,
 * a new service can override an existing one in another process), 
 * tappers are tied to processes and cannot be overridden by 
 * another service of the same name. It is not redirected to 
 * another service provider. Also, unlike ordinary message delivery
 * that delivers one message even if there are multiple processes 
 * offering the service, there can be multiple tappers, even with the
 * same service names, and each tapper receives a copy of every message
 * delivered to the tappee.
 *
 * The lifetime of a tap is independent of the lifetimes of the tappee
 * and the tapper, but the tap is tied to a process, so if the process
 * is terminated, the tap is destroyed throughout the O2 ensemble. For
 * example, if the tappee's process crashes and restarts, and if the
 * tapper belongs to another process that survives, then the tap will
 * be reinstated on the tappee. Similarly, a tap may be created *before*
 * the tappee or the tapper. Calling #o2_service_free on a tapper will
 * *not* free the tap, so tap messages will continue to be delivered to
 * the tap's process, and if the tappper service is (re)created, these
 * tap messages will be delivered to the new tapper service.
 */
O2_EXPORT O2err o2_tap(const char *tappee, const char *tapper, O2tap_send_mode send_mode);


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
O2_EXPORT O2err o2_untap(const char *tappee, const char *tapper);


/**
 *  \brief Remove a local service
 *
 * The #service_name corresponds to the parameter previously passed to
 * #o2_service_new or #o2_osc_delegate. Note that if an OSC port 
 * forwards to this service (see #o2_osc_port_new), the port remains
 * open, but the OSC messages will be dropped. See #o2_osc_port_free.
 *
 * @param service_name the name of the service
 * 
 * @return #O2_SUCCSS if success, #O2_FAIL if not.
 */
O2_EXPORT O2err o2_service_free(const char *service_name);


/**
 * \brief Add a handler for an address.
 *
 * @param path      the address including the service name. 
 * @param typespec  the types of parameters, use "" for no parameters and
 *                      NULL for no type checking
 * @param h         the handler
 * @param user_data pointer saved and passed to handler
 * @param coerce    is true if you want to allow automatic coercion of types.
 *                      `coerce` is ignored if `parse` is false.
 * @param parse     is true if you want O2 to construct an argv argument
 *                      vector to pass to the handle
 *
 * @return O2_SUCCESS if succeed, O2_FAIL if not.
 *
 * If the address is only the service name with no trailing slash, the 
 * handler will match *any* message to the service. Pay attention to the 
 * parse flag -- if true and types do not match, the message will not be
 * delivered.
 *
 * Addresses should not conflict: An address should not match another 
 * address, and for every pair of addresses X and Y, X/ should not be a
 * prefix of Y. Otherwise, the last handler added will remove all
 * conflicting handlers.
 * 
 * A handler for /W/X will not handle children at /W/X/Y and /W/X/Z, and
 * in fact causes messages to /W/X/Y and /W/X/Z to be ignored. The only
 * way to receive multiples addresses with one handler is to handle /W,
 * i.e. to install a handler for just the service.
 */
O2err o2_method_new(const char *path, const char *typespec,
                    O2method_handler h, const void *user_data,
                    bool coerce, bool parse);


/**
 * \brief remove a path -- remove a path and associated handler
 *
 * To remove a handler, call this function to remove it from the
 * handler lookup structure. You can also remove a subtree of
 * handlers, e.g. if path is /W/X, it will remove any handlers on
 * paths starting with /W/X, e.g. /W/X/Y and /W/X/Z.
 *
 *  @param path The path of the method
 *
 *  @return If success, return O2_SUCCESS. If not, return O2_FAIL.
 */
O2err o2_method_free(const char *path);


/**
 * \brief Default dropped message alert.
 *
 * @param warn     A human-readable description of the cause.
 * @param msg      a pointer to the message data to be dropped
 *
 * This default parameter for #o2_message_warnings prints the `warn`
 * string followed by the message to be dropped (if O2_NO_DEBUG is
 * undefined) or the message address and type string (if O2_NO_DEBUG
 * is defined) to stdout. Do not call this function. Use 
 * #o2_drop_message or #o2_drop_msg_data instead. You can pass this
 * function to #o2_message_warnings to restore default warning behavior.
 */
void o2_message_drop_warning(const char *warn, O2msg_data_ptr msg);

/**
 * \brief Tell world that a message was dropped.
 *
 * @param warn     A human-readable description of the cause.
 * @param free_the_msg      if true, frees the message
 *
 * Applications and libraries can call this function to report dropped
 * messages in the same manner as O2. The default behavior is to call
 * #o2_message_drop_warning or another handler installed by a previous
 * call to #o2_message_warnings. This function assumes the message is
 * at the head of the list o2_ctx->msgs, so you can access it wtih
 * #o2_current_message() and aquire ownership with #o2_postpone_delivery().
 */
O2_EXPORT void o2_drop_message(const char *warn, bool free_the_msg);

/**
 * \brief Enable/Disable warnings for dropped messages.
 *
 * @param warning   a function such as o2_message_drop_warning to 
 *                  issue the warning or NULL.
 *
 * It can be very annoying when O2 messages are not received due to
 * an error in setting up a message handler with #o2_method_new.
 * Therefore, the default behavior is to print a message whenever an
 * O2 message is dropped. (Exceptions: messages addressed to "!_o2/si"
 * but not handled are dropped without warning. This  address is 
 * described under "API Messages." Undelivered messages when O2 is 
 * shut down with #o2_finish are silently deleted.) The default warning 
 * behavior is to print a warning to stdout, but since that does not 
 * always exist, you can call this function to install a custom handler.
 *
 * Call #o2_message_warnings(NULL) to disable any warnings.
 *
 * The #warning function, if any, must not free #msg, which is
 * owned and eventually freed by the caller.
 *
 * No warning is printed if a message is lost by the network or 
 * queued for network delivery when the receiver closes the 
 * socket or loses its connection. Also, UDP messages have no
 * acknowledgements, so there is no way to warn if a UDP message
 * from O2 is intended for an OSC server that does not even exist.
 * With O2, however, failed or non-existent remote services are
 * detected (eventually), resulting in warnings even for UDP messages.
 */
O2_EXPORT void o2_message_warnings(
        void (*warning)(const char *warn, O2msg_data_ptr msg));


/**
 *  \brief Process current O2 messages.
 *
 *  Since O2 does not create a thread and O2 requires active processing
 *  to establish and maintain connections, the O2 programmer (user)
 *  should call #o2_poll periodically, even if not offering a service.
 *  #o2_poll runs a discovery protocol to find and connect to other
 *  processes, runs a clock synchronization protocol to establish valid
 *  time stamps, and handles incoming messages to all services. #o2_poll
 *  should be called at least 10 times per second. Messages can only be
 *  delivered during a call to #o2_poll so more frequent calls will
 *  generally lower the message latency as well as the accuracy of the
 *  clock synchronization (at the cost of greater CPU utilization).
 *  Human perception of timing jitter is on the order of 10ms, so
 *  polling rates of 200 to 1000 are advised in situations where
 *  rhythmic accuracy is expected.
 *
 *  @return 0 (O2_SUCCESS) if succeed, -1 (O2_FAIL) if not.
 */
O2_EXPORT O2err o2_poll(void);

/**
 * \brief Run O2.
 *
 * Call #o2_poll at the rate (in Hz) indicated.
 * Returns if a handler sets #o2_stop_flag to non-zero.
 */
O2_EXPORT int o2_run(int rate);

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
 *  @return service status code.
 *
 * Note that codes are carefully ordered to allow testing for categories:
 * - to test if delivery is possible with a zero (immediate) timestamp,
 * use `o2_status(service) > O2_FAIL`, `o2_status(service) >= 0`, or
 * `o2_status(service) >= O2_LOCAL_NOTIME`.
 * - to test if delivery is possible with a non-zero timestamp, use
 * `o2_status(service) >= O2_LOCAL`. Note that status can change over
 * time, e.g. the status of a remote service will be #O2_UNKNOWN until
 * the service is discovered. It will then change to #O2_REMOTE_NOTIME
 * until both the sender and receiver achieve clock synchronization
 * and share their synchronized status, and finally the status will
 * become #O2_REMOTE.
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
 * When the status of a service changes, a message is sent with
 * address `!_o2/si`. The type string is "sis" and the parameters are
 * (1) the service name, (2) the new status, and (3) the
 * @public:internal:port string of the process that offers (or
 * offered) the service.
 */
O2_EXPORT int o2_status(const char *service);


#ifndef O2_NO_DEBUG
/**
 * \brief retrieve text version of an O2status
 *
 * @param status a status code
 * 
 * @return human-readable string representation of status
 *
 * note that the parameter is of type #O2status while #o2_status returns
 * int. Therefore #o2_status_to_string(#o2_status("service")) is an
 * invalid conversion from int to #O2status. This is because #o2_status
 * can also return an #O2err (a negative value). You can pass an error
 * value to #o2_status_to_string and it will return "O2_FAIL" (rather
 * than a specific error description). Normally, you will retrieve an int
 * from #o2_status and call #o2_status_to_string((#O2status) stat).
 */
const char *o2_status_to_string(int status);
#endif


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
 * This can lead to deadlock if processes block trying to send to each 
 * other. If necessary for responsiveness or deadlock-avoidance, the 
 * sender should call #o2_can_send before calling #o2_send_cmd. 
 * If #O2_BLOCKED is returned, the caller should defer the call to
 * #o2_send_cmd but continue calling #o2_poll, and at some point 
 * in the future, provided the receiver is receiving, #o2_can_send
 * should return #O2_SUCCESS.
 *
 * This works if `service` forwards messages locally to OSC via TCP. 
 * However, if `service` is provided by another process, and *that*
 * process forwards messages to an OSC server via TCP, then the
 * return value indicates whether the first hop to the remote process
 * will block. Once the message arrives at the remote process, the
 * hop to the OSC server might be blocked, in which case the *remote*
 * process will block. This could create any number of problems. It
 * is safer to send to the service using #o2_send (UDP). In that 
 * case, the remote process will drop the message rather than block.
 * An even better option might be to send directly to the OSC server
 * by creating a local service for it (see #o2_osc_delegate). In 
 * order for two processes to connect to the same OSC server, 
 * *it is necessary to use different service names*: one for each
 * sending process. Otherwise, one process will take priority and all
 * messages will will flow through that process. If it is not 
 * possible for all processes to connect directly to the OSC server,
 * and if blocking must be avoided, the most general solution is to
 * create an O2 service to receive messages and resend them to OSC.
 * The receiver can then detect blocking using #o2_can_send and 
 * implement any policy or filtering desired.
 */
O2_EXPORT O2err o2_can_send(const char *service);


/**
 * \brief A variable indicating that the clock is the reference or is
 *        synchronized to the reference.
 */
O2_EXPORT bool o2_clock_is_synchronized;

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
 * sending a message to `!@public:internal:port/cs/rt`, where `@public:internal:port` is the
 * @public:internal:port string for a process. (One way to get this is to call
 * `o2_get_addresses` and construct a @public:internal:port process name from
 * the information returned. But then you can just call 
 * `o2_roundtrip` for the local process round trip information.
 * To get remote process names, you can create a handler for 
 * `/_o2/si`. The process name is provided whenever one of its
 * services is created or otherwise changes status.) The 
 * type string for `!@public:internal:port/cs/rt` is "s", and the parameter is
 * an O2 address. When the message is received, a reply is
 * sent to the address with the type string "sff",
 * and the parameters are (1) the process @public:internal:port name, (2) the
 * mean of recent round trip times to the reference clock, and
 * (3) the minimum of recent round trip times. (The clock is set
 * using the minimum, so this number is an upper bound on the
 * clock skew for this process.
 */
O2_EXPORT int o2_roundtrip(double *mean, double *min);


/** \brief signature for callback that defines the reference clock
 *
 * See #o2_clock_set for details.
 */
typedef O2time (*o2_time_callback)(void *rock);


/** \brief signature for callback that handles reference clock jumps
 *
 * The callback parameters are the local time at which the jump occurs,
 * the global time before the jump, and the global time after the jump.
 * If actions are taken (and in particular if the reference time
 * computation is defined by calling #o2_clock_jump, return true. Return
 * false to get the default O2 behavior. See also #o2_time_jump_callback_set.
 */
typedef bool (*o2_time_jump_callback)(double local_time, 
                       double old_global_time, double new_global_time);

/**
 *  \brief Provide a time reference to O2.
 *
 *  Exactly one process per O2 ensemble should provide a reference
 *  clock. All other processes synchronize to the reference. To become
 *  the reference, call #o2_clock_set. 
 *
 *  The time reported by the gettime function will be offset to 
 *  match the current local time so that local time continues to 
 *  increase smoothly. You cannot force O2 time to match an external 
 *  absolute time, but once #o2_clock_set is called, the difference
 *  between the time reference and O2's local time (as reported by 
 *  #o2_local_time) will be fixed.
 *
 *  @param gettime function to get the time in units of seconds. The
 *  reference may be operating system time, audio system time, MIDI
 *  system time, or any other time source. The times returned by this
 *  function must be non-decreasing and must increase by one second
 *  per second of real time to close approximation. The value may be
 *  NULL, in which case a default time reference will be used.
 *
 *  @param rock an arbitrary value that is passed to the gettime
 *  function. This may be need to provide context. Use NULL if no
 *  context is required.
 *
 *  @return #O2_SUCCESS if success, #O2_FAIL if not.
 */
O2_EXPORT O2err o2_clock_set(o2_time_callback gettime, void *rock);


/**
 * \brief Override clock synchronization behavior.
 *
 * After synchronization to a remote clock reference, if the local
 * expected time (#o2_time_get) is off by more than 1 second, and
 * if the last call to #o2_time_jump_callback_set provided a non-NULL
 * function pointer, the function is called to implement
 * application-specific actions before the clock is adjusted. See
 * also #o2_time_jump_callback. Actions by the callback could include
 * #o2_clock_jump and #o2_sched_flush. If no actions are taken but true
 * is returned, the mapping from local to global time used by
 * #o2_time_get is not altered.
 *
 * @param callback a function to be called when #o2_time_get is
 * discovered to be off by more than 1 second.
 *
 * @return #O2_SUCESS
 */
O2_EXPORT O2err o2_time_jump_callback_set(o2_time_jump_callback callback);


/**
 * \brief Cause #o2_time_get to return the best estimate of global O2 time
 *
 * If the estimated reference clock and #o2_time_get differ by more
 * than a second, which means that it will take more than 10 seconds
 * to synchronize by speeding up or slowing down by 10%, and if a time
 * jump handler is defined (see #o2_time_jump_callback_set), then the
 * callback is called to indicate time has "jumped." The application
 * can optionally call #o2_clock_jump to immediately synchronize
 * #o2_time_get to the latest estimate of the O2 reference time, with
 * no regard for continuity or monotonicity, i.e. time can jump backward.
 * See also #o2_sched_flush. This function need not be called only
 * from within the callback function.
 *
 * @param local_time is the current local time
 *
 * @param global_time is the estimated and desired global O2 time
 *
 * @param adjust will offset the timestamps in all pending messages in
 * #o2_gtsched to maintain their schedule relative to wall time. E.g.,
 * if #o2_time_get time is being set earlier by 60 seconds, that would
 * imply that messages will be delayed by an extra 60 seconds of time
 * in the real world. If #adjust is true, timestamps will be decremented
 * by 60 seconds to compensate.
 */
O2_EXPORT O2err o2_clock_jump(double local_time, double global_time,
                              bool adjust);


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
 *  indicates one data item. Allowed type characters are those in 
 *  "ifsbhtdScmTFNIB". Threee O2 type codes: "[]v" are not supported.
 *  (if needed, create a message and use the appropriate o2_add_...
 *  function.)
 *  @param ...  the data of the message. There is one parameter for each
 *  character in the typestring.
 *
 *  @return #O2_SUCCESS if success. See #o2_send_finish for details.
 *
 */
/** \hideinitializer */ // turn off Doxygen report on #o2_send_marker
#define o2_send(path, time, ...)         \
    o2_send_marker(path, time, false,    \
                   __VA_ARGS__, O2_MARKER_A, O2_MARKER_B)

/** \cond INTERNAL */ \
O2_EXPORT O2err o2_send_marker(const char *path, double time, bool tcp_flag,
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
 *  one message is waiting to be sent, #o2_can_send will return 
 *  #O2_BLOCKED. In this state, #o2_send_cmd will block until the
 *  pending message can be sent. Then #o2_send_cmd will try again,
 *  possibly re-entering the #O2_BLOCKED state with the new message.
 *
 *  @param path an address pattern
 *  @param time when to dispatch the message, 0 means right now. In any
 *  case, the message is sent to the receiving service as soon as
 *  possible. If the message arrives early, it will be held at the
 *  service and dispatched as soon as possible after the indicated time.
 *  @param typestring the type string for the message. Each character
 *  indicates one data item. Type codes are defined by #O2type.
 *  @param ...  the data of the message. There is one parameter for each
 *  character in the typestring.
 *
 *  @return #O2_SUCCESS if success. See #o2_send_finish for details.
 *
 */
/** \hideinitializer */ // turn off Doxygen report on #o2_send_marker
#define o2_send_cmd(path, time, ...) \
    o2_send_marker(path, time, true, \
                   __VA_ARGS__, O2_MARKER_A, O2_MARKER_B)


/**
 * \brief Send an O2 message. (See also macros #o2_send and #o2_send_cmd).
 *
 * @param msg points to an O2 message.
 *
 *  @return #O2_SUCCESS if success, #O2_FAIL if not.
 *
 * After the call, the `msg` parameter is "owned" by O2, which will
 * free it. Therefore, do *not* free msg after calling #o2_message_send.
 */
O2_EXPORT O2err o2_message_send(O2message_ptr msg);

/**
 * \brief Get the estimated synchronized global O2 time.
 *
 * This function returns a valid value either after you call
 * #o2_clock_set, making the local clock the reference clock for
 * the O2 ensemble, or after O2 has finished discovering and
 * synchronizing with the reference clock. Until then, -1 is returned.
 *
 * The clock accuracy depends upon network latency, how often
 * #o2_poll is called, and other factors, and some smoothing and 
 * adjustments are made to insure a continuous monotonic non-decreasing
 * time. In particular, reported time will speed up or down by 10% to
 * smoothly adjust to new estimates of the global O2 time when differences
 * are less than 1 sec. Larger differences either cause #o2_time_get times
 * to jump ahead to catch up, or stop advancing until O2 time catches up
 * to #o2_time_get time. (This can happen if the `_cs` service is restarted
 * and provides a new reference time.) See also #o2_clock_jump.
 *
 *  @return the time in seconds, or -1 if global (reference) time is unknown.
 */
O2_EXPORT O2time o2_time_get(void);


/**
 * \brief Get the real time using the local O2 clock.
 *
 * The local O2 clock source may be specified by #o2_clock_set, and
 * defaults to #o2_native_time(). Regardless of the clock source, O2
 * may add an offset to minimize time discontinuities. For example, if
 * another system provided the reference clock and then becomes
 * disconnected, #o2_local_time() takes over, but its source of time
 * is offset to match the time of the disconnected reference.
 *
 * @return the local time in seconds
 */
O2_EXPORT O2time o2_local_time(void);

/**
 * \brief Get the local real time from the operating system.
 *
 * This is the default source of time for #o2_local_time(). This time starts
 * at zero when O2 is initialized.
 *
 * @return the local system time in seconds
 */
O2_EXPORT O2time o2_native_time(void);


/**
 *  \brief release the memory and shut down O2.
 *
 *  Close all sockets, free all memory, and restore critical
 *  variables so that O2 behaves as if it was never initialized.
 *
 *  Note that #o2_finish will close websockets immediately
 *  without performing the standard websocket close
 *  protocol. The recommended clean shutdown with
 *  o2lite running over websockets is: If the O2 host is to
 *  shut down, send a message from the browser to the O2
 *  host (see `test/websockhost.cpp` and its `stop_handler`
 *  function for example.) Then call `o2ws_finish()` in the
 *  browser. If the O2 host is shutting down, have it call
 *  #o2_poll() for a 100 ms or so, giving O2 a chance to
 *  respond to the incoming websocket CLOSE command.
 *  This will complete the shutdown on the browser side,
 *  avoiding an exception there. Then, the O2 host can call
 *  #o2_finish to close all sockets and free resources.
 *
 *  @return #O2_SUCCESS if success, #O2_FAIL if not.
 */
O2_EXPORT O2err o2_finish(void);

/** @} */ // end of Basics


#ifndef O2_NO_OSC
/** \defgroup oscinterop OSC Interoperation
 * @{
 */

/**
 *  \brief Create a port to receive OSC messages.
 *
 *  OSC messages are converted to O2 messages and directed to the
 *  service.  E.g. if the service is "maxmsp" and the message address
 *  is `/foo/x`, then the message is directed to and handled by
 *  `/maxmsp/foo/x`. If the #service_name does not exist at any time
 *  after calling #o2_osc_port_new, incoming OSC messages will be
 *  dropped until the service is available again. Note that this
 *  function does not automatically create a service named
 *  `service_name`.
 *
 *  @param service_name The name of the service to which messages are delivered
 *  @param port_num     Port number.
 *  @param tcp_flag     Be a TCP server for remote clients. Otherwise, use UDP
 *
 *  @return #O2_SUCCESS if success, #O2_FAIL if not.
 */
O2_EXPORT O2err o2_osc_port_new(const char *service_name, int port_num,
                                bool tcp_flag);

/**
 * \brief Remove a port receiving OSC messages.
 *
 * This removes a port created by #o2_osc_port_new. If you want to
 * remove the corresponding service, you must also call
 * #o2_service_free with the service name. The port should be the same
 * port passed to #o2_osc_port_new. In the case of TCP, this will
 * close all connections that were *accepted* from this server port as
 * well as the server port.
 *
 * @param port_num The port number that receives OSC messages or that receives
          connect requests for TCP connections.
 * 
 * @return #O2_SUCCESS if success, #O2_FAIL if not.
 *
 */
O2_EXPORT O2err o2_osc_port_free(int port_num);


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
 * If this is a tcp connection, close it by calling #o2_service_free.
 */
O2_EXPORT O2err o2_osc_delegate(const char *service_name, const char *ip,
                                int port_num, bool tcp_flag);

/**
 *  \brief Set the OSC time offset.
 *
 * @param offset the offset between (global) O2 time and OSC time
 *
 * @return the previous offset
 *
 * O2 global time should start from 0.0 when the clock is started,
 * whereas OSC time starts at 1 Jan 1900. The offset is the OSC time
 * corresponding to O2 time 0.0. Equivalently, OSC_time = O2_time +
 * offset.
 */
O2_EXPORT uint64_t o2_osc_time_offset(uint64_t offset);
/** @} */ // end of OSC Interoperation
#endif

/**
 * \defgroup lowlevelsend Low-Level Message Send/Receive
 *
 * Rather than passing all parameters in one call or letting O2
 * extract parameters from a message before calling its handler, these
 * functions allow building messages one parameter at a time and
 * extracting message parameters one at a time.  The functions operate
 * on "hidden" messages, so these functions are not reentrant.
 *
 * To build a message, begin by calling #o2_send_start to allocate a
 * message. Then call one of the `o2_add_*()` functions to add each
 * parameter. Finally, call either #o2_send_finish to send the
 * message. You should not explicitly allocate or deallocate a message
 * using this procedure.
 *
 * To extract parameters from a message, begin by calling
 * #o2_extract_start to prepare to get parameters from the
 * message. Then call #o2_get_next to get each parameter. If the
 * result is non-null, a parameter of the requested type was obtained
 * and you can read the parameter from the result. Results other than
 * strings, MIDI, and blobs may only remain valid until the next call
 * to #o2_get_next, so you should use or copy the value before reading
 * the next one. Values that are not coerced (requiring a copy) are
 * left in the O2 message and have the same lifetime as the message.
 * You should not reuse this storage because the message may have
 * multiple destinations; thus, the message content should not be
 * altered.
 *
 * A by-product of #o2_extract_start and #o2_get_next is an argument
 * vector (argv) that can be accessed from O2argv. (This is the same
 * argument vector created automatically when a handler is added with
 * #o2_method_new when the parse parameter is true.) A possible
 * advantage of using a sequence of #o2_get_next calls rather than
 * simply setting the parse flag is that you can receive messages with
 * various types and numbers of parameters. Also, you can check vector
 * lengths and stop parsing if unacceptable lengths are encountered.
 * 
 * #o2_get_next will perform type conversion if possible when the
 * requested type does not match the actual type. You can determine
 * the original type by reading the type string in the message. The
 * number of parameters is determined by the length of the type
 * string, with some exceptions.
 *
 * Vectors can be coerced into arrays, in which case each element will
 * be coerced as requested. Arrays can be coerced into vectors if each
 * element of the array can be coerced into the expected vector
 * element type. Vector lengths are provided by the message; there is
 * no way to coerce or limit vector lengths or check that the length
 * matches an expected value. (You can determine the length from the
 * return value and of course you can decide to reject the message if
 * the length is not acceptable.)
 *
 * When a vector is returned, the argument vector has a single element
 * that points to a vector descriptor (the "v" field), which contains
 * the vector element types and the length of the vector (>= 0).
 *
 * When an array is returned, the argument vector contains the value
 * o2_got_start_array followed by an O2arg_ptr for each element of
 * the array, followed by o2_got_end_array.
 *
 * When types T (True), F (False), I (Infinitum), or N (Nil) are in
 * the message, there is an entry in the argument vector; however,
 * there is no data associated with these types (other than the type
 * itself), so the pointers should not be used except to test for
 * non-NULL.
 *
 * In all other cases, the argument vector contains data corresponding
 * to the data item in the message. This may be a pointer into the
 * actual message or a pointer to a temporary location in case the
 * element was coerced to a different type.
 *
 * When the actual type code in the message is in "TFIN" you should
 * call #o2_get_next even though there is no corresponding data stored
 * in the message. The return value, if successful, is a non-NULL
 * pointer that points within or just after the message, but you must
 * not dereference this pointer. (NULL indicates failure as with other
 * type codes. One rationale for calling #o2_get_next even when there
 * is nothing to "get" is that you can call o2_get_next(O2_BOOL) to
 * retrieve 'T', 'F', or 'B' types as an int32_t which is 0 or 1. The
 * 'I' and 'N' types are never coerced.
 *
 * Normally, you should not free the message because normally you are
 * accessing the message in a handler and the message will be freed by
 * the O2 message dispatch code that called the handler.
 *
 * Arrays denoted by [...] in the type string are handled in a somewhat
 * special way:
 *
 * If an array is expected, call o2_get_next(O2_ARRAY_START).  The
 * return value will be o2_got_start_array on success, or NULL if
 * there is no array. The actual value in the message may be an array
 * or a vector. If it is a vector, the elements of the vector will be
 * coerced to the types requested in successive calls to
 * #o2_get_next. After retrieving array elements, call
 * o2_get_next(O2_ARRAY_END). The return value should be
 * o2_got_end_array.  NULL is returned if there is an error. For
 * example, suppose you call #o2_get_next with characters from the
 * type string "[id]" and the actual parameter is a vector integers
 * ("vi") of length 2. The return values from #o2_get_next will be
 * o2_got_start_array, an O2arg_ptr to an integer, an O2arg_ptr to a
 * double (coerced from the integer vector), and finally
 * o2_got_end_array.  If the vector length is 1, the third return
 * value will be NULL. If the vector length is 3 (or more), the fourth
 * return value will be NULL rather than o2_got_end_array.
 *
 * The special values o2_got_start_array and o2_got_end_array are not
 * valid structures. In other words, fields such as
 * o2_got_start_array->i32 are never valid or meaningful. Instead,
 * o2_got_start_array and o2_got_end_array are just 'tokens' used to
 * indicate success in type checking. These values are distinct from
 * NULL, which indicates a type incompatibility.
 *
 * Note also that vector elements cannot be retrieved directly without
 * calling o2_get_next(O2_VECTOR) or o2_get_next(O2_ARRAY_START).  For
 * example, if the actual argument is a two-element integer vector
 * ("vi"), a call to o2_get_next(O2_INT32) will fail unless it is
 * preceded by o2_get_next(O2_VECTOR) or o2_get_next(O2_ARRAY_START).
 *
 * If a vector is expected, call o2_get_next(O2_VECTOR). The return
 * value will be a non-null O2arg_ptr if the next argument in the
 * actual message is a vector or array, and otherwise NULL. You should
 * not dereference this return value yet...
 *
 * You *must* then call #o2_get_next with the desired type for vector
 * elements. The return value will be an O2arg_ptr (which will be the
 * same value previously returned) containing v.typ set to the desired
 * type, v.len still set to the number of elements, and v.vi, v.vh,
 * v.vd, v.vf, or v.vc pointing to the possibly coerced elements.
 *
 * Note that the sequence of calling #o2_get_next twice for vectors
 * corresponds to the two type characters used to encode them,
 * e.g. "vi" indicates a vector of integers.
 *
 * Coercion is supported as follows. An "x" means that coercion is
 * provided from the type indicated on some row to the type
 * corresponding to the column ("\*" indicates special consideration
 * described below.)
 * \verbatim
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
 * \endverbatim
 *    \*Entries marked with "*": Coercion is
 *    from 0 to False and from non-zero to True.
 *
 * Coercion fails in all cases not marked.
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
 * greater than the `size` parameter originally passed to #o2_blob_new.
 *
 * Caller is responsible for freeing the returned blob using #O2_FREE.
 *
 * A constructed blob can be added to a message. If you add parameters to
 * a message one-at-a-time, you can use #o2_add_blob_data to copy data
 * directly to a message without first allocating a blob and copying
 * data into it.
 *
 * @param size The size of the data to be added to the blob
 *
 * @return the address of the new blob or NULL if memory cannot be allocated.
 */
O2_EXPORT O2blob_ptr o2_blob_new(uint32_t size);


/**
 * \brief Prepare to build a message
 *
 * @return #O2_SUCCESS if success, #O2_FAIL if not.
 *
 * Allocates a "hidden" message in preparation for adding
 * parameters. After calling this, you should call `o2_add_` functions
 * such as #o2_add_int32 to add parameters. Then call
 * #o2_send_finish to send the message.
 *
 * Calling #o2_send_start again before calling #o2_send_finish or
 * #o2_message_finish will cause an immediate #O2_FAIL return.
 * If you decide not to send a message after #o2_send_start,
 * call #o2_message_finish to retrieve the constructed message
 * and free it.
 */
O2_EXPORT O2err o2_send_start(void);


/// \brief add a `float` to the message (see #o2_send_start)
O2_EXPORT O2err o2_add_float(float f);

/// \brief This function suppports #o2_add_symbol and #o2_add_string
/// Normally, you should not call this directly.
O2_EXPORT O2err o2_add_string_or_symbol(O2type tcode, const char *s);

/// \brief add a symbol to the message (see #o2_send_start)
#define o2_add_symbol(s) o2_add_string_or_symbol(O2_SYMBOL, s)

/// \brief add a string to the message (see #o2_send_start)
#define o2_add_string(s) o2_add_string_or_symbol(O2_STRING, s)

/// \brief add an `O2blob` to the message (see #o2_send_start), where
///        the blob is given as a pointer to an #O2blob object.
O2_EXPORT O2err o2_add_blob(O2blob_ptr b);

/// \brief add an `O2blob` to the message (see #o2_send_start), where
///        the blob is specified by a size and a data address.
O2_EXPORT O2err o2_add_blob_data(uint32_t size, void *data);

/// \brief add an `int64` to the message (see #o2_send_start)
O2_EXPORT O2err o2_add_int64(int64_t i);

/// \brief This function supports #o2_add_double and #o2_add_time
/// Normally, you should not call this directly.
O2_EXPORT O2err o2_add_double_or_time(O2type tchar, double d);

/// \brief add a `double` to the message (see #o2_send_start)
#define o2_add_double(d) o2_add_double_or_time(O2_DOUBLE, d)

/// \brief add a time (`double`) to the message (see #o2_send_start)
#define o2_add_time(t) o2_add_double_or_time(O2_TIME, t)

/// \brief This function supports #o2_add_int32 and #o2_add_char
/// Normally, you should not call this directly.
O2_EXPORT O2err o2_add_int32_or_char(O2type tcode, int32_t i);

/// \brief add an `int32` to the message (see #o2_send_start)
#define o2_add_int32(i) o2_add_int32_or_char(O2_INT32, i)

/// \brief add a `char` to the message (see #o2_send_start)
#define o2_add_char(c) o2_add_int32_or_char(O2_CHAR, c)

/// \brief add a short midi message to the message (see #o2_send_start)
O2_EXPORT O2err o2_add_midi(uint32_t m);

/// \brief This function supports #o2_add_true, #o2_add_false, #o2_add_bool,
/// #o2_add_nil, #o2_add_infinitum, and others.
/// Normally, you should not call this directly.
O2_EXPORT O2err o2_add_only_typecode(O2type typecode);

/// \brief add "true" to the message (see #o2_send_start)
#define o2_add_true() o2_add_only_typecode(O2_TRUE);

/// \brief add a "false" to the message (see #o2_send_start)
#define o2_add_false() o2_add_only_typecode(O2_FALSE);

/// \brief add a boolean typecode T or F
///  (this differs from `o2_add_bool` which uses typecode B and adds
///  a 0 or 1 as data)
#define o2_add_tf(x) o2_add_only_typecode((x) != 0 ? O2_TRUE : O2_FALSE)

/// \brief add 0 (false) or 1 (true) to the message (see #o2_send_start)
#define o2_add_bool(x) o2_add_int32_or_char(O2_BOOL, (x) != 0)

/// \brief add "nil" to the message (see #o2_send_start)
#define o2_add_nil() o2_add_only_typecode(O2_NIL);

/// \brief add "infinitum" to the message (see #o2_send_start)
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
O2_EXPORT O2err o2_add_vector(O2type element_type,
                  int length, void *data);

#ifndef O2_NO_BUNDLES
/**
 * \brief add a message to a bundle
 *
 * @param msg a message or bundle to add
 *
 * @return O2_SUCCESS
 *
 * This function can be called after #o2_send_start. If you 
 * add a message to a bundle with this function, you must not
 * call any other `o2_add_*()` functions. E.g. do not call both
 * #o2_add_int32 and #o2_add_message on the same message.
 * 
 * This function does NOT free msg. Probably you should call 
 * O2_FREE(msg) after calling o2_add_message(msg).
 */
O2_EXPORT O2err o2_add_message(O2message_ptr msg);
#endif

/**
 * \brief finish and return the message.
 *
 * @param time the timestamp for the message (0 for immediate)
 * @param address the O2 address pattern for the message
 * @param tcp_flag boolean if true, send message reliably
 *
 * @return the address of the completed message, or NULL on error
 *
 * The message must be freed using #O2_FREE or by calling
 * #o2_message_send. If the message is a bundle (you have added
 * messages using #o2_add_message), the address should be '#' 
 * followed by the service name, e.g. "#service1".
 */
O2_EXPORT O2message_ptr o2_message_finish(O2time time, const char *address,
                                 bool tcp_flag);

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
 * The message must be freed using #O2_FREE or by calling
 * #o2_message_send. This function is intended to be used to 
 * forward OSC messages to a service, but it is the implementation
 * of #o2_message_finish, which simply passes NULL for service.
 */
O2_EXPORT O2message_ptr o2_service_message_finish(O2time time,
             const char *service, const char *address, bool tcp_flag);


/**
 * \brief send a message allocated by #o2_send_start.
 *
 * This is similar to calling #o2_send, except you use a three-step
 * process of (1) allocate the message with #o2_send_start, (2) add
 * parameters to it using `o2_add_` functions, and (3) call
 * #o2_send_finish to send it.
 *
 * @param time the timestamp for the message
 * @param address the destination address including the service name.
 *        To send a bundle to a service named foo, use the address "#foo".
 * @param tcp_flag boolean that says to send the message reliably.
 *        Normally, true means use TCP, and false means use UDP.
 *
 * @return #O2_SUCCESS if success.
 *
 * Typically, #o2_send_finish returns #O2_SUCCESS. Messages can be 
 * queued for later delivery, forwarded for remote delivery, or delivered
 * only to a tapper (see #o2_tap), so success and failure are not always
 * easy to distinguish. One (probably) clear case is that if a message is
 * sent to a non-existent service, #O2_NO_SERVICE is returned. #O2_NO_CLOCK
 * is returned if there is no established clock and the message has a
 * non-zero timestamp. For other situations, see #o2_message_warnings.
 */
O2_EXPORT O2err o2_send_finish(O2time time, const char *address, bool tcp_flag);


/** @} */

/**
 * \defgroup lowlevelparse Low-Level Message Parsing
 *
 * These functions can retrieve message arguments one-at-a-time.
 * There are some hidden state variables to keep track of the state
 * of unpacking, so these functions are not reentrant.
 * Arguments are returned using a pointer to a union type: #O2arg_ptr.
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
 * To get arguments from a message, call #o2_extract_start, then for
 * each parameter, call #o2_get_next.
 */
O2_EXPORT int o2_extract_start(O2msg_data_ptr msg);

/**
 * \brief get the next message parameter
 *
 * This function is called repeatedly to obtain parameters in order
 * from the message passed to #o2_extract_start.
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

Note: call #o2_method_new with type_spec = "id", h = my_handler,
coerce = false, parse = false. In this case, since there is
no type coercion, type_spec must match the message exactly,
so #o2_get_next should always return a non-null O2arg_ptr.
However, this code can fail if a badly formed message is sent
because there is no test for the NULL value that will be
returned by #o2_get_next.
\code{.c}
    int my_handler(O2message_ptr msg, char *types,
                   O2arg_ptr *argv, int argc, void *user_data)
    {
        o2_extract_start(msg);
        // we expect an int32 and a double argument
        int32_t my_int = o2_get_next(O2_INT32)->i32;
        double my_double = o2_get_next(O2_DOUBLE)->d;
        ...
    }
\endcode

### Example 2: Type coercion and type checking.

Note: call #o2_method_new with type_spec = NULL, h = my_handler,
coerce = false, parse = false. In this case, even though
coerce is false, there is no type_spec, so the handler will
be called without type checking. We could check the
actual message types (given by types), but here, we will
coerce into our desired types (int32 and double) if possible.
Since type coercion can fail (e.g. string will not be converted
to number, not even "123"), we need to check the return value
from #o2_get_next, where NULL indicates incompatible types.
\code{.c}
    int my_handler(O2message_ptr msg, char *types,
                   O2arg_ptr *argv, int argc, void *user_data)
    {
        o2_extract_start(msg);
        // we want to get an int32 and a double argument
        O2arg_ptr ap = o2_get_next(O2_INT32);
        if (!ap) return O2_FAIL; // parameter cannot be coerced
        int32_t my_int = ap->i32;
        O2arg_ptr ap = o2_get_next(O2_DOUBLE);
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
O2_EXPORT O2arg_ptr o2_get_next(O2type type_code);

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
typedef struct O2sched {
    int64_t last_bin;
    double last_time;
    O2message_ptr table[O2_SCHED_TABLE_LEN];
} O2sched, *O2sched_ptr;
/** \endcond */

/**
 * \brief Scheduler that schedules according to global (reference) clock
 * time
 *
 * Scheduling on this scheduler (including sending timed messages)
 * will only work after clock synchronization is obtained. Until then,
 * timed message sends will fail and attempts to #o2_schedule_msg will
 * fail.
 */
O2_EXPORT O2sched o2_gtsched;

/**
 * \brief Scheduler that schedules according to local clock time
 *
 * It may be necessary to schedule events before clock synchronization
 * with the reference clock, or you may want to schedule local processing
 * that ignores any changes in clock time or clock speed needed to
 * stay synchronized with the reference clock (even though these should
 * be small). For example, O2 uses the local time scheduler to
 * schedule the clock synchronization protocol, which of course must
 * run before clock synchronization is obtained.
 *
 * In these cases, you should schedule messages using #o2_ltsched.
 */
O2_EXPORT O2sched o2_ltsched;

/**
 * \brief Current scheduler.
 *
 * When a timed message is delivered by a scheduler, #o2_active_sched
 * is set to pount to the scheduler. A handler that constructs and
 * schedules a message can use this pointer to continue using the same
 * scheduler.
 */
O2_EXPORT O2sched_ptr o2_active_sched; // the scheduler that should be used


/**
 * /brief Schedule a message.
 *
 * Rather than sending a message, messages can be directly
 * scheduled. This is particulary useful if you want to schedule
 * activity before clock synchronization is achieved. For example, you
 * might want to poll every second waiting for clock
 * synchronization. In that case, you need to use the local scheduler
 * (#o2_ltsched). #o2_send will use the global time scheduler
 * (#o2_gtsched), so your only option is to construct a message and
 * call #o2_schedule_msg.
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
O2_EXPORT O2err o2_schedule_msg(O2sched_ptr scheduler, O2message_ptr msg);


/**
 * /brief Flush all scheduled messages in #o2_gtsched
 *
 * All messages in #o2_gtsched are removed. Normally, this would only
 * be called from a #o2_time_jump_callback
 * indicating that global time is about to jump, possibily invalidating
 * the timestamps of scheduled messages. See also #o2_schedule_sendall
 * and #o2_clock_jump.
 *
 * @return number of messages flushed or an O2 error code.
 */
O2_EXPORT O2err o2_sched_flush(void);


/** @} */ // end of a basics group

#ifndef O2_NO_MQTT
/** \defgroup mqttapi MQTT API
 * @{
 */

/// \brief
/** @} */ // end of a mqttapi group
#endif

#ifndef O2_NO_BRIDGES
/** \defgroup bridgeapi Bridge API
 * @{
 */

/// \brief enable O2lite protocol connections to this proces
O2err o2lite_initialize(void);


/** @} */ // end of a bridgeapi group
#endif

/**
 * /brief Obtain the message being sent.
 * 
 * This function is for use within a #bridge_send callback. It obtains a
 * reference to the message to be sent, but it does not assume ownership
 * of the message.
 */
O2message_ptr o2_current_message(void);


/**
 * /brief Free the message being sent.
 * 
 * This function is for use within a #bridge_send callback. It takes
 * ownership of the current message being sent and frees its memory.
 * Both of these are required for correctness of #bridge_send callbacks,
 * but if the message is to be freed later, use #o2_postpone_delivery instead.
 * Never call this function from a message handler passed to #o2_method_new.
 */
void o2_complete_delivery(void);


/**
 * /brief Take ownership of the current message being delivered.
 *
 * This function is for use within a #bridge_send callback. It takes
 * ownership of and returns the current message. It is then the callback's
 * responsibility to (eventually) free the message using #O2_FREE. Typically,
 * this function is used to hold the message in a queue for delivery, and
 * the message is freed after the data is transferred via the protocol
 * supported by this bridge. The message should also be freed if the bridge
 * is deleted, i.e. when the #bridge_finish callback is called.
 * Never call this function from a message handler passed to #o2_method_new.
 * These handlers may not take ownership of messages.
 */
O2message_ptr o2_postpone_delivery(void);


#ifndef O2_NO_MQTT
/** \defgroup mqttapi MQTT Bridge API
 * @{
 */

/**
 * \brief Enable MQTT to form wide-area-network connections between O2 processes
 *
 * Note that #o2_mqtt_enable can be called before #o2_initialize or
 * while O2 initialization is waiting for a public IP port. In that
 * case, O2_SUCCESS is called, but if O2 fails to obtain a public IP
 * port (within about 10 seconds), an MQTT connection will not be
 * established. This decision can be detected by calling
 * #o2_get_addresses (it will set the #public_ip parameter to
 * "00000000" if there is no public IP).
 *
 * @param broker An IP address or name for an MQTT broker, or NULL or
 *        empty for default.
 *
 * @param port_num A port number for the MQTT broker, or 0 for default.
 *
 * @return O2_SUCCESS if successful, O2_NOT_INITIALIZED if O2 is not
 *         initialized, or O2_NO_NETWORK if networking is disabled
 *         (see #o2_network_enable) or no Internet connection was
 *         found.
 */
 O2err o2_mqtt_enable(const char *broker, int port_num);

/** @} */ // end of a mqttapi group
#endif

// note: shared mem process support depends on bridge support
#ifndef O2_NO_SHAREDMEM
O2err o2_shmem_initialize(void);
#endif


#ifndef O2_NO_WEBSOCKETS
#ifdef O2_NO_BRIDGES
#error WEBSOCKETS feature depends on BRIDGES
#error You must define O2_NO_WEBSOCKETS or undefine O2_NO_BRIDGES
#endif
#endif 

/** \defgroup httpapi HTTP/Websockets API
 * @{
 */

#define O2_NO_HOSTNAME "\001"   // a non-text marker string

/**
 * \brief Enable HTTP and Websocket access to an O2 host
 *
 * An simple HTTP server is operated on the given port with support for
 * web sockets. A name is set up with ZeroConf/Bonjour so that the
 * server can be accessed with the url `http://<ensname>.local:<port>`,
 * where `<ensname>` is the O2 ensemble name and `<port>` is the
 * `port` parameter passed to this function. If other hosts are also
 * offering an http service, the name may be changed by appending
 * " (2)" etc. The server may also be reached via the host name which
 * may already be published by ZeroConf/Bonjour, or by the IP address,
 * which is necessary for Android devices that do not support .local.
 * names through mDNS.
 *
 * @param port The port number for the web server. If 0, then 8080 is
 *        used.
 *
 * @param root The path to the files to be served. E.g. if the file
 *        `mypage.htm` is in `path`, then the page will be served by
 *        a URL like `http://o2.local:8080/mypage.htm`. `path` can
 *        be a full path or relative path. If relative, it is relative
 *        to the current working directory of the current process.
 *
 * @return O2_SUCCESS if successful, O2_NOT_INITIALIZED if O2 is not
 *         initialized, or O2_NO_NETWORK if networking is disabled
 *         (see #o2_network_enable) or no Internet connection was
 *         found.
 */
O2err o2_http_initialize(int port, const char *root);

/** @} */ // end of a httpapi group

#ifdef __cplusplus
}
#endif

#endif /* O2_H */
