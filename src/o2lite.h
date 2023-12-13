// o2lite.h -- header for o2lite library
//
// Roger B. Dannenberg
// July 2020

/** The O2lite protocol is a subset of O2. O2lite clients always
 * connect to a host called the "sponsor" running a full O2
 * implementation, never another O2lite client.  Due to the
 * peer-to-peer nature of O2, once connected, the O2lite host can send
 * messages to O2 services and also provide a service to the O2
 * ensemble. An O2lite host's services appear as services of the
 * sponsor, and therefore an O2lite service name cannot conflict with
 * a sponsor's service or the service of another O2lite host connected
 * to the same sponsor. Attempts to create conflicting services will
 * fail.
 *
 * O2lite offers a subset of functions available in O2.  The
 * functionality on the sponsor side limits what O2lite can do. There
 * are further limitations of this o2lite implementation, but this
 * does not mean extensions are not allowed. For portability and
 * interoperation, one should not assume greater functionality than
 * offered in this implementation. (These implementation restrictions
 * particularly include a restricted set of types in messages, no
 * pattern matching, and no support for bundles.)
 *
 * The O2lite protocol and O2 implementation of it support these
 * features:
 *    - create service (advertised on the sponsor side, messages are 
 *        forwarded to the O2lite side)
 *    - clock synchronization
 *    - send messages of any kind, including bundles, with any data types
 *    - every O2lite host is given a "bridge ID." The O2lite host can
 *        also obtain the O2 sponsor's full name. By combining them, the
 *        O2lite host can create a globally unique name of the form
 *        "@4a6dfb76:c0a801a6:fc1d:2", which has the public IP, local IP,
 *        port number of the sponsor, and bridge ID of the O2lite host.
 * This O2lite implementation has these restrictions:
 *    - no pattern matching for incoming messages
 *    - no bundle construction or handling
 *    - no type coercion -- message handlers can specify a type string 
 *        that must match exactly or the message will be dropped, or 
 *        message handlers can specify no types, in which case no types
 *        are checked and all messages are dispatched to the handler.
 *    - properties are not supported.
 *    - O2lite host has two status bits: connected and synchronized. There
 *        is no per-service status information, although a work-around is
 *        to poll services for status information.
 *    - message types are:
 *        - string
 *        - time
 *        - double
 *        - float
 *        - int32
 *    - message handlers are expected to call `o2l_get_float()`,
 *        `o2l_get_int()`, etc. in order to extract data from messages.
 *        O2lite does not construct an argument vector.
 *    - O2lite does not have a general scheduler. It only provides
 *        o2_local_time and o2l_get_time() (global time after clock sync).
 */ 

// Discovery options:
//     o2discovery -- o2's original built-in protocol (default is false)
//         broadcast -- use broadcast to speed things up (default is true)
//     zeroconf -- use ZeroConf for discovery (default is true)
//     clocksync -- o2lite should synchronize with host (can be used with
//         either o2discovery or zeroconf) (default is true)
// Note that macros are "true" if defined, "false" if undefined, and most
//     are inverted, e.g. O2_NO_O2DISCOVERY rather than
//     O2_O2DISCOVERY, because most options are for disabling features;
//     thus, most macros are undefined.
// Here are the macros with their default values:
//     O2_NO_O2DISCOVERY -- O2 is not using built-in discovery; (defined)
//     O2_NO_ZEROCONF -- O2 is not using ZeroConf for discovery; (undefined)
//     O2L_NO_BROADCAST -- O2lite does not broadcast to speed up discovery
//         (undefined). This has no effect if O2_NO_O2DISCOVERY, which is
//         defined by default. The ZeroConf protocol ALWAYS uses broadcast
//         (even if O2L_NO_BROADCAST); it's internal to ZeroConf.
//     O2L_NO_CLOCKSYNC -- O2lite does not synchronize with host (undefined)

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
// NOTE: more includes below after O2_MALLOC is defined

