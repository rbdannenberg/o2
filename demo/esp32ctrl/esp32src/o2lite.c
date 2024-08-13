// o2lite.c -- a simple o2lite client library
//
// Roger B. Dannenberg
// Jul-Aug 2020, Jun 2021 updated for Avahi
//
// This illustrates a bare-bones o2lite client implementation.
// To simplify things, it calls system network functions directly
// rather than network.c. All network calls are synchronous, so 
// this could introduce more latency than necessary.
//
// See below for using O2lite in the ESP32 Arduino environment.
//
// LIMITATIONS
//
// Some important differences between O2 and O2lite:
// - O2lite connects to one O2 host as a client, not as a peer.
// - All O2lite messages travel through the host as intermediary (if
//   there is one host to communicate with, this is insignificant.)
// - O2lite message creation/disassembly handles only these types:
//   string, time, double, float, int32.
// - O2lite services cannot conflict with services on the host.
// - O2lite must send queries to the host to obtain the status of 
//   services (find out how below).
// - O2lite messages are dispatched immediately regardless of
//   timestamps (but hosts will hold messages until their 
//   timestamps before forwarding messages to O2lite processes).
// - O2lite messages are limited to a length of 256 bytes.
//
// SERVICE STATUS
//
// O2lite clients do not have a full directory of services and their
// status. To retrieve a service status, send the service name to
// "/_o2/o2lite/st" (typespec "s"). Create a local handler for "/_o2/st"
// with typespec "si". The parameters will be the service name and status
// using values as specified in o2.h (see o2_status()).
//
// Similarly, there is no o2_services_list() for o2lite. Instead, send a
// message to "/_o2/o2lite/ls" (typespec ""). A message will be sent for
// each service to this o2lite client's "/_o2/ls" with typespec "siss"
// with parameters:
// - service name
// - service type (see o2.h o2_service_type())
// - process name (see o2.h o2_service_process())
// - properties or tapper (see o2.h o2_service_tapper() and 
//      o2_service_properties())
// After all service information has been sent, an end-of-services message
// is sent with service name "", type 0, process name "", properties "".
//
// O2LITE GOALS
//
// Not all machines have the full capabilities required to implement O2:
// - low-latency audio processes cannot invoke heavy networking system calls
// - browsers cannot open general sockets
// - microcontrollers may want to minimize code and data size
//
// O2lite solves this by connecting to a single O2 host rather than all 
// O2 hosts. This minimizes connections, but in general, it requires 2 
// hops to send a message to an arbitrary O2 service. The connected O2
// host serves as an intermediary. O2 supports connections through 
// websockets and shared memory as well as TCP/IP.
//
// O2LITE INTRODUCTION
//
// Terminology: 
// - Host or host process: A full O2 implementation that can "host" an
//   O2lite connection  
// - O2lite process: Another process (microcontroller or whatever)  
//   running the O2lite protocol to connect to O2 through a host process.  
//
// Naming
// 
// An O2lite service is considered to belong to the O2 host. The O2
// host acts as a "proxy" for the O2lite process. Therefore:
// - Messages to a service offered by an O2lite process are directed
//   to the host and forwarded to the O2lite process.
// - An O2lite service is "global" and reachable by any process
//   (perhaps indirectly) in the O2 ensemble. 
// - If an O2lite process creates a service that exists in the host,
//   the new service will be ignored by the host. (Currently, no 
//   error is sent back to the O2lite process.)
// - Similarly, if an O2 host tries to create a service already 
//   offered by a connected O2lite process, the attempt fails and 
//   an error is returned.
// - The priority of an O2lite process service is determined by 
//   the IP address of the host, not that of the O2lite process.
//   The priority could depend on which host the O2lite process
//   connects to if there are multiple hosts available on the 
//   local area network.
//
// INSTALLATION FOR ARDUINO ESP32
// 
// O2lite test programs can be found in test/* and work under macOS,
// Linux, and Windows. O2lite has been tested on ESP32 Thing board 
// from Spark Fun, Inc. (hopefully more in the future), using the
// Arduino development environment. Rather than making an installable
// library, I kept the code more accessible and debuggable by simply
// including it in a project. To compile with O2lite, you should:
// - create a src subdirectory in your Arduino project. This is
//   easily done *outside* the Arduino IDE, once you find where it
//   stores projects.
// - copy the following to your ESP_project folder:
//       o2/o2lite.c
//       o2/o2lite.h
//       o2/o2base.h
//       o2/hostipimpl.h
//       o2/hostip.h
//       o2/o2liteesp32.cpp
// - add an #include to code where you make calls to o2lite:
//   #include "o2lite.h"
// You will need at least the .c and .cpp files to appear in tabs
// in the Arduino IDE. This tells the IDE to compile and link them.
// The Arduino IDE should take care of the rest.. It will compile
// and link your project with o2lite.c and o2liteesp32.cpp. (Do not
// use hostip.c with o2lite -- it is for o2 only.)
//
// See also ../arduino/README.md
//
// IMPLEMENTATION NOTES
//
// Discovery uses Bonjour (or Avahi for Linux). The O2lite process
// uses the browse functions of Bonjour/Avahi to discover an O2 host.
// There are multiple files implementing discovery depending on the 
// system: o2litebonjour.c, o2liteavahi.c, o2liteesp32.c, 
// o2litedisc.c (the latter is a legacy implementation that can 
// only be used by *disabling* the definition of O2_NO_O2DISCOVERY,
// which is normally defined in the CMakeLists.txt file). See
// DISCOVERY API below for details.
//
// To attach to an O2 host, the O2lite process (as client) makes 
// a TCP connection to an O2host. As soon as the O2lite process 
// connects, it sends "!_o2/o2lite/con":
//    local ip (hex string) -- the O2lite process local IP address 
//    port (int32) -- the O2lite process udp port number 
//
// When the host receives "!_o2/o2lite/con", it replies with "!_o2/id"
// with an integer ID number that gives the O2lite process a unique
// identity when combined with the host name.
//
// Once the ID is received, the O2lite process sends "!_o2/o2lite/sv"
// with a service description for each service (services can be created
// before the connection is made, and they are retained in a simple data
// structure):
// - service_name (string) -- the service name
// - exists (int32) -- 1 if the service exists, 0 if the service is
//        removed
// - is_service (int32) -- 1 for service, 0 for tap (should always be 1)
// - properties (string) -- service properties (currently always empty)
//
// A small library is used to construct messages, supporting int32,
// float, time, and string types. (Other types might be supported in 
// the future or with library options.)
//
// To receive messages, the receiver handler uses a table mapping
// addresses to handler functions, and linear search is used, based on
// the assumption that the total number of addresses will be minimal and
// known at compile time. The handler function uses low-level
// functions to fetch parameters sequentially from the message, again to
// simplify the implementation and reduce the need for dynamic memory
// allocation. 
//
// Clock sync is optional: the host holds messages and delivers
// them according to their timestamps. This eliminates some code 
// for the O2lite process, but delays message delivery by the 
// somewhat random network latency. 
//
// If clock sync is enabled, the client can add timestamps to 
// outgoing messages. It can also interpret timestamps on incoming
// messages. For example, a simple O2lite process could add a constant
// offset D to each timestamp to determine the actual delivery time,
// and the message handlers can simply stop processing until the
// time of the timestamp. This would implement a simple "forward 
// synchronous" policy, assuming all senders can send messages 
// ahead of time and can offset their timestamps by -D. The senders
// must also insure timestamps are strictly increasing (the O2 host
// will sort messages by timestamp when sending but only if the timestamp
// is for a future time.
//
// Note: eliminating scheduling in O2lite eliminates scheduling code,
// eliminates the need for the clock sync protocol (it can be disabled
// at compile time by defining O2L_NO_CLOCKSYNC), and allows the use
// of a single static message buffer (256 bytes) rather than copying 
// every message to the heap as in O2.
//
// Clock sync is accomplished by sending "!_o2/o2lite/cs/get" to 
// the host with these parameters:
// - bridge_id (int32) -- the ID number for this bridge instance
// - serial_no (int32) -- a serial number generated by O2lite process
// - reply_to (string) -- address for reply, which will be
//        "!_o2/cs/put"
// The reply message to "!_o2/cs/put" (via UDP) will contain:
// - serial_no (int32) -- serial number sent by O2lite process
// - time -- the host's O2 time
//
// The O2lite process will send a /_cs/get message periodically and
// compute the mapping from O2lite process clock to O2 time in a 
// similar fashion to O2's clock sync.
//
// When clock sync is obtained, O2lite process sends
// "!_o2/o2lite/cs/cs" with no parameters.
//
// DISCOVERY API
//
// This o2lite.c file depends on another file to implement discovery
// functions. This allows alternate discovery methods to be compiled:
// macOS Bonjour, esp32 Bonjour or Linux Avahi. The original O2
// discovery (now obsolete) is woven into the o2lite network code and
// is included in this o2lite.c source file, but only enabled by
// setting O2_NO_ZEROCONF and not setting O2_NO_O2DISCOVERY. The 
// corresponding files use conditional compilation so you can 
// simply compile them all and the appropriate one will produce 
// the required code.
// 
// The interface between o2lite and discovery is as follows (we will
// call the external code "discovery", e.g. discovery on macOS could 
// be o2/src/o2litebonjour.c.
//
// o2ldisc_poll() is provided by discovery and is called by o2lite
//     from network_poll() which is in turn called by o2l_poll(). 
//     discovery can assume this is called frequently.
//
// o2ldisc_init(const char *ensemble) is provided by discovery and
//     is called by o2lite from o2l_initialize(). The ensemble is
//     the O2 ensemble name, and discovery should connect to an O2 
//     process in this ensemble. This function should return 
//     O2L_SUCCESS or O2L_FAIL. If 
//
// o2l_network_connect(const char *ip, int port) is provided by
//     o2lite.c and called by discovery with the IP address in
//     dot format (128.2.100.78) and the TCP port number of the
//     O2 host. No value is returned. If successful, tcp_sock is 
//     set; otherwise, tcp_sock will be INVALID_SOCKET and discovery
//     should continue.
// 
// SOCKET tcp_sock is provided by o2lite.c and initialized to
//     INVALID_SOCKET (from o2lite.h). When tcp_sock is 
//     INVALID_SOCKET, there is no host connection or the host
//     connection has been dropped, so discovery should look for
//     a host. When tcp_sock is valid, there is a host and there
//     is nothing to be done by discovery. It can shut down as 
//     long as it is prepared to restart when tcp_sock becomes
//     invalid. These decisions are all made in o2ldisc_poll().
//     (The esp32 implementation currently uses synchronous or
//     blocking discovery, which runs every 5 seconds until a 
//     host is found.)
// 
// o2l_time o2l_local_now gives the current local time which can
//    be used for scheduling actions in o2ldisc_poll(). There is
//    no scheduled event facility in this O2lite implementation.


