// o2lite.c -- a simple o2lite client library
//
// Roger B. Dannenberg
// Jul-Aug 2020, Jun 2021 updated for Avahi
//
// This illustrates a bare-bones o2lite client implementation.
// It call system network functions directly rather than network.c
// used in O2 to simplify things, but all network calls are
// synchronous, so this could introduce more latency than necessary.
//
// For ESP32, this must be named as a .cpp file because of the includes.
//
// Note that o2lite clients do not have a full directory of services and
// their status. To retrieve a service status, send the service name to
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
// Discovery uses Bonjour (or Avahi for Linux).

#include "o2lite.h"
#include <string.h>
#include <ctype.h>
// IMPORTANT: hostip.c normally uses O2_MALLOC as defined in o2base.h,
// but here, we want to replace o2_dbg_malloc and o2_malloc with simple
// malloc using macros set up in o2lite.h (above) to override this 
// default. If you compile hostip.c independently, it will expect to 
// link with o2_dbg_malloc.
#include "hostip.c"

#ifndef O2_NO_ZEROCONF

#include <dns_sd.h>

#define BROWSE_TIMEOUT 20  // restart ServiceBrowse if no activity

#ifndef O2_NO_O2DISCOVERY
// O2 ensembles should adopt one of two discovery methods. If ZeroConf
// works out, the built-in O2 discovery mechanism will be removed entirely.
// Whatever method is used by O2, this O2lite library must do the same.
#error O2lite supporte either ZeroConf or built-in discovery, but not both.
#error One of O2_NO_ZEROCONF or O2_NO_O2DISCOVERY must be defined
#endif

#ifndef O2L_NO_BROADCAST
// if O2_NO_O2DISCOVERY, then we should disable sending O2 discovery messages
#define O2L_NO_BROADCAST
#endif

#endif

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
// PTR is a machine address to which you can add byte offsets
#define PTR(addr) ((char *) (addr))
// get address of first 32-bit word boundary at or above ptr:
#define ROUNDUP(ptr) ((char *)((((size_t) ptr) + 3) & ~3))

void o2l_dispatch(o2l_msg_ptr msg);
static void find_my_ip_address();

static const char *o2l_services = NULL;
static const char *o2l_ensemble = NULL;

#ifdef WIN32
/****************************WINDOWS***********************************/
#include <winsock2.h> // define SOCKET, INVALID_SOCKET
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
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>

typedef int SOCKET;  // In O2, we'll use SOCKET to denote the type of a socket
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define TERMINATING_SOCKET_ERROR (errno != EAGAIN && errno != EINTR)
#define closesocket close

/**** a few things are not common to both APPLE and LINUX *******/
#ifdef __APPLE__
#include "CoreAudio/HostTime.h"
static uint64_t start_time;
#elif __linux__
static long start_time;
#endif

/*********************************ESP32********************************/
#elif ESP32
#include "Printable.h"
#include "WiFi.h"
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include "esp32-hal.h"

typedef int SOCKET;  // In O2, we'll use SOCKET to denote the type of a socket
#define TERMINATING_SOCKET_ERROR (errno != EAGAIN && errno != EINTR)
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1

const int LED_PIN = 5;

/* Note: we assume caller sets up serial interface and Wifi:
const char *network_name = "your network name here";
const char *network_pswd = "your network password here";

void setup() {
    Serial.begin(115200);
    connect_to_wifi(hostname, network_name, network_pswd);
    ...
*/

void print_line()
{
    printf("\n");
    for (int i = 0; i < 30; i++)
        printf("-");
    printf("\n");
}

void connect_to_wifi(const char *hostname, const char *ssid, const char *pwd)
{
    int ledState = 0;

    print_line();
    printf("Connecting to WiFi network: %s\n", ssid);
    WiFi.begin(ssid, pwd);
    WiFi.setHostname(hostname);
  
    while (WiFi.status() != WL_CONNECTED) {
        // Blink LED while we're connecting:
        digitalWrite(LED_PIN, ledState);
        ledState = (ledState + 1) % 2; // Flip ledState
        delay(500);
        printf(".");
    }
    uint32_t ip = WiFi.localIP();
    snprintf(o2n_internal_ip, O2N_IP_LEN, "%08x", ip);
    char dot_ip[O2N_IP_LEN];
    o2_hex_to_dot(o2n_internal_ip, dot_ip);
    printf("\n");
    printf("WiFi connected!\nIP address: %s (%s)\n", o2n_internal_ip, dot_ip);
}