#ifdef __cplusplus
extern "C" {
#endif

#define O2L_VERSION 0x020000

// you can enable/disable O2LDB printing using -DO2LDEBUG=1 or =0 
#if (!defined(O2LDEBUG))
#ifndef NDEBUG
// otherwise default is to enable in debug versions
#define O2LDEBUG 1
#else
#define O2LDEBUG 0
#endif
#endif

#define O2LDB if (O2LDEBUG)

#define O2LDBV if (O2LDEBUG && verbose)

#if defined(O2_NO_O2DISCOVERY) && !defined(O2L_NO_BROADCAST)
#define O2L_NO_BROADCAST 1
#endif
#if defined(O2_NO_O2DISCOVERY) && defined(O2_NO_ZEROCONF)
#error O2_NO_O2DISCOVERY and O2_NO_ZEROCONF are both defined - no discovery
#endif

// default is ZEROCONF, so if necessary, disable O2_NO_O2DISCOVERY:
#if !defined(O2_NO_O2DISCOVERY) && !defined(O2_NO_ZEROCONF)
#define O2_NO_O2DISCOVERY
#if !defined(O2L_NO_BROADCAST)
#define O2L_NO_BROADCAST
#endif
#endif

#define O2_MALLOC malloc
#define O2_CALLOC calloc
#define O2_FREE free

#include "o2base.h"
#include "hostip.h"

#define MAX_MSG_LEN 256
#define PORT_MAX 16

/// \brief success return code
#define O2L_SUCCESS 0

/// \brief general failure return code
#define O2L_FAIL -1

/// \brief this thing you are intializing is already running
#define O2L_ALREADY_RUNNING -5

typedef float o2l_time; // could be double, but this gives 1ms accuracy
                        // for 2.3 hours, or 10ms accuracy for 23 hours

#define O2_UDP_FLAG 0
#define O2_TCP_FLAG 1

typedef struct o2l_msg {
    int32_t length; // length of flags, timestamp, address, and the rest
    int32_t misc;   // flags and ttl (see O2msg_data)
    double timestamp; // regardless of o2l_time, this is O2time = double
    char address[4];
} o2l_msg, *o2l_msg_ptr;

/// \brief incoming message handler type
typedef void (*o2l_handler)(o2l_msg_ptr msg, const char *types,
                            void *data, void *info);

/// \brief enable extra debugging
O2_EXPORT int verbose;

/// \brief The current local time, maintained by #o2_poll
O2_EXPORT o2l_time o2l_local_now;

#ifdef ESP32
/// \brief used for debugging ESP32 Thing implementation
void button_poll();
#endif

/**
 * \brief start preparing a message to send by UDP or TCP
 *
 * @param address O2 address
 * @param time timestamp for message
 * @param types O2 type string, only s, i, f, t, d supported
 * @param tcp true for TCP, false for UDP; must be 0 or 1
 *
 * Start every message by calling this function. After adding parameters
 * according to the type string, send the message with #o2l_send().
 */
O2_EXPORT void o2l_send_start(const char *address, o2l_time time,
                    const char *types, bool tcp);

/// \brief Send a fully constructed message
O2_EXPORT void o2l_send();

/**
 * \brief Add a handler for an O2 address.
 * @param path a full O2 address including service
 * @param typespec the expected O2 type string (without ','), or NULL
 *                 to allow any types
 * @param full require a full exact match to the incoming address
 * @param h the handler to call for this message
 * @param info an optional value to pass to the handler. This can be
 *             useful if one handler handles multiple addresses
 * 
 * If full is false, this handler will match any address that begins with
 * #path provided that the next character in the address is '/' or
 * end-of-string. In other words, /s1/a does not act like /s1/a*, but it
 * can match /s1/a, /s1/a/b, /s1/a/c, etc.
 *
 * The handler will not be called if the #typespec does not match the
 * message type string exactly, except that a NULL #typespec will match
 * any types. Note that NULL and empty string ("") are not the same.
 *
 * Handlers are searched from newest to oldest, and they may not be removed.
 * (Although it would not be hard to add this functionality.)
 *
 * Handlers are not reentrant, so handle the message and copy out any data
 * you want to retain. The message is destroyed after the handler 
 * returns. 
 * 
 * Message handling is synchronous and driven by calls to #o2l_poll. 
 *
 * If a message is sent from O2 with a timestamp, but the o2lite process
 * does not have clock synchronization, the O2 process will use its 
 * scheduler to hold the message until the timestamp, delivering it
 * according to the timestamp but delayed by network latency. If the
 * o2lite process has synchronized clocks, the message will be sent
 * immediately, and o2lite will call the handler immediately, so it is
 * up to the application to deal with the timestamp.
 */
O2_EXPORT void o2l_method_new(const char *path, const char *typespec,
                    bool full, o2l_handler h, void *info);

/// \brief returns time in seconds since #o2l_initialize.
O2_EXPORT o2l_time o2l_local_time();

/// \brief return O2 time in seconds.
///
/// Returns -1 if clock synchronization has not completed.
///
O2_EXPORT o2l_time o2l_time_get();

/// \brief call this frequently to poll for messages.
O2_EXPORT void o2l_poll();

#if ESP32
/// \brief WiFi connection
///
/// Blinks LED until connected, then prints IP address
///
O2_EXPORT void connect_to_wifi(const char *hostname, const char *ssid, const char *pwd);

/// \brief print a horizontal line to highlight debug info
///
/// This is used by connect_to_wifi() to highlight the IP address,
/// but the routine is exported here to make it available for general use.
///
O2_EXPORT void print_line();
#endif


/**
 * \brief call this before any other o2lite functions.
 *
 * esp32 requires WiFi and serial configuration: For esp32 in the
 * Arduino environment, you should write something like:
 *
 * void setup() {
 *    Serial.begin(115200);
 *    connect_to_wifi(hostname, network_name, network_pswd);
 *    o2l_initialize(ensemble_name);
 *    ...
 * }
 *
 * @param ensemble the O2 ensemble name. This string is owned by o2lite
 *                 until it finishes. Typically the name is a literal
 *                 string. o2lite will not modify or free this string.
 * @return O2L_SUCCESS normally or O2L_FAIL if initialization fails
 */
O2_EXPORT int o2l_initialize(const char *ensemble);

/// \brief shut down o2lite resources (not fully implemented)
O2_EXPORT int o2l_finish();

/// \brief call between #o2l_send_start and #o2l_send to add a string.
O2_EXPORT void o2l_add_string(const char *s);

/// \brief call between #o2l_send_start and #o2l_send to add a time.
O2_EXPORT void o2l_add_time(double time);

/// \brief call between #o2l_send_start and #o2l_send to add a double.
#define o2l_add_double(x) o2l_add_time(x)

/// \brief call between #o2l_send_start and #o2l_send to add a float.
O2_EXPORT void o2l_add_float(float x);

/// \brief call between #o2l_send_start and #o2l_send to add an int32.
#define o2l_add_int(i) o2l_add_int32(i)

/// \brief call between #o2l_send_start and #o2l_send to add an int32.
O2_EXPORT void o2l_add_int32(int32_t i);

/**
 * \brief call between #o2l_send_start and #o2l_send to check for overflow.
 *
 * @return true if there was an error either in unpacking a message with
 * o2l_get_ functions or packing a message with o2l_add_ functions. The
 * error is cleared when a handler is called and when #o2l_send_start is
 * called, so if you are creating a new message as you unpack parameters
 * from an incoming message in a handler, the error could refer to either
 * reading past the end of the incoming message or trying to write past
 * the end of the send buffer (which is set to MAX_MSG_LEN).
 */
O2_EXPORT bool o2l_get_error();

/// \brief call in a message handler to get the message timestamp.
O2_EXPORT double o2l_get_timestamp();

/// \brief call in a message handler to get the next parameter as time.
O2_EXPORT double o2l_get_time();

/// \brief call in a message handler to get the next parameter as double.
#define o2l_get_double() o2l_get_time()

/// \brief call in a message handler to get the next parameter as float.
O2_EXPORT float o2l_get_float();

/// \brief call in a message handler to get the next parameter as int32.
O2_EXPORT int32_t o2l_get_int32();

/// \brief call in a message handler to get the next parameter as int32.
#define o2l_get_int(i) o2l_get_int32(i)

/// \brief call in a message handler to get the next parameter as string.
///
/// The string is not copied to the heap, so the data will be invalid
/// when the handler returns.
O2_EXPORT char *o2l_get_string();

/**
 * \brief Announce services offered to O2 by this bridged process
 *
 * @param services a list of service names (without leading '/')
 *                 separated by commas, e.g. "player" or "player,sensor"
 *
 * This may be called any time after initialization. The #services string
 * ownership passes to o2lite until o2lite finishes. Typically, use a
 * literal string, which will not be altered or freed by o2lite.
 *
 * When a connection is made, the O2 process will receive the service 
 * names and offer them, but if there are other offerings of the same
 * service, only one of them will receive messages.
 *
 * You *must* call #o2l_set_services to receive any messages. Creating 
 * a method (handler) is not sufficient.
 */
O2_EXPORT void o2l_set_services(const char *services);

/**
 * \brief The IP and port number of the bridged O2 process.
 *
 * The final step of the connection protocol is setting #o2l_bridge_id.
 * Since there can be multiple servers for any service in O2, you may
 * want the ability to create a unique service name. O2 uses @public:internal:port
 * names, e.g. "@4a6dfb76:c0a801a6:fc1d" as services that denote O2 processes.
 * An o2lite process could do the same, but if it is using NAT, it might
 * share an IP address with an O2 process. A safer way is to take the
 * IP:port name of the O2 host and append the bridge id, e.g. something
 * like "@4a6dfb76:c0a801a6:fc1d:2", which is guaranteed to be unique, at least
 * until you disconnect, at which point another bridge process can reuse
 * the bridge id.
 */
O2_EXPORT char o2l_remote_ip_port[16];

/** 
 * \brief A small integer to identify this bridged process.
 *
 * The bridge id after a connection complets is stored in #o2l_bridge_id.
 * This variable is initially -1 and is reset to -1 whenever the TCP 
 * connection to the O2 process is closed or lost, so this is also a 
 * good indicator that the connection is up and running.
 */
O2_EXPORT int o2l_bridge_id;

// define IS_BIG_ENDIAN, IS_LITTLE_ENDIAN, and swap64(i),
// swap32(i), and swap16(i)
#if WIN32
#elif ESP32
// LITTLE_ENDIAN, BYTE_ORDER are defined already
#define IS_BIG_ENDIAN (BYTE_ORDER != LITTLE_ENDIAN)
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
#if IS_LITTLE_ENDIAN
#define o2lswap16(i) ((((i) >> 8) & 0xff) | (((i) & 0xff) << 8))
#define o2lswap32(i) ((((i) >> 24) & 0xff) | (((i) & 0xff0000) >> 8) | \
                   (((i) & 0xff00) << 8) | (((i) & 0xff) << 24))