#include "o2lite.h"
#include <string.h>
#include <ctype.h>
// IMPORTANT: hostipint.h is the "internal" include file that should
// be included only in one place (here) because it contains the 
// implementation. The implementation is not in a .c file because
// if you compile it separately, it will expect to link with 
// o2_dbg_malloc; but in o2lite, we have no o2_dbg_malloc and set
// up macros in o2lite.h to override this default.
#include "hostipimpl.h"

#if !defined(O2_NO_ZEROCONF) && !defined(O2_NO_O2DISCOVERY)
// O2 ensembles should adopt one of two discovery methods. If ZeroConf
// works out, the built-in O2 discovery mechanism will be removed entirely.
// Whatever method is used by O2, this O2lite library must do the same.
#error O2lite supporte either ZeroConf or built-in discovery, but not both.
#error One of O2_NO_ZEROCONF or O2_NO_O2DISCOVERY must be defined
#endif

// PTR is a machine address to which you can add byte offsets
#define PTR(addr) ((char *) (addr))
// get address of first 32-bit word boundary at or above ptr:
#define ROUNDUP(ptr) ((char *)((((size_t) ptr) + 3) & ~3))

char o2n_internal_ip[O2N_IP_LEN] = "";

void o2l_dispatch(o2l_msg_ptr msg);
static void find_my_ip_address();