#else
#error no environment has been defined; 
#error expecting WIN32, __APPLE__, __linux__, or ESP32
#endif

#ifndef O2L_NO_BROADCAST
SOCKET broadcast_sock = INVALID_SOCKET;
struct sockaddr_in broadcast_to_addr;
#endif

int address_init(struct sockaddr_in *sa, const char *ip,
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


#define CHECKERROR(typ) \
    if (parse_cnt + sizeof(typ) > max_parse_cnt) { \
        O2LDB printf("o2lite: parse error reading message to %s\n", \
                     parse_msg->address); \
        parse_error = true; return 0; }

#define CURDATAADDR(typ) ((typ) ((char *) parse_msg + parse_cnt))

#define CURDATA(var, typ) CHECKERROR(typ) \
    typ var = *CURDATAADDR(typ *); parse_cnt += sizeof(typ);


double o2l_get_time()
{
    CURDATA(t, int64_t);
    t = o2lswap64(t);
    return *(double *)&t;
}

float o2l_get_float()
{
    CURDATA(x, int32_t);
    x = o2lswap32(x);
    return *(float *)&x;
}

int32_t o2l_get_int32()
{
    CURDATA(i, int32_t);
    return o2lswap32(i);
}

char *o2l_get_string()
{
    CHECKERROR(char *)
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

#if O2LDEBUG
char o2l_remote_ip_port[16];
#endif

#ifndef O2_NO_O2DISCOVERY

unsigned short o2_port_map[PORT_MAX] = {
                                64541, 60238, 57143, 55764, 56975, 62711,
                                57571, 53472, 51779, 63714, 53304, 61696,
                                50665, 49404, 64828, 54859 };
#else // assume ZeroConf
#ifndef __linux__
DNSServiceRef browse_ref = NULL;
SOCKET browse_sock = INVALID_SOCKET;
DNSServiceRef resolve_ref = NULL;
SOCKET resolve_sock = INVALID_SOCKET;
o2l_time resolve_timeout = 0;
o2l_time browse_timeout = BROWSE_TIMEOUT;
#else


#endif
#endif


static int bind_recv_socket(SOCKET sock, int *port)
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
    if (bind(sock, (struct sockaddr *) &server_addr,
             sizeof server_addr)) {
        return O2L_FAIL;
    }
    if (*port == 0) { // find the port that was (possibly) allocated
        socklen_t addr_len = sizeof server_addr;
        if (getsockname(sock, (struct sockaddr *) &server_addr,
                        &addr_len)) {
            perror("getsockname call to get port number");
            return O2L_FAIL;
        }
        *port = o2lswap16(server_addr.sin_port);  // set actual port used
    }
    O2LDB printf("o2lite: bind port %d as UDP server port\n", *port);
    return O2L_SUCCESS;
}


int o2l_network_initialize()
{
#ifdef WIN32
    // Initialize (in Windows)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#define inet_pton InetPton
#endif // WIN32

#ifndef O2L_NO_BROADCAST
    // Initialize addr for broadcasting
    broadcast_to_addr.sin_family = AF_INET;
    inet_pton(AF_INET, "255.255.255.255",
              &broadcast_to_addr.sin_addr.s_addr);

    // create UDP broadcast socket
    if ((broadcast_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("allocating udp broadcast socket");
        return O2L_FAIL;
    }
    // Set the socket's option to broadcast
    int optval = true; // type is correct: int, not bool
    if (setsockopt(broadcast_sock, SOL_SOCKET, SO_BROADCAST,
                   (const char *) &optval, sizeof optval) == -1) {
        perror("Set socket to broadcast");
        return O2L_FAIL;
    }
#endif

    // create UDP send socket
    if ((udp_send_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("allocating udp send socket");
        return O2L_FAIL;
    }
    // create UDP server socket
    udp_recv_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_recv_sock == INVALID_SOCKET) {
        printf("udp socket creation error");
        return O2L_FAIL;
    }
#ifndef O2_NO_O2DISCOVERY
    for (int i = 0; i < PORT_MAX; i++) {
        udp_recv_port = o2_port_map[i];
        if (bind_recv_socket(udp_recv_sock, &udp_recv_port) == 0) {
            goto done;
        }
    }
#else
    udp_recv_port = 0;
    if (bind_recv_socket(udp_recv_sock, &udp_recv_port) == 0) {
        goto done;
    }
#endif
    O2LDB printf("o2lite: could not allocate a udp recv port\n");
    return O2L_FAIL;
  done:
    O2LDB printf("o2lite: allocated udp recv port %d\n", udp_recv_port);
    find_my_ip_address();
    return O2L_SUCCESS;
}


// initializes tcp_server_sa with server address and port
//
int address_init(struct sockaddr_in *sa, const char *ip, int port_num, bool tcp)
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


// connect TCP port to O2 IP:port server address
void network_connect(const char *ip, int port)
{
    address_init(&tcp_server_sa, ip, port, true); // sets tcp_server_sa
    O2LDB printf("connecting to %s port %d\n", ip, port);
    tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(tcp_sock, (struct sockaddr *) &tcp_server_sa,
                sizeof tcp_server_sa) == -1) {
        perror("o2lite network_connect");
        return;
    }
#ifdef __APPLE__
    int set = 1;
    setsockopt(tcp_sock, SOL_SOCKET, SO_NOSIGPIPE, (void *) &set, sizeof set);
#endif
    O2LDB snprintf(o2l_remote_ip_port, 16, "%s:%04x", ip, port);
    O2LDB printf("o2lite: connected to O2 %s\n", o2l_remote_ip_port);
    // send back !_o2/o2lite/con ipaddress updport
    o2l_send_start("!_o2/o2lite/con", 0, "si", true);
    O2LDB printf("o2lite sends !_o2/o2lite/con %s %x\n", 
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
    } else if (n == 0) {
        closesocket(tcp_sock);
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

static void add_socket(SOCKET s)
{
    if (s != INVALID_SOCKET) {
        FD_SET(s, &read_set);
        // Windows socket is not an int, but Windows does not care about
        // the value of nfds, so it's OK even if this cast loses data
        if (s >= nfds) nfds = (int) (s + 1);
    }
}

#ifndef O2_NO_ZEROCONF
#ifndef __linux__
static void zc_handle_event(SOCKET *sock, DNSServiceRef *sd_ref,
                            const char *msg)
{
    DNSServiceErrorType err = DNSServiceProcessResult(*sd_ref);
    if (err) {
        printf("Error %d from DNSServiceProcessResult for %s\n", err, msg);
        DNSServiceRefDeallocate(*sd_ref);
        *sd_ref = NULL;
        *sock = INVALID_SOCKET;
    }
}
#else
#endif
#endif


void network_poll()
{
    nfds = 0;
    FD_ZERO(&read_set);
    add_socket(udp_recv_sock);
    add_socket(tcp_sock);
#ifndef O2_NO_ZEROCONF
#ifndef __linux__
    add_socket(browse_sock);
    add_socket(resolve_sock);
#else

#endif
#endif
    int total;
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
#ifndef O2_NO_ZEROCONF
#ifndef __linux__
    if (browse_sock != INVALID_SOCKET) {
        if (FD_ISSET(browse_sock, &read_set)) {
            zc_handle_event(&browse_sock, &browse_ref, "ServiceBrowse");
        }
    }
    if (resolve_sock != INVALID_SOCKET) {
        if (FD_ISSET(resolve_sock, &read_set)) {
            zc_handle_event(&resolve_sock, &resolve_ref, "ServiceResolve");
        }
    }
#else

#endif
#endif
}


void o2l_send()
{
    if (parse_error || tcp_sock == INVALID_SOCKET) {
        return;
    }
    // grap the tcp flag before byte-swapping
    out_msg->length = o2lswap32(out_msg_cnt - sizeof out_msg->length);
    if (out_msg->misc & o2lswap32(O2_TCP_FLAG)) {
        send(tcp_sock, outbuf, out_msg_cnt, 0);
    } else {
        if (sendto(udp_send_sock, outbuf + sizeof out_msg->length,
                   out_msg_cnt - sizeof out_msg->length, 0,
                   (struct sockaddr *) &udp_server_sa,
                   sizeof udp_server_sa) < 0) {
            perror("Error attempting to send udp message");
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
        parse_cnt = (int) (data - (char *) msg);
        parse_error = false;
        max_parse_cnt = sizeof msg->length + msg->length;
        (*m->handler)(msg, typespec + 1, (void *) data, m->info);
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
    //             "expecting clock_sync_id %d\n", id, clock_sync_id);
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
            } else if (global_minus_local < upper) {
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


static void o2l_clock_initialize()
{
    if (clock_initialized) {
        o2l_clock_finish();
    }
    o2l_method_new("!_o2/cs/put", "it", true,
                   &ping_reply_handler, NULL);
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
    o2n_get_internal_ip();
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
    o2l_get_string();  // assume host is local; ignore public
    const char *iip = o2l_get_string();  // here is the internal (local) IP
    int tcp_port = o2l_get_int32();
    int udp_port = o2l_get_int32();
    if (parse_error || !streql(ens, o2l_ensemble)) {
        return; // error parsing message
    }
    char iip_dot[16];
    o2l_hex_to_dot(iip, iip_dot);
    address_init(&udp_server_sa, iip_dot, udp_port, false);
    network_connect(iip_dot, tcp_port);
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
#ifndef __linux__
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

typedef struct pending_service_struct {
    char *name;
    struct pending_service_struct *next;
} pending_service_type;

pending_service_type *pending_services = NULL;
pending_service_type *active_service = NULL;
#define LIST_PUSH(list, node) (node)->next = (list); (list) = (node);
#define LIST_POP(list, node) (node) = (list); (list) = (list)->next;

static bool is_valid_proc_name(char *name, int port,
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
    // pad O2 name with zeros to a word boundary (only one zero needed)
    name[23] = 0;
    return true;
}


static void stop_resolving()
{
    // clean up previous resolve attempt:
    if (resolve_ref) {
        DNSServiceRefDeallocate(resolve_ref);
        resolve_sock = INVALID_SOCKET;
        resolve_ref = NULL;
    }
    if (active_service) {
        O2_FREE(active_service->name);
        O2_FREE(active_service);
    }
}


static void zc_resolve_callback(DNSServiceRef sd_ref, DNSServiceFlags flags,
                         uint32_t interface_index, DNSServiceErrorType err,
                         const char *fullname, const char *hosttarget,
                         uint16_t port, uint16_t txt_len,
                         const unsigned char *txt_record, void *context)
{
    int udp_send_port;
    port = ntohs(port);
    uint8_t proc_name_len;
    const char *proc_name = (const char *) TXTRecordGetValuePtr(txt_len,
                                    txt_record, "name", &proc_name_len);
    if (proc_name_len == 28) {  // names are fixed length -- reject if invalid
        char name[32];
        memcpy(name, proc_name, 28);
        name[28] = 0;

        char internal_ip[O2N_IP_LEN];
        if (is_valid_proc_name(name, port, internal_ip, &udp_send_port)) {
            char iip_dot[16];
            o2l_hex_to_dot(internal_ip, iip_dot);
            address_init(&udp_server_sa, iip_dot, udp_send_port, false);
            network_connect(iip_dot, port);
            if (tcp_sock) {  // we are connected; stop browsing ZeroConf
                if (browse_ref) {
                    DNSServiceRefDeallocate(browse_ref);
                }
                browse_ref = NULL;
                browse_sock = INVALID_SOCKET;
                while (pending_services) {
                    LIST_POP(pending_services, active_service);
                    stop_resolving();  // existing code to free active_service
                }
            }
        }
    }
    stop_resolving();
    resolve_timeout = o2l_local_now;  // so start_resolving() will be called
}


static void start_resolving()
{
    DNSServiceErrorType err;
    stop_resolving();
    LIST_POP(pending_services, active_service);
    
    err = DNSServiceResolve(&resolve_ref, 0, kDNSServiceInterfaceIndexAny,
                       active_service->name, "_o2proc._tcp.", "local",
                       zc_resolve_callback, (void *) active_service->name);
    browse_timeout = o2l_local_now + BROWSE_TIMEOUT;
    if (err) {
        fprintf(stderr, "DNSServiceResolve returned %d\n", err);
        DNSServiceRefDeallocate(resolve_ref);
        resolve_ref = NULL;
    } else {
        resolve_sock = DNSServiceRefSockFD(resolve_ref);
        resolve_timeout = o2l_local_now + 1; //  try for 1s
    }
}

static void zc_browse_callback(DNSServiceRef sd_ref, DNSServiceFlags flags,
                uint32_t interfaceIndex, DNSServiceErrorType err,
                const char *name, const char *regtype,
                const char *domain, void *context)
{
    // match if ensemble name is a prefix of name, e.g. "ensname (2)"
    if ((flags & kDNSServiceFlagsAdd) &&
        (strncmp(o2l_ensemble, name, strlen(o2l_ensemble)) == 0)) {
        pending_service_type *ps = O2_MALLOCT(pending_service_type);
        ps->name = O2_MALLOCNT(strlen(name) + 1, char);
        strcpy(ps->name, name);
        LIST_PUSH(pending_services, ps);
    }
}
#else
/*********** Linux Avahi Implementation *************/

#endif
#endif


static void o2l_discovery_initialize(const char *ensemble)
{
    o2l_ensemble = ensemble;
#ifndef O2_NO_O2DISCOVERY
    time_for_discovery_send = o2l_local_time();
    o2l_method_new("!_o2/dy", "sssiii", true, &o2l_dy_handler, NULL);
#else
#ifndef __linux__
    // set up ZeroConf discovery -- our goal is to find any O2 host
    // in the ensemble, so service type is "_o2proc._tcp". Then, we
    // have to resolve a service to get the proc name, IP, and ports.
    // We'll start DNSServiceBrowse and make a list of incoming services.
    // We'll start DNSServiceResolve one service at a time until we find
    // a host. Allow 1s for each service to return a resolution. Host
    // connection will just time out on its own. Unlike the more elaborate
    // scheme in the O2 implementation, if a service times out, we just
    // go on to the next one without any retries later.
    DNSServiceErrorType err = DNSServiceBrowse(&browse_ref, 0,
                 kDNSServiceInterfaceIndexAny,
                 "_o2proc._tcp.", NULL, zc_browse_callback, NULL);
    if (err) {
        fprintf(stderr, "DNSServiceBrowse returned %d\n", err);
        DNSServiceRefDeallocate(browse_ref);
        browse_ref = NULL;
    } else {
        browse_sock = DNSServiceRefSockFD(browse_ref);
    }
#else
    
#endif
#endif
}

void o2l_poll()
{
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

#ifndef O2_NO_ZEROCONF
#ifndef __linux__
    // start resolving if timeout
    if (tcp_sock == INVALID_SOCKET) {
        if (pending_services && o2l_local_now > resolve_timeout) {
            start_resolving();
        // in principle, if we just leave the browser open, we'll see
        // anything new that appears. But we have nothing else to do.
        // And a full restart seems more robust when all else fails.
        // So if there's nothing to resolve, and no activity for 20s,
        // restart the ServiceBrowse operation.
        } else if (!pending_services && o2l_local_now > browse_timeout) {
            O2LDB printf("No activity, restarting ServiceBrowse\n");
            stop_resolving();
            if (browse_ref) {
                DNSServiceRefDeallocate(browse_ref);
                browse_ref = NULL;
                browse_sock = INVALID_SOCKET;
            }
            browse_timeout = o2l_local_now + BROWSE_TIMEOUT;  // try every 20s
            o2l_discovery_initialize(o2l_ensemble);
        }
    }
#else

#endif
#endif

    network_poll();
}


void o2l_initialize(const char *ensemble)
{
    o2l_method_new("!_o2/id", "i", true, &o2l_id_handler, NULL);
    o2l_network_initialize();
    o2l_discovery_initialize(ensemble);
#ifndef O2L_NO_CLOCKSYNC
    o2l_clock_initialize();
#endif
}