#define o2lswap64(i) ((((uint64_t) o2lswap32(i)) << 32) | o2lswap32((i) >> 32))
#else
#define o2lswap16(i) (i)
#define o2lswap32(i) (i)
#define o2lswap64(i) (i)
#endif


/************************************************************/
/****************** Discovery API ***************************/
/************************************************************/

#ifdef WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

// There are about 4 different discovery implementations, even for O2lite.
// To avoid massive amounts of conditional compilation to support each
// protocol, the implementations are separated into multiple files:
//     o2liteavahi.c -- Avahi (linux only)
//     o2litebonjour.c -- Avahi (Apple, Windows)
//     o2liteo2disc.c -- Apple, Windows, Linux o2 discovery (non-standard)
// All files use conditional compilation at the top level so they may be
// compiled and linked together, but you only need to link with one, e.g.
// o2liteavahi.c for linux, o2litebonjour.c for Apple and Windows.
//
// Each file implements these functions which are called from o2lite.c:

// o2ldisc_init() -- called in o2l_initialize() for
//     implementation-specific actions
O2_EXPORT int o2ldisc_init(const char *ensemble);

// o2ldisc_poll() -- called before select(), which polls sockets. Here, you
//     can add_socket() to add to the read_set passed to select or poll for
//     timeouts.
O2_EXPORT void o2ldisc_poll();