static const char *o2l_services = NULL;
const char *o2l_ensemble = NULL;

#ifdef O2LDEBUG
// verbose enables extra debugging output
int verbose = 0;
#endif

// on by holding 
#ifdef WIN32
/****************************WINDOWS***********************************/
// #include <winsock2.h> -- already included in o2lite.h
#include <ws2tcpip.h>
#define TERMINATING_SOCKET_ERROR \
    (WSAGetLastError() != WSAEWOULDBLOCK && WSAGetLastError() != WSAEINTR)

static long start_time;

/***************************APPLE and LINUX****************************/
#elif defined(__APPLE__) || defined(__linux__)
#include "arpa/inet.h"  // Headers for all inet functions
#include <unistd.h>    // define close()
#include "sys/ioctl.h"
#include <ifaddrs.h>
#include <netinet/tcp.h>
#include <errno.h>
// #include <sys/select.h>  -- already included in o2lite.h
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>

#define TERMINATING_SOCKET_ERROR (errno != EAGAIN && errno != EINTR)
#define closesocket close

/**** a few things are not common to both APPLE and LINUX *******/
#ifdef __APPLE__
#include "CoreAudio/HostTime.h"
static uint64_t start_time;
#else
static long start_time;
#endif

/*********************************ESP32********************************/
#elif ESP32
#include <lwip/sockets.h>
#include <lwip/netdb.h>

//#include "esp32-hal.h"
 
typedef int SOCKET;  // In O2lite, SOCKET denotes the type of a socket
#define TERMINATING_SOCKET_ERROR (errno != EAGAIN && errno != EINTR)
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1

/* Note: we assume caller sets up serial interface and Wifi:
const char *network_name = "your network name here";
const char *network_pswd = "your network password here";

void setup() {
    Serial.begin(115200);
    connect_to_wifi(hostname, network_name, network_pswd);
    ...
*/

#else
#error no environment has been defined; 
#error expecting WIN32, __APPLE__, __linux__, or ESP32
#endif


int o2l_address_init(struct sockaddr_in *sa, const char *ip,
                     int port_num, bool tcp);


/******* MESSAGES *********/

char tcpinbuf[MAX_MSG_LEN];
char udpinbuf[MAX_MSG_LEN];
char outbuf[MAX_MSG_LEN];

o2l_time o2l_local_now = -1;

static int tcp_len_got = 0;
static int tcp_msg_got = 0;
static o2l_msg_ptr udp_in_msg = (o2l_msg_ptr) udpinbuf;
static o2l_msg_ptr tcp_in_msg = (o2l_msg_ptr) tcpinbuf;
o2l_msg_ptr out_msg = (o2l_msg_ptr) outbuf;
static o2l_msg_ptr parse_msg; // incoming message to parse
static const char *parse_type;      // next type to retrieve from parse_msg
static int parse_cnt;         // how many bytes retrieved
static int max_parse_cnt;     // how many bytes can be retrieved
static bool parse_error;      // was there an error parsing message?
int out_msg_cnt;              // how many bytes written to outbuf


// convert 8-char, 32-bit hex representation to dot-notation,
//   e.g. "7f000001" converts to "127.0.0.1"
//   dot must be a string of length 16 or more
void o2l_hex_to_dot(const char *hex, char *dot)
{
    int i1 = o2_hex_to_byte(hex);
    int i2 = o2_hex_to_byte(hex + 2);
    int i3 = o2_hex_to_byte(hex + 4);
    int i4 = o2_hex_to_byte(hex + 6);
    snprintf(dot, 16, "%d.%d.%d.%d", i1, i2, i3, i4);
}


// get data from current message, which is in network order

// return timestamp from message
double o2l_get_timestamp()
{
    int64_t t = (int64_t) (parse_msg->timestamp);
    t = o2lswap64(t);
    return *(double *)&t;
}


// was there an error in parsing?
bool o2l_get_error()
{
    return parse_error;
}


static void report_length_error()
{
    O2LDB printf("o2lite: parse error reading message to %s,"
                 " message too short\n", parse_msg->address);
    parse_error = true;
}
    

static void report_type_error(char typecode)
{
    char got[4];
    got[0] = parse_type[-1];
    got[1] = 0;
    if (typecode == 0) {
        strncpy(got, "EOS", 4);
    }
    O2LDB printf("o2lite: parse error reading message to %s,"
                 " expected type %c but got type %s\n",
                 parse_msg->address, typecode, got);
    parse_error = true;
}


#define CHECKERROR(typ, typecode) \
    if (parse_cnt + sizeof(typ) > max_parse_cnt) { \
        report_length_error(); return 0; \
    } else if (*parse_type++ != typecode) { \
        report_type_error(typecode); return 0; }


#define CURDATAADDR(typ) ((typ) ((char *) parse_msg + parse_cnt))

#define CURDATA(var, typ, typecode) CHECKERROR(typ, typecode) \
    typ var = *CURDATAADDR(typ *); parse_cnt += sizeof(typ);


double o2l_get_time()
{
    CURDATA(t, int64_t, 't');
    t = o2lswap64(t);
    return *(double *)&t;
}

float o2l_get_float()
{
    CURDATA(x, int32_t, 'f');
    x = o2lswap32(x);
    return *(float *)&x;
}

int32_t o2l_get_int32()
{
    CURDATA(i, int32_t, 'i');
    return o2lswap32(i);
}

char *o2l_get_string()
{
    CHECKERROR(char *, 's')
    char *s = CURDATAADDR(char *);
    int len = (int) strlen(s);
    parse_cnt += (len + 4) & ~3;
    return s;
}

void o2l_add_string(const char *s)
{
    while (*s) {
        // we still need to write *s and EOS, so need space for 2 chars:
        if (out_msg_cnt + 2 > MAX_MSG_LEN) {
            parse_error = true;
            return;
        }
        outbuf[out_msg_cnt++] = *s++;
    }
    // write eos
    outbuf[out_msg_cnt++] = 0;
    // fill to word boundary
    while (out_msg_cnt & 0x3) outbuf[out_msg_cnt++] = 0;
}


void o2l_add_time(double time)
{
    if (out_msg_cnt + sizeof(double) > MAX_MSG_LEN) {
        parse_error = true;
        return;
    }
    int64_t t = o2lswap64(*(int64_t *)&time);
    *(int64_t *)(outbuf + out_msg_cnt) = t;
    out_msg_cnt += sizeof(double);
}


void o2l_add_float(float x)
{
    if (out_msg_cnt + sizeof(float) > MAX_MSG_LEN) {
        parse_error = true;
        return;
    }
    int32_t xi = o2lswap32(*(int32_t *)&x);
    *(int32_t *)(outbuf + out_msg_cnt) = xi;
    out_msg_cnt += sizeof(float);
}


void o2l_add_int32(int32_t i)
{
    if (out_msg_cnt + sizeof(int32_t) > MAX_MSG_LEN) {
        parse_error = true;
        return;
    }
    *(int32_t *)(outbuf + out_msg_cnt) = o2lswap32(i);
    out_msg_cnt += sizeof(int32_t);
}


void o2l_send_start(const char *address, o2l_time time, 
		    const char *types, bool tcp)
{
    parse_error = false;
    out_msg_cnt = sizeof out_msg->length;
    o2l_add_int32(tcp ? O2_TCP_FLAG : O2_UDP_FLAG);
    o2l_add_time(time);
    o2l_add_string(address);
    outbuf[out_msg_cnt++] = ','; // type strings have a leading ','
    o2l_add_string(types);
}


/******* NETWORKING *******/
// provide udp_recv_sock for incoming messages, chosen from o2_port_map
// provide broadcast_sock for outgoing discovery messages (UDP)
// provide udp_send_sock for outgoing udp messages to O2
// provide for connecting a tcp_sock to O2 for 2-way communication


int udp_recv_port = 0;
SOCKET udp_recv_sock = INVALID_SOCKET;

struct sockaddr_in udp_server_sa;
SOCKET udp_send_sock = INVALID_SOCKET;

int tcp_port = 0;
struct sockaddr_in tcp_server_sa;
SOCKET tcp_sock = INVALID_SOCKET;

static struct sockaddr_in server_addr;

int o2l_bridge_id = -1; // unique id for this process's connection to O2

#ifndef O2_NO_O2DISCOVERY
unsigned short o2_port_map[PORT_MAX] = {
                                64541, 60238, 57143, 55764, 56975, 62711,
                                57571, 53472, 51779, 63714, 53304, 61696,
                                50665, 49404, 64828, 54859 };
#endif


int o2l_bind_recv_socket(SOCKET sock, int *port)
{
    // bind server port
    memset(PTR(&server_addr), 0, sizeof server_addr);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = o2lswap32(INADDR_ANY); // local IP address
    server_addr.sin_port = o2lswap16(*port);
    unsigned int yes = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                   PTR(&yes), sizeof yes) < 0) {
        perror("setsockopt(SO_REUSEADDR)");
        return O2L_FAIL;
    }
    if (bind(sock, (struct sockaddr *) &server_addr, sizeof server_addr)) {
        return O2L_FAIL;
    }
    if (*port == 0) { // find the port that was (possibly) allocated
        socklen_t addr_len = sizeof server_addr;
        if (getsockname(sock, (struct sockaddr *) &server_addr, &addr_len)) {
            perror("getsockname call to get port number");
            return O2L_FAIL;
        }
        *port = o2lswap16(server_addr.sin_port);  // set actual port used
    }
    return O2L_SUCCESS;
}


// initializes tcp_server_sa with server address and port
//
int o2l_address_init(struct sockaddr_in *sa, const char *ip, int port_num,
                     bool tcp)
{
    char port[24];
    sprintf(port, "%d", port_num);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    if (tcp) {
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
    } else {
        hints.ai_family = PF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
    }
    struct addrinfo *aiptr = NULL;
    if (getaddrinfo(ip, port, &hints, &aiptr)) {
        return O2L_FAIL;
    }
    memcpy(sa, aiptr->ai_addr, sizeof *sa);
    if (sa->sin_port == 0) {
        sa->sin_port = o2lswap16((short) port_num);
    }
    if (aiptr) freeaddrinfo(aiptr);
    return O2L_SUCCESS;
}


void o2l_send_services()
{
    const char *s = o2l_services;
    if (o2l_bridge_id < 0) {
        return;
    }
    while (s && *s) { // until we reach end of services string
        // invariant: s points to beginning of first/next service
        char name[64];
        char *nptr = name;
        while (*s != ',' && *s) { // copy from s to name
            // invariant: s point to next character to copy
            if (nptr - name > 31) { // name too long
                printf("service name too long\n");
                return;
            }
            *nptr++ = *s++;
        }
        // invariant: s points after service, nptr points to end of name
        *nptr = 0;
        if (nptr == name) continue; // skip comma
        o2l_send_start("!_o2/o2lite/sv", 0, "siisi", true);
        o2l_add_string(name);
        o2l_add_int32(1); // exists
        o2l_add_int32(1); // this is a service
        o2l_add_string(""); // no properties
        o2l_add_int32(0); // send_mode is ignored for services
        o2l_send();
        if (*s == ',') { // establish the invariant at top of loop
            s++;
        }
    }
}