// o2ldisc_events() -- called to check for incoming messages or socket errors
O2_EXPORT void o2ldisc_events(fd_set *readset);

/**********************************************************************/
/* Internal symbols declared for use by Discovery API Implementations */
/**********************************************************************/
#ifndef WIN32
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
typedef int SOCKET;  // we use SOCKET to denote the type of a socket
#endif

O2_EXPORT SOCKET tcp_sock;
O2_EXPORT const char *o2l_ensemble;
O2_EXPORT struct sockaddr_in udp_server_sa;
O2_EXPORT int udp_recv_port;
O2_EXPORT SOCKET udp_recv_sock;

O2_EXPORT bool o2l_is_valid_proc_name(const char *name, int port,
                            char *internal_ip, int *udp_port);

O2_EXPORT int o2l_parse_version(const char *vers, int vers_len);

O2_EXPORT int o2l_address_init(struct sockaddr_in *sa, const char *ip, 
                               int port_num, bool tcp);

O2_EXPORT void o2l_network_connect(const char *ip, int port);

O2_EXPORT void o2l_add_socket(SOCKET s);

O2_EXPORT int o2l_bind_recv_socket(SOCKET sock, int *port);

/*********************************/
/* exported for use in test code */
/*********************************/

O2_EXPORT void o2l_dispatch(o2l_msg_ptr msg);
O2_EXPORT char tcpinbuf[MAX_MSG_LEN];
O2_EXPORT char outbuf[MAX_MSG_LEN];
O2_EXPORT o2l_msg_ptr out_msg;
O2_EXPORT int out_msg_cnt;

#ifdef __cplusplus
}
#endif