// connect our TCP port to O2 IP:port server address - this is the final
// step of successful discovery
void o2l_network_connect(const char *ip, int port)
{
    o2l_address_init(&tcp_server_sa, ip, port, true); // sets tcp_server_sa
    O2LDB printf("o2lite: discovered and connecting to %s port %d\n", ip, port);
    tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(tcp_sock, (struct sockaddr *) &tcp_server_sa,
                sizeof tcp_server_sa) == -1) {
        perror("o2lite o2l_network_connect");
        tcp_sock = INVALID_SOCKET;
        return;
    }
#ifdef __APPLE__
    int set = 1;
    setsockopt(tcp_sock, SOL_SOCKET, SO_NOSIGPIPE, (void *) &set, sizeof set);
#endif
    O2LDB printf("o2lite: connected to a host\n");
    // send back !_o2/o2lite/con ipaddress updport
    o2l_send_start("!_o2/o2lite/con", 0, "si", true);
    O2LDB printf("o2lite: sending !_o2/o2lite/con %s %x\n", 
                 o2n_internal_ip, udp_recv_port);
    o2l_add_string(o2n_internal_ip);
    o2l_add_int(udp_recv_port);
    o2l_send();
    return;
}


void cleanup_tcp_msg()
{
    tcp_in_msg->length = 0;
    tcp_len_got = 0;
    tcp_msg_got = 0;
}

void disconnect()
{
    if (tcp_sock != INVALID_SOCKET) {
        closesocket(tcp_sock);
    }
    tcp_sock = INVALID_SOCKET;
    o2l_bridge_id = -1;
}


void read_from_tcp()
{
    int n;
    if (tcp_len_got < 4) {
        n = (int) recvfrom(tcp_sock, PTR(&tcp_in_msg->length) +
                           tcp_len_got, 4 - tcp_len_got, 0, NULL, NULL);
        if (n <= 0) {
            goto error_exit;
        }
        tcp_len_got += n;
        if (tcp_len_got == 4) { // done receiving tcp_in_msg->length
            tcp_in_msg->length = o2lswap32(tcp_in_msg->length);
            int capacity = MAX_MSG_LEN - sizeof tcp_in_msg->length;
            if (tcp_in_msg->length > capacity) {
                // throw out message if length is too long. This is tricky
                // because if we do not read exactly the right number of
                // bytes, we'll lose sync and will not find the next
                // message. We'll block until we read and discard the right
                // number of bytes from the stream.
                while (tcp_msg_got < tcp_in_msg->length) {
                    int togo = tcp_in_msg->length - tcp_msg_got;
                    if (togo > capacity) togo = capacity;
                    n = recvfrom(tcp_sock, PTR(&tcp_in_msg->misc), togo, 
                                 0, NULL, NULL);
                    if (n <= 0) {
                        goto error_exit;
                    }
                    tcp_msg_got += n;
                }
                cleanup_tcp_msg();
                return;
            }
        }
    }
    if (tcp_msg_got < tcp_in_msg->length) {
        n = (int) recvfrom(tcp_sock, PTR(&tcp_in_msg->misc) + tcp_msg_got,
                           tcp_in_msg->length - tcp_msg_got, 0, NULL, NULL);
        if (n <= 0) { 
            goto error_exit;
        }
        tcp_msg_got += n;
        if (tcp_msg_got < tcp_in_msg->length) {
            return; // incomplete message, wait for more
        }
    }
    o2l_dispatch(tcp_in_msg);
    cleanup_tcp_msg();
    return;
  error_exit:
    if (n < 0 && !TERMINATING_SOCKET_ERROR) {
        return; // incomplete message, maybe we were interrupted
    }
    cleanup_tcp_msg();
    disconnect();
    O2LDB printf("o2lite: TCP receive error, disconnected from O2\n");
}


int read_from_udp()
{
    int n; // note that length is not part of message; length == n
    if ((n = (int) recvfrom(udp_recv_sock, udpinbuf + sizeof(int32_t), 
                            MAX_MSG_LEN, 0, NULL, NULL)) <= 0) {
        // I think udp errors should be ignored. UDP is not reliable
        // anyway. For now, though, let's at least print errors.
        // Any payload larger than MAX_MSG_LEN will be truncated,
        // EMSGSIZE will be returned, and we will ignore the message.
        perror("recvfrom in udp_recv_handler");
        return O2L_FAIL;
    }
    udp_in_msg->length = n;
    o2l_dispatch(udp_in_msg);
    return O2L_SUCCESS;
}


static fd_set read_set;
static int nfds;
struct timeval no_timeout = {0, 0};

void o2l_add_socket(SOCKET s)
{
    if (s != INVALID_SOCKET) {
        FD_SET(s, &read_set);
        // Windows socket is not an int, but Windows does not care about
        // the value of nfds, so it's OK even if this cast loses data
        if (s >= nfds) nfds = (int) (s + 1);
    }
}


void network_poll()
{
    nfds = 0;
    FD_ZERO(&read_set);
    o2l_add_socket(tcp_sock);
    if (tcp_sock != INVALID_SOCKET) {  // only accept UDP if TCP is connected
        o2l_add_socket(udp_recv_sock); // to an O2 host process.
    }
    o2ldisc_poll();

    int total;
    O2LDBV printf("select: udp_recv_sock %d tcp_sock %d nfds %d\n",
                  (int) udp_recv_sock, (int) tcp_sock, nfds);
    if (nfds == 0) { // we are not expecting incoming messages;
        return;      // not sure what select does with nfds == 0,
    }                // but it is safe to return in this case.
    if ((total = select(nfds, &read_set, NULL, NULL,
                        &no_timeout)) == SOCKET_ERROR) {
        return;
    }
    if (total == 0) { /* no messages waiting */
        return;
    }
    if (tcp_sock != INVALID_SOCKET) {
        if (FD_ISSET(tcp_sock, &read_set)) {
            // O2LDB printf("o2lite: network_poll got TCP msg\n");
            read_from_tcp();
        }
    }
    if (FD_ISSET(udp_recv_sock, &read_set)) {
        // O2LDB printf("o2lite: network_poll got UDP msg\n");
        read_from_udp();
    }

    o2ldisc_events(&read_set);
}


void o2l_send()
{
    if (parse_error || tcp_sock == INVALID_SOCKET) {
        return;
    }
    out_msg->length = o2lswap32(out_msg_cnt - sizeof out_msg->length);
    if (out_msg->misc & o2lswap32(O2_TCP_FLAG)) {
        send(tcp_sock, outbuf, out_msg_cnt, 0);
    } else {
        if (sendto(udp_send_sock, outbuf + sizeof out_msg->length,
                   out_msg_cnt - sizeof out_msg->length, 0,
                   (struct sockaddr *) &udp_server_sa,
                   sizeof udp_server_sa) < 0) {
            perror("Error attempting to send udp message");
            printf("Address: %s, socket: %d\n", 
                   out_msg->address, (int) udp_send_sock);
        }
    }
}


/******* MESSAGE DISPATCH *********/

typedef struct o2l_method {
    struct o2l_method *next;
    const char *address;
    const char *typespec;
    bool full; // match full address
    o2l_handler handler;
    void *info; // to pass to handler function
} o2l_method, *o2l_method_ptr;


static o2l_method_ptr methods = NULL;


void o2l_method_new(const char *path, const char *typespec,
                    bool full, o2l_handler h, void *info)
{
    o2l_method_ptr mp = O2_MALLOCT(o2l_method);
    assert(mp);
    mp->next = methods;
    mp->address = path;
    mp->typespec = typespec;
    mp->full = full;
    mp->handler = h;
    mp->info = info;
    methods = mp;
}


// announce that we are offering services. services must be a static
// string or allocated on the heap (this interface assumes the string
// remains for the lifetime of the program). services is a list of 
// service names separated by ",", with no "/" characters.
//
void o2l_set_services(const char *services)
{
    o2l_services = services;
    o2l_send_services();
}


// send a message. msg is in network byte order except for msg->length
//
void o2l_dispatch(o2l_msg_ptr msg)
{
    const char *addr = msg->address;
    const char *typespec = ROUNDUP(addr + strlen(addr) + 1); // skip ','
    const char *data = ROUNDUP(typespec + strlen(typespec) + 1);
    o2l_method_ptr m = methods;
    while (m) {
        if (m->full) { // must match full address, but ignore initial ! or /
            if (!streql(m->address + 1, addr + 1) || // skip ','
                (m->typespec != NULL && !streql(m->typespec, typespec + 1))) {
                goto no_match;
            }
        } else { // allow an exact match OR a match up to / in the message
            const char *pre = m->address + 1;
            const char *str = addr + 1;
            while (*pre) {
                if (*pre++ != *str++) goto no_match;
            }
            if ((*str != 0 && *str != '/') ||
                (m->typespec != NULL && !streql(m->typespec, typespec + 1))) {
                goto no_match;
            }
        }
        parse_msg = msg;
        parse_type = typespec + 1;
        parse_cnt = (int) (data - (char *) msg);
        parse_error = false;
        max_parse_cnt = sizeof msg->length + msg->length;
        (*m->handler)(msg, parse_type, (void *) data, m->info);
        return;
      no_match:
        m = m->next;
    }
    O2LDB printf("o2l_dispatch dropping msg to %s\n", addr);
}


#ifndef O2L_NO_CLOCKSYNC

/******* CLOCK ********/

#define CLOCK_SYNC_HISTORY_LEN 5

static bool clock_initialized = false;
static bool clock_synchronized = false;
static o2l_time global_minus_local;
static o2l_time rtts[CLOCK_SYNC_HISTORY_LEN];
static o2l_time ref_minus_local[CLOCK_SYNC_HISTORY_LEN];
static o2l_time start_sync_time; // when did we start syncing
static o2l_time time_for_clock_ping = 1e7; // about 100 days = never
static int clock_sync_id = 0;
static o2l_time clock_ping_send_time;
static int ping_reply_count = 0;

static void o2l_clock_finish()
{
#ifdef WIN32
    timeEndPeriod(1); // give up 1ms resolution for Windows
#endif
    clock_initialized = false;
}    

// handler for "!_o2/cs/put"
//
static void ping_reply_handler(o2l_msg_ptr msg, const char *types,
                                   void *data, void *info)
{
    int id = o2l_get_int32();
    // O2LDB printf("o2lite: ping_reply_handler called with id %d "
    //              "(expecting  %d)\n", id, clock_sync_id);
    if (id != clock_sync_id) {
        return;
    }
    o2l_time rtt = o2l_local_now - clock_ping_send_time;
    o2l_time ref_time = (o2l_time) (o2l_get_time() + rtt * 0.5);
    //O2LDB printf("o2lite: ping_reply_handler now %g rtt %g ref %g error %d\n",
    //             o2l_local_now, rtt, ref_time, parse_error);
    if (parse_error) {
        return; // error parsing message
    }
    int i = ping_reply_count++ % CLOCK_SYNC_HISTORY_LEN;
    rtts[i] = rtt;
    ref_minus_local[i] = ref_time - o2l_local_now;
    // O2LDB printf("o2lite: ping_reply_handler at %g count %d offset %g\n",
    //              o2l_local_now, ping_reply_count, ref_minus_local[i]);
    if (ping_reply_count >= CLOCK_SYNC_HISTORY_LEN) {
        // find minimum round trip time
        o2l_time min_rtt = rtts[0];
        int best_i = 0;
        for (i = 1; i < CLOCK_SYNC_HISTORY_LEN; i++) {
            if (rtts[i] < min_rtt) {
                min_rtt = rtts[i];
                best_i = i;
            }
        }
        o2l_time new_gml = ref_minus_local[best_i];
        if (!clock_synchronized) { // set global clock to our best estimate
            O2LDB printf("o2lite: clock synchronized\n");
            clock_synchronized = true;
            o2l_send_start("!_o2/o2lite/cs/cs", 0, "", true);
            o2l_send(); // notify O2 via tcp
            global_minus_local = new_gml;
        } else { // avoid big jumps when error is small. Set clock if error
            // is greater than min_rtt. Otherwise, bump by 2ms toward estimate.
            o2l_time bump = 0.0;
            o2l_time upper = new_gml + min_rtt;
            o2l_time lower = new_gml - min_rtt;
            // clip to [lower, upper] if outside range
            if (global_minus_local < lower) {
                global_minus_local = lower;
            } else if (global_minus_local > upper) {
                global_minus_local = upper;
            } else if (global_minus_local < new_gml - 0.002) {
                bump = 0.002F; // increase by 2ms if too low by more than 2ms
            } else if (global_minus_local > new_gml + 0.002) {
                bump = -0.002F; // decrease by 2ms is too high by more then 2ms
            } else {  // set exactly to estimate
                bump = new_gml - global_minus_local;
            }
            global_minus_local += bump;
        }
    }
}


static void o2l_clock_initialize(void)
{
    if (clock_initialized) {
        o2l_clock_finish();
    }
#ifndef O2L_NO_CLOCKSYNC
    o2l_method_new("!_o2/cs/put", "it", true,
                   &ping_reply_handler, NULL);
#endif
#ifdef __APPLE__
    start_time = AudioGetCurrentHostTime();
#elif __linux__
    struct timeval tv;
    gettimeofday(&tv, NULL);
    start_time = tv.tv_sec;
#elif WIN32
    timeBeginPeriod(1); // get 1ms resolution on Windows
    start_time = timeGetTime();
#elif ESP32
    // millis() is available, no work here!
#else
#error o2_clock has no implementation for this system
#endif
    // until local clock is synchronized, LOCAL_TO_GLOBAL will return -1:
    global_minus_local = 0;

    clock_synchronized = false;
    ping_reply_count = 0;
}


o2l_time o2l_time_get()
{
    if (clock_synchronized) {
        return o2l_local_time() + global_minus_local;
    } else {
        return -1;
    }
}


// send a clock ping (/_o2/o2lite/cs/get) and schedule next one
//
static void clock_ping()
{
    clock_ping_send_time = o2l_local_now;
    clock_sync_id++;
    o2l_send_start("!_o2/o2lite/cs/get", 0, "iis", false);
    o2l_add_int32(o2l_bridge_id);
    o2l_add_int32(clock_sync_id);
    o2l_add_string("!_o2/cs/put");
    o2l_send();
    time_for_clock_ping = clock_ping_send_time + 0.1F;
    if (clock_ping_send_time - start_sync_time > 1)
        time_for_clock_ping += 0.4F;
    if (clock_ping_send_time - start_sync_time > 5)
        time_for_clock_ping += 9.5F;
    // O2LDB printf("clock_ping sent at %g id %d\n", 
    //              clock_ping_send_time, clock_sync_id);
}

#else
o2l_time o2l_time_get() { return -1; }
#endif


o2l_time o2l_local_time()
{
#ifdef __APPLE__
    uint64_t clock_time, nsec_time;
    clock_time = AudioGetCurrentHostTime() - start_time;
    nsec_time = AudioConvertHostTimeToNanos(clock_time);
    return ((o2l_time) (nsec_time * 1.0E-9));
#elif __linux__
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((tv.tv_sec - start_time) + (tv.tv_usec * 0.000001));
#elif WIN32
    return (o2l_time) ((timeGetTime() - start_time) * 0.001);
#elif ESP32
    // casting intended to do a double multiply or a float multiply,
    // depending on o2l_time's definition.
    return millis() * (o2l_time) 0.001F;
#else
#error o2_clock has no implementation for this system
#endif
}

#ifndef O2L_NO_BROADCAST
/******* DISCOVERY *********/

#define O2_DY_INFO 50
#define RATE_DECAY 1.125
#define PORT_MAX 16

static o2l_time disc_period = 0.1F;
static o2l_time time_for_discovery_send = 0;
int next_disc_index = -1;

int o2l_broadcast(int port)
{
    broadcast_to_addr.sin_port = o2lswap16(port);
    if (sendto(broadcast_sock, outbuf + sizeof out_msg->length,
               out_msg_cnt - sizeof out_msg->length, 0,
               (struct sockaddr *) &broadcast_to_addr,
               sizeof broadcast_to_addr) < 0) {
        perror("Error attempting to broadcast discovery message");
        return O2L_FAIL;
    }
    return O2L_SUCCESS;
}


static void make_dy()
{
    o2l_send_start("!_o2/o2lite/dy", 0, "ssiii", false);
    o2l_add_string(o2l_ensemble);
    o2l_add_string(o2n_internal_ip);
    o2l_add_int(tcp_port);
    o2l_add_int(udp_recv_port);
    o2l_add_int(O2_DY_INFO);
}

static void discovery_send()
{
    // send to all discovery ports
    next_disc_index = (next_disc_index + 1) % PORT_MAX;
    make_dy();
    int port = o2_port_map[next_disc_index];
    if (port) {
        if (!o2l_broadcast(port)) {
            O2LDB printf("o2lite: broadcast !_o2/o2lite/dy to port %d at %g\n",
                         port, o2l_local_now);
            o2_port_map[next_disc_index] = 0; // disable port after failure
        }
    }
    time_for_discovery_send = o2l_local_now + disc_period;
    disc_period *= RATE_DECAY;
    if (disc_period > 4.0) {
        disc_period = 4.0;
    }
}
#endif

static void find_my_ip_address()
{
#ifndef ESP32 
    o2n_get_internal_ip(o2n_internal_ip);
    char dot_ip[O2N_IP_LEN];
    o2_hex_to_dot(o2n_internal_ip, dot_ip);
    O2LDB printf("o2lite: local ip address is %s (%s)\n",
	 	 o2n_internal_ip, dot_ip);
#endif
    return;
}


#ifndef O2_NO_O2DISCOVERY
// handler for "!_o2/dy" messages from O2 hosts
//
static void o2l_dy_handler(o2l_msg_ptr msg, const char *types,
                           void *data, void *info)
{
    if (tcp_sock != INVALID_SOCKET) { // already connected
        return;
    }
    const char *ens = o2l_get_string();
    int version = o2l_get_int32();
    o2l_get_string();  // assume host is local; ignore public
    const char *iip = o2l_get_string();  // here is the internal (local) IP
    int tcp_port = o2l_get_int32();
    int udp_port = o2l_get_int32();
    if (parse_error || !streql(ens, o2l_ensemble) ||
        (version & 0xFF0000) != (O2L_VERSION & 0xFF0000)) {
        return; // error parsing message
    }
    char iip_dot[16];
    o2_hex_to_dot(iip, iip_dot);
    o2l_address_init(&udp_server_sa, iip_dot, udp_port, false);
    o2l_network_connect(iip_dot, tcp_port);
}
#endif

// Handler for !_o2/id message
static void o2l_id_handler(o2l_msg_ptr msg, const char *types,
                           void *data, void *info)
{
    o2l_bridge_id = o2l_get_int32();
    O2LDB printf("o2lite: got id = %d\n", o2l_bridge_id);
    // we're connected now, send services if any
    o2l_send_services();
#ifndef O2L_NO_CLOCKSYNC
    // Sends are synchronous. Since we just sent a bunch of messages,
    // take 50ms to service any other real-time tasks before this:
    time_for_clock_ping = o2l_local_now + 0.05F;
    start_sync_time = time_for_clock_ping; // when does syncing start?
#endif
}

#ifndef O2_NO_ZEROCONF
// These functions are shared by Avahi and Bonjour implementations

// check for len-char hex string
static bool check_hex(const char *addr, int len)
{
    for (int i = 0; i < len; i++) {
        char c = addr[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}


bool o2l_is_valid_proc_name(const char *name, int port,
                            char *internal_ip, int *udp_port)
{
    if (!name) return false;
    if (strlen(name) != 28) return false;
    if (name[0] != '@') return false;
    // must have 8 lower case hex chars starting at name[1] followed by ':'
    if (!check_hex(name + 1, 8)) return false;
    if (name[9] != ':') return false;
    if (!check_hex(name + 10, 8)) return false;
    if (name[18] != ':') return false;
    // internal IP is copied to internal_ip
    memcpy(internal_ip, name + 10, 8);
    internal_ip[8] = 0;
   // must have 4-digit hex tcp port number matching port
    if (!check_hex(name + 19, 4)) return false;
     int top = o2_hex_to_byte(name + 19);
    int bot = o2_hex_to_byte(name + 21);
    int tcp_port = (top << 8) + bot;
    if (tcp_port != port) return false;  // name must be consistent
    if (name[23] != ':') return false;
    // must find 4-digit hex udp port number
    if (!check_hex(name + 24, 4)) return false;
    top = o2_hex_to_byte(name + 24);
    bot = o2_hex_to_byte(name + 26);
    *udp_port = (top << 8) + bot;
    return true;
}


// parses a version string of the form "123.45.067". Returns an
// integer encoding, e.g. "2.3.4" becomes 0x00020304. If there is
// any syntax error, zero is returned.
int o2l_parse_version(const char *vers, int vers_len)
{
    int version = 0;
    int version_shift = 16;
    int field = 0;
    while (vers_len > 0) {
        if (isdigit(*vers)) {
            field = (field * 10) + *vers - '0';
            if (field > 255) return 0;
        } else if (*vers == '.') {
            version += (field << version_shift);
            field = 0;
            version_shift -= 8;
            if (version_shift < 0) return 0;
        }
        vers++;
        vers_len--;
    }
    version += (field << version_shift);
    return version;
}
#endif


void o2l_poll()
{
    O2LDBV printf("o2l_poll\n");
    o2l_local_now = o2l_local_time();

#ifndef O2L_NO_CLOCKSYNC
    // send clock pings
    if (time_for_clock_ping < o2l_local_now) {
        clock_ping();
    }
#endif

#ifndef O2L_NO_BROADCAST
    // send discovery if not connected to O2
    if (tcp_sock == INVALID_SOCKET && 
        time_for_discovery_send < o2l_local_now) {
        discovery_send();
    }
#endif

    network_poll();
}


int o2l_initialize(const char *ensemble)
{
#ifdef WIN32
    // Initialize (in Windows)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif // WIN32
#ifndef O2L_NO_CLOCKSYNC
    o2l_clock_initialize();
#endif
    o2l_method_new("!_o2/id", "i", true, &o2l_id_handler, NULL);

    // create UDP send socket
    if ((udp_send_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("allocating udp send socket");
        return O2L_FAIL;
    }

    udp_recv_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_recv_sock == INVALID_SOCKET) {
        O2LDB printf("o2lite: udp socket creation error\n");
        return O2L_FAIL;
    }
    if (o2l_bind_recv_socket(udp_recv_sock, &udp_recv_port) != 0) {
        O2LDB printf("o2lite: could not allocate udp_recv_port\n");
        return O2L_FAIL;
    } else {
        O2LDB printf("o2lite: UDP server port %d\n", udp_recv_port);
    }

    find_my_ip_address();
    return o2ldisc_init(ensemble);
}


// We assume o2lite applications are minimal and have no need to
// shut down cleanly, close an o2lite connection, or free resources.
// This implementation of o2l_finish() is not complete or tested.
int o2l_finish()
{
#ifdef WIN32
    WSACleanup();
#endif
    return O2L_SUCCESS;
}
