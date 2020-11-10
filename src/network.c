// network.c -- implementation of network communication
//
// Roger B. Dannenberg, 2020
//

// to define addrinfo, need to set this macro:

#include "o2usleep.h"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ctype.h>
#include <netdb.h>
#include <sys/time.h>
#include <unistd.h>
#include "o2internal.h"
#include <errno.h>

#ifdef WIN32
#include <stdio.h> 
#include <stdlib.h> 
#include <windows.h>

// test after recvfrom() < 0 to see if the socket should close
#define TERMINATING_SOCKET_ERROR \
    (WSAGetLastError() != WSAEWOULDBLOCK && WSAGetLastError() != WSAEINTR)

typedef struct ifaddrs
{
    struct ifaddrs  *ifa_next;    /* Next item in list */
    char            *ifa_name;    /* Name of interface */
    unsigned int     ifa_flags;   /* Flags from SIOCGIFFLAGS */
    struct sockaddr *ifa_addr;    /* Address of interface */
    struct sockaddr *ifa_netmask; /* Netmask of interface */
    union {
        struct sockaddr *ifu_broadaddr; /* Broadcast address of interface */
        struct sockaddr *ifu_dstaddr; /* Point-to-point destination address */
    } ifa_ifu;
#define              ifa_broadaddr ifa_ifu.ifu_broadaddr
#define              ifa_dstaddr   ifa_ifu.ifu_dstaddr
    void            *ifa_data;    /* Address-specific data */
} ifaddrs;

#else
#include <unistd.h>    // define close()
#include "sys/ioctl.h"
#include <ifaddrs.h>
#include <sys/poll.h>
#include <netinet/tcp.h>

#define TERMINATING_SOCKET_ERROR (errno != EAGAIN && errno != EINTR)
#endif

static dyn_array o2n_fds;      ///< pre-constructed fds parameter for poll()
static dyn_array o2n_fds_info; ///< info about sockets

#define GET_O2N_FDS(i) DA_GET_ADDR(o2n_fds, struct pollfd, i)
#define GET_O2N_INFO(i) DA_GET(o2n_fds_info, o2n_info_ptr, i)

char o2n_local_ip[24];
// we have not been able to connect to network
// and (so far) we only talk to 127.0.0.1 (localhost)
bool o2n_found_network = false;
o2n_info_ptr o2n_message_source; 


static struct sockaddr_in o2_serv_addr;

//static int tcp_recv_handler(SOCKET sock, o2n_info_ptr info);
//static int udp_recv_handler(SOCKET sock, o2n_info_ptr info);
//static void tcp_message_cleanup(o2n_info_ptr info);
static int read_event_handler(SOCKET sock, o2n_info_ptr info);

// a socket for sending broadcast messages:
SOCKET o2n_broadcast_sock = INVALID_SOCKET;
// address for sending broadcast messages:
struct sockaddr_in o2n_broadcast_to_addr;

// a socket for general UDP message sends:
SOCKET o2n_udp_send_sock = INVALID_SOCKET;
// address for sending discovery UDP messages to local host:
static struct sockaddr_in local_to_addr;

static o2n_recv_callout_type o2n_recv_callout;
static o2n_accept_callout_type o2n_accept_callout;
static o2n_connected_callout_type o2n_connected_callout;
static o2n_close_callout_type o2n_close_callout;

static bool o2n_socket_delete_flag = false;

// macOS does not always free ports, so to aid in debugging orphaned ports,
// define CLOSE_SOCKET_DEBUG 1 and get a list of sockets that are opened
// and closed
//
#define CLOSE_SOCKET_DEBUG 0
#if CLOSE_SOCKET_DEBUG
SOCKET o2_socket(int domain, int type, int protocol, const char *who)
{
    SOCKET sock = socket(domain, type, protocol);
    if (sock >= 0) {
        long s = (long) sock; // whatever the type is, get it and print it
        printf("**** opened socket %ld for %s\n", s, who);
    }
    return sock;
}

SOCKET o2_accept(SOCKET socket, struct sockaddr *restrict address,
                 socklen_t *restrict address_len, const char *who)
{
    SOCKET sock = accept(socket, address, address_len);
    if (sock >= 0) {
        long s = (long) sock; // whatever the type is, get it and print it
        printf("**** accepted socket %ld for %s\n", s, who);
    }
    return sock;
}

void o2_closesocket(SOCKET sock, const char *who)
{
    long s = (long) sock; // whatever the type is, get it and print it
    printf("**** closing socket %ld for %s\n", s, who);
    int err = closesocket(sock);
    if (err < 0) {
        perror("o2_closesocket");
    }
}
#else
#define o2_socket(dom, type, prot, who) socket(dom, type, prot)
#define o2_accept(sock, addr, len, who) accept(sock, addr, len)
#define o2_closesocket(sock, who) closesocket(sock)
#endif


o2n_info_ptr o2n_get_info(int i)
{
    if (i >= 0 && i < o2n_fds_info.length) {
        return GET_O2N_INFO(i);
    }
    return NULL;
}


o2_err_t o2n_address_init(o2n_address_ptr remote_addr_ptr, const char *ip,
                          int port_num, bool tcp_flag)
{
    o2_err_t rslt = O2_SUCCESS;
    char port[24]; // can't overrun even with 64-bit int
    sprintf(port, "%d", port_num);
    if (streql(ip, "")) ip = "localhost";
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    if (tcp_flag) {
        hints.ai_family = AF_INET; // should this be AF_UNSPEC?
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
    } else {
        hints.ai_family = PF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
    }
    struct addrinfo *aiptr = NULL;
    if (getaddrinfo(ip, port, &hints, &aiptr)) {
        rslt = O2_HOSTNAME_TO_NETADDR_FAIL;
    } else {
        memcpy(&remote_addr_ptr->sa, aiptr->ai_addr,
               sizeof remote_addr_ptr->sa);
        if (remote_addr_ptr->sa.sin_port == 0) {
            remote_addr_ptr->sa.sin_port = htons((short) port_num);
        }
    }
    if (aiptr) freeaddrinfo(aiptr);
    return rslt;
}


int o2n_address_get_port(o2n_address_ptr address)
{
    return ntohs(address->sa.sin_port);
}


void o2n_address_set_port(o2n_address_ptr address, int port)
{
    address->sa.sin_port = htons(port);
}


o2_err_t o2n_send_udp_via_socket(SOCKET socket, o2n_address_ptr ua, 
                                 o2n_message_ptr msg)
{
    ssize_t err = sendto(socket,
                         ((char *) &msg->length) + sizeof msg->length,
                         msg->length, 0, (struct sockaddr *) &ua->sa,
                         sizeof ua->sa);
    O2_FREE(msg);
    if (err < 0) {
        printf("error sending udp to port %d ", ntohs(ua->sa.sin_port));
        perror("o2n_send_udp_via_socket");
        return O2_FAIL;
    }
    return O2_SUCCESS;
}


o2_err_t o2n_send_udp_via_info(o2n_info_ptr info, o2n_address_ptr ua,
                               o2n_message_ptr msg)
{
    return o2n_send_udp_via_socket(GET_O2N_FDS(info->fds_index)->fd, ua, msg);
}


// send a udp message to an address
o2_err_t o2n_send_udp(o2n_address_ptr ua, o2n_message_ptr msg)
{
    return o2n_send_udp_via_socket(o2n_udp_send_sock, ua, msg);
}


// send udp message to local port. msg is owned/freed by this function.
// msg must be in network byte order
//
void o2n_send_udp_local(int port, o2n_message_ptr msg)
{
    local_to_addr.sin_port = port; // copy port number
    O2_DBd(printf("%s sending localhost msg to port %d\n",
                  o2_debug_prefix, ntohs(port)));
    if (sendto(o2n_udp_send_sock,
               ((char *) &msg->length) + sizeof msg->length,
               msg->length, 0, (struct sockaddr *) &local_to_addr,
               sizeof local_to_addr) < 0) {
        perror("Error attempting to send udp message locally");
    }
    O2_FREE(msg);
}


o2_err_t o2n_send_tcp(o2n_info_ptr info, int block, o2n_message_ptr msg)
{
    // if proc has a pending message, we must send with blocking
    if (info->out_message && block) {
        o2_err_t rslt = o2n_send(info, true);
        if (rslt != O2_SUCCESS) { // process is dead and removed
            O2_FREE(msg); // we drop the message
            return rslt;
        }
    }
    // now send the new msg
    o2n_enqueue(info, msg);
    return O2_SUCCESS;
}


// Important: msg is owned by caller, msg is in network order except for length
ssize_t o2n_send_broadcast(int port, o2n_message_ptr msg)
{
    o2n_broadcast_to_addr.sin_port = htons(port);
    ssize_t err = sendto(o2n_broadcast_sock,
                         ((char *) &msg->length) + sizeof msg->length,
                         msg->length, 0,
                         (struct sockaddr *) &o2n_broadcast_to_addr,
                         sizeof o2n_broadcast_to_addr);
    if (err < 0) {
        perror("Error attempting to broadcast discovery message");
    }
    return err;
}


// create a UDP send socket for broadcast or general sends
//
o2_err_t o2n_udp_send_socket_new(SOCKET *sock)
{
    if ((*sock = o2_socket(AF_INET, SOCK_DGRAM, 0,
                           "o2n_udp_send_socket_new")) < 0) {
        perror("allocating udp send socket");
        return O2_FAIL;
    }
    O2_DBo(printf("%s allocating udp send socket %ld\n",
                  o2_debug_prefix, (long) *sock));
    return O2_SUCCESS;
}


// On OS X, need to disable SIGPIPE when socket is created
void o2_disable_sigpipe(SOCKET sock)
{
#ifdef __APPLE__
    int set = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE,
                   (void *) &set, sizeof set) < 0) {
        perror("in setsockopt in o2_disable_sigpipe");
    }
#endif    
}


static int bind_recv_socket(SOCKET sock, int *port, int tcp_recv_flag,
                            bool reuse)
{
    memset(PTR(&o2_serv_addr), 0, sizeof o2_serv_addr);
    o2_serv_addr.sin_family = AF_INET;
    o2_serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // local IP address
    o2_serv_addr.sin_port = htons(*port);
    unsigned int yes = 1;
    if (reuse) {
        // this code will allow two processes to open the same port on linux;
        // then, if they try to communicate, they'll send to themselves. So,
        // for discovery ports and server ports, set reuse to false.
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                           PTR(&yes), sizeof yes) < 0) {
            perror("setsockopt(SO_REUSEADDR)");
            return O2_FAIL;
        }
    }
    if (bind(sock, (struct sockaddr *) &o2_serv_addr, sizeof o2_serv_addr)) {
        if (tcp_recv_flag) perror("Bind receive socket");
        return O2_FAIL;
    }
    if (*port == 0) { // find the port that was (possibly) allocated
        socklen_t addr_len = sizeof o2_serv_addr;
        if (getsockname(sock, (struct sockaddr *) &o2_serv_addr,
                        &addr_len)) {
            perror("getsockname call to get port number");
            return O2_FAIL;
        }
        *port = ntohs(o2_serv_addr.sin_port);  // set actual port used
    }
    O2_DBo(printf("*   %s bind socket %d port %d\n", o2_debug_prefix,
                  sock,   *port));
    assert(*port != 0);
    return O2_SUCCESS;
}


// add a new socket to the fds and fds_info arrays,
// on success, o2n_info_ptr descriptor is initialized
//
static o2n_info_ptr socket_info_new(SOCKET sock, int net_tag)
{
    // expand socket arrays for new port
    o2n_info_ptr info = O2_CALLOCT(o2n_info); // create info struct
    info->net_tag = net_tag;
    info->fds_index = o2n_fds.length; // this will be the last element
    assert(info->fds_index >= 0);
    DA_APPEND(o2n_fds_info, o2n_info_ptr, info);
    struct pollfd *pfd = DA_EXPAND(o2n_fds, struct pollfd);
    pfd->fd = sock;
    pfd->events = POLLIN;
    pfd->revents = 0;
#if CLOSE_SOCKET_DEBUG
    printf("**socket_info_new:\n");
    for (int i = 0; i < o2n_fds.length; i++) {
        pfd = GET_O2N_FDS(i);
        long s = (long) pfd->fd;
        printf("    %d: %ld\n", i, s);
    }
#endif
    return info;
}


static void set_nodelay_option(SOCKET sock)
{
    int option = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *) &option,
               sizeof option);
}


static o2n_info_ptr socket_cleanup(const char *error, o2n_info_ptr info,
                                   SOCKET sock)
{
    perror("bind and listen");
    o2_closesocket(sock, "socket_cleanup");
    o2n_fds_info.length--;   // restore socket arrays
    o2n_fds.length--;
    O2_FREE(info);
    return NULL; // so caller can return socket_cleanup(...);
}


o2n_info_ptr o2n_tcp_server_new(int port, void *application)
{
    o2n_info_ptr info = o2n_tcp_socket_new(NET_TCP_SERVER, port, application);
    if (!info) {
        return NULL;
    }
    int sock = DA_LAST(o2n_fds, struct pollfd).fd;
    // bind server port
    if (bind_recv_socket(sock, &info->port, true, true) != O2_SUCCESS ||
        listen(sock, 10) != 0) {
        return socket_cleanup("bind and listen", info, sock);
    }
    O2_DBo(printf("%s bind and listen called on socket %ld\n",
                  o2_debug_prefix, (long) sock));
    return info;
}


// creates a server listening to port, or can also be a client
// where you send messages to socket and expect a UDP reply to port
// 
o2n_info_ptr o2n_udp_server_new(int *port, bool reuse, void *application)
{
    SOCKET sock = o2_socket(AF_INET, SOCK_DGRAM, 0, "o2n_udp_server_new");
    if (sock == INVALID_SOCKET) {
        return NULL;
    }
    // Bind the socket
    int err;
    if ((err = bind_recv_socket(sock, port, false, reuse))) {
        o2_closesocket(sock, "bind failed in o2n_udp_server_new");
        return NULL;
    }
    o2n_info_ptr info = socket_info_new(sock, NET_UDP_SERVER);
    assert(info);
    info->application = application;
    info->port = *port;
    return info;
}


o2_err_t o2n_broadcast_socket_new(SOCKET *sock)
{
    // Set up a socket for broadcasting discovery info
    RETURN_IF_ERROR(o2n_udp_send_socket_new(sock));
    // Set the socket's option to broadcast
    int optval = true; // type is correct: int, not bool
    if (setsockopt(*sock, SOL_SOCKET, SO_BROADCAST,
                   (const char *) &optval, sizeof optval) == -1) {
        perror("Set socket to broadcast");
        return O2_FAIL;
    }
    return O2_SUCCESS;
}


const char *o2n_get_local_process_name(int port)
{
    struct ifaddrs *ifap, *ifa;
    static char name[O2_MAX_PROCNAME_LEN];
    name[0] = 0;   // initially empty
    o2n_local_ip[0] = 0;
    struct sockaddr_in *sa;
    // look for AF_INET interface. If you find one, copy it
    // to name. If you find one that is not 127.0.0.1, then
    // stop looking.
        
    if (getifaddrs(&ifap)) {
        perror("getting IP address");
        return name;
    }
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr->sa_family == AF_INET) {
            sa = (struct sockaddr_in *) ifa->ifa_addr;
            if (!inet_ntop(AF_INET, &sa->sin_addr, o2n_local_ip,
                           sizeof o2n_local_ip)) {
                perror("converting local ip to string");
                break;
            }
            snprintf(name, O2_MAX_PROCNAME_LEN,
                     "%s:%d", o2n_local_ip, port);
            if (!streql(o2n_local_ip, "127.0.0.1")) {
                o2n_found_network = true;
                break;
            }
        }
    }
    freeifaddrs(ifap);
    return name;
}


// initialize this module
// - create UDP broadcast socket
// - create UDP send socket
o2_err_t o2n_initialize(o2n_recv_callout_type recv,
                        o2n_accept_callout_type acc,
                        o2n_connected_callout_type conn,
                        o2n_close_callout_type clos)
{
    o2_err_t err;
#ifdef WIN32
    // Initialize (in Windows)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif // WIN32

    // Initialize addr for broadcasting
    o2n_broadcast_to_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, "255.255.255.255",
                  &o2n_broadcast_to_addr.sin_addr.s_addr) != 1) {
        return O2_FAIL;
    }
    // create UDP broadcast socket
    // note: returning an error will result in o2_initialize calling
    // o2_finish, which calls o2n_finish, so all is properly shut down
    RETURN_IF_ERROR(o2n_broadcast_socket_new(&o2n_broadcast_sock));

    // Initialize addr for local sending
    local_to_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, "127.0.0.1",
                  &local_to_addr.sin_addr.s_addr) != 1) {
        return O2_FAIL;
    }
    // create UDP send socket
    if ((err = o2n_udp_send_socket_new(&o2n_udp_send_sock))) {
        o2n_finish();
        return err;
    }

    DA_INIT(o2n_fds, struct pollfd, 5);
    DA_INIT(o2n_fds_info, o2n_info_ptr, 5);
    o2n_recv_callout = recv;
    o2n_accept_callout = acc;
    o2n_connected_callout = conn;
    o2n_close_callout = clos;

    return err;
}


// cleanup and prepare to exit module
//
void o2n_finish()
{
    // o2_ctx->proc has been freed
    // local process name was removed as part of tcp server removal
    // tcp server socket was removed already by o2_finish
    // udp receive socket was removed already by o2_finish
    DA_FINISH(o2n_fds_info);
    DA_FINISH(o2n_fds);
    if (o2n_udp_send_sock != INVALID_SOCKET) {
        o2_closesocket(o2n_udp_send_sock, "o2n_finish (o2n_udp_send_sock)");
        o2n_udp_send_sock = INVALID_SOCKET;
    }
    if (o2n_broadcast_sock != INVALID_SOCKET) {
        o2_closesocket(o2n_broadcast_sock, "o2n_finish (o2n_broadcast_sock)");
        o2n_broadcast_sock = INVALID_SOCKET;
    }
#ifdef WIN32
    WSACleanup();    
#endif
}


/// allocate a message big enough for size bytes of data
// message also contains next and size fields
o2n_message_ptr o2n_message_new(int size)
{
    o2n_message_ptr msg = O2N_MESSAGE_ALLOC(size);
    msg->length = size;
    return msg;
}


o2n_info_ptr o2n_tcp_socket_new(int net_tag, int port, void *application)
{
    SOCKET sock = o2_socket(AF_INET, SOCK_STREAM, 0, "o2n_tcp_socket_new");
    if (sock == INVALID_SOCKET) {
        printf("tcp socket creation error");
        return NULL;
    }
    // make the socket non-blocking
    fcntl(sock, F_SETFL, O_NONBLOCK);

    o2n_info_ptr info = socket_info_new(sock, net_tag);
    assert(info);
    info->application = application;
    O2_DBo(printf("%s created tcp socket %ld index %d tag %s\n",
                  o2_debug_prefix, (long) sock, info->fds_index,
                  o2n_tag_to_string(net_tag)));
    // a "normal" TCP connection: set NODELAY option
    // (NODELAY means that TCP messages will be delivered immediately
    // rather than waiting a short period for additional data to be
    // sent. Waiting might allow the outgoing packet to consolidate
    // sent data, resulting in greater throughput, but more latency.
    set_nodelay_option(sock);

    info->port = port;
    return info;
}


// remove a socket from o2n_fds and o2n_fds_info
//
void o2n_socket_remove(o2n_info_ptr info)
{
    int index = info->fds_index;
    assert(index >= 0 && index < o2n_fds_info.length);
    (*o2n_close_callout)(info); // called before switching pointers
    struct pollfd *pfd = GET_O2N_FDS(index);

    O2_DBo(printf("%s o2n_socket_remove: net_tag %s port %d closing "
                  "socket %lld index %d\n",
                  o2_debug_prefix, o2n_tag_to_string(info->net_tag),
                  info->port, (long long) pfd->fd, index));
    if (o2n_fds.length > index + 1) { // move last to i
        struct pollfd *lastfd = DA_LAST_ADDR(o2n_fds, struct pollfd);
        memcpy(pfd, lastfd, sizeof *lastfd);
        o2n_info_ptr replace = DA_LAST(o2n_fds_info, o2n_info_ptr);
        DA_SET(o2n_fds_info, o2n_info_ptr, index, replace); // move to new index
        replace->fds_index = index;
    }
    o2n_fds.length--;
    o2n_fds_info.length--;
    assert(info->net_tag == NET_INFO_CLOSED);
    O2_FREE(info);
}


// assumes that if delete_me is set, the info structure has already been 
// cleaned up so that it no longer points to any heap structures and it
// is now safe to free the info structure itself.
//
void o2n_free_deleted_sockets()
{
    o2n_info_ptr info;
    // while deleting sockets, we might mark another socket for deletion
    // so we need to iterate the scan and delete loop until we make a
    // full pass with no more deletions. (Example: if we delete the OSC
    // TCP server socket, then all accepted sockets are then marked for
    // deletion, so there could be a second pass to remove them.)
    while (o2n_socket_delete_flag) {
        o2n_socket_delete_flag = false;
        int i = 0;
        while ((info = o2n_get_info(i++))) {
            if (info->delete_me) {
                o2n_socket_remove(info);
                i--;
            }
        }
    }
}


// create a TCP connection to a server
//
o2n_info_ptr o2n_connect(const char *ip, int tcp_port,
                         void * application)
{
    o2n_info_ptr info = o2n_tcp_socket_new(NET_TCP_CONNECTING, 0, application);
    if (!info) {
        return NULL;
    }
    o2n_address remote_addr;
    if (o2n_address_init(&remote_addr, ip, tcp_port, true)) {
        return NULL;
    }

    // note: our local port number is not recorded, not needed
    // get the socket just created by o2n_tcp_socket_new
    struct pollfd *pfd = GET_O2N_FDS(info->fds_index);
    SOCKET sock = pfd->fd;

    O2_DBo(printf("%s connect to %s:%d with socket %ld index %d\n",
                  o2_debug_prefix, ip, tcp_port, (long) sock,
                  o2n_fds.length - 1));
    if (connect(sock, (struct sockaddr *) &remote_addr.sa,
                sizeof remote_addr.sa) == -1) {
        if (errno != EINPROGRESS) {
            O2_DBo(perror("o2n_connect making TCP connection"));
            return socket_cleanup("connect error", info, sock);
        }
        // detect when we're connected by polling for writable
        pfd->events |= POLLOUT;
    } else { // wow, we're already connected, not sure this is possible
        DA_GET(o2n_fds_info, o2n_info_ptr, info->fds_index)->net_tag =
            NET_TCP_CLIENT;
        o2_disable_sigpipe(sock);
        O2_DBdo(printf("%s connected to %s:%d index %d\n",
                       o2_debug_prefix, ip, tcp_port, o2n_fds.length - 1));
    }
    return info;
}


// Take next step to send a message. If block is true, this call will 
//     block until all queued messages are sent or an error or closed
//     socket breaks the connection. If block is false, sending is 
//     asynchronous and only one step is taken, e.g. sending the next 
//     message in the queue. This function is normally used internally
//     without blocking. To avoid queuing up more than one, user-level
//     message, the o2_send() function will call this *with* blocking
//     when a message is already pending and o2_send is called again.
//
o2_err_t o2n_send(o2n_info_ptr info, bool block)
{
    int err;
    int flags = 0;
#ifndef __APPLE__
    flags = MSG_NOSIGNAL;
#endif
    if (info->net_tag == NET_INFO_CLOSED) {
        return O2_FAIL;
    }
    struct pollfd *pfd = GET_O2N_FDS(info->fds_index);
    if (info->net_tag == NET_TCP_CONNECTING && block) {
        O2_DBo(printf("%s: o2n_send - index %d tag is NET_TCP_CONNECTING, "
                      "so we wait\n", o2_debug_prefix, info->fds_index));
        // we need to wait until connected before we can send
        fd_set write_set;
        FD_ZERO(&write_set);
        FD_SET(pfd->fd, &write_set);
        int total;
        // try while a signal interrupts us
        while ((total = select(pfd->fd + 1, NULL,
                               &write_set, NULL, NULL)) != 1) {
#ifdef WIN32
            if (total == SOCKET_ERROR && errno != EINTR) {
#else
            if (total < 0 && errno != EINTR) {
#endif
                O2_DBo(perror("SOCKET_ERROR in o2n_recv"));
                return O2_SOCKET_ERROR;
            }
        }
        if (total != 1) {
            return O2_SOCKET_ERROR;
        }
        int socket_error;
        socklen_t errlen = sizeof socket_error;
        getsockopt(pfd->fd, SOL_SOCKET, SO_ERROR, &socket_error, &errlen);
        if (socket_error) {
            return O2_SOCKET_ERROR;
        }
        // otherwise, socket is writable, thus connected now
        info->net_tag = NET_TCP_CLIENT;
    }
    if (!block) {
        flags |= MSG_DONTWAIT;
    }
    o2n_message_ptr msg;
    while ((msg = info->out_message)) { // more messages to send
        // Send the length of the message followed by the message.
        // We want to do this in one send; otherwise, we'll send 2
        // network packets due to the NODELAY socket option.
        int32_t len = msg->length;
        int n;
        char *from;
        if (info->raw_flag) {
            from = msg->payload + info->out_msg_sent;
            n  = len - info->out_msg_sent;
        } else {  // need to send length field in network byte order:
            msg->length = htonl(len);
            from = ((char *) &msg->length) + info->out_msg_sent;
            n = len + sizeof msg->length - info->out_msg_sent;
        }
        // send returns ssize_t, but we will never send a big message, so
        // conversion to int will never overflow
        err = (int) send(pfd->fd, from, n, flags);
        msg->length = len; // restore byte-swapped len (noop if info->raw_flag)

        if (err < 0) {
            O2_DBo(perror("o2n_send sending a message"));
            if (!block && !TERMINATING_SOCKET_ERROR) {
                printf("setting POLLOUT on %d\n", info->fds_index);
                pfd->events |= POLLOUT; // request event when it unblocks
                return O2_BLOCKED;
            } else if (TERMINATING_SOCKET_ERROR) {
                O2_DBo(printf("%s removing remote process after send error "
                              "%d err %d to socket %ld index %d\n",
                              o2_debug_prefix, errno, err, (long) (pfd->fd),
                              info->fds_index));
                // free all messages in case there is a queue
                while (msg) {
                    o2n_message_ptr next = msg->next;
                    O2_FREE(msg);
                    msg = next;
                }
                info->out_message = NULL; // just to be safe, no dangling ptr
                o2n_close_socket(info);
                return O2_FAIL;
            } // else EINTR or EAGAIN, so try again
        } else {
            // err >= 0, update how much we have sent
            info->out_msg_sent += err;
            if (err >= n) { // finished sending message
                assert(info->out_msg_sent == n);
                info->out_msg_sent = 0;
                o2n_message_ptr next = msg->next;
                O2_FREE(msg);
                info->out_message = next;
                // now, while loop will send the next message if any
            } else if (!block) { // next send call would probably block
                printf("setting POLLOUT on %d\n", info->fds_index);
                pfd->events |= POLLOUT; // request event when writable
                return O2_BLOCKED;
            } // else, we're blocking, so loop and send more data
        }
    }
    return O2_SUCCESS;
}



// Send a message. Named "enqueue" to emphasize that this is asynchronous.
// Follow this call with o2n_send(info, true) to force a blocking
// (synchronous) send.
//
// msg content must be in network byte order
//
int o2n_enqueue(o2n_info_ptr info, o2n_message_ptr msg)
{
    // if nothing pending yet, no send in progress;
    //    set up to send this message
    msg->next = NULL; // make sure this will be the end of list
    if (!info->out_message && info->net_tag != NET_TCP_CONNECTING) {
        // nothing to block sending the message
        info->out_message = msg;
        info->out_msg_sent = 0;
        o2n_send(info, false);
    } else {
        // insert message at end of queue; normally queue is empty
        o2n_message_ptr *pending = &info->out_message;
        while (*pending) pending = &(*pending)->next;
        // now *pending is where to put the new message
        *pending = msg;
    }
    return O2_SUCCESS;
}


void o2n_close_socket(o2n_info_ptr info)
{
    O2_DBo(printf("%s o2n_close_socket called with info %p (%s)\n",
                  o2_debug_prefix, info, o2n_tag_to_string(info->net_tag)));
    if (info->in_message) O2_FREE(info->in_message);
    info->in_message = NULL; // in case we're closed again
    while (info->out_message) {
        o2n_message_ptr p = info->out_message;
        info->out_message = p->next;
        O2_FREE(p);
    }
    struct pollfd *pfd = GET_O2N_FDS(info->fds_index);
    SOCKET sock = pfd->fd;
    if (sock != INVALID_SOCKET) { // in case we're closed again
        #ifdef SHUT_WR
            shutdown(sock, SHUT_WR);
        #endif
        o2_closesocket(sock, "o2n_close_socket");
        pfd->fd = INVALID_SOCKET;
        info->net_tag = NET_INFO_CLOSED;
    }
    info->delete_me = true;
    o2n_socket_delete_flag = true;
}


#ifdef WIN32

FD_SET o2_read_set;
FD_SET o2_write_set;
struct timeval o2_no_timeout;

o2_err_t o2n_recv()
{
    // if there are any bad socket descriptions, remove them now
    if (o2n_socket_delete_flag) o2n_free_deleted_sockets();
    
    int total;
    
    FD_ZERO(&o2_read_set);
    FD_ZERO(&o2_write_set);
    for (int i = 0; i < o2n_fds.length; i++) {
        struct pollfd *d = GET_O2N_FDS(i);
        FD_SET(d->fd, &o2_read_set);
        o2n_info_ptr info = GET_O2N_INFO(i);
        if (TAG_IS_REMOTE(info->tag) && info->proc.pending_msg) {
            FD_SET(d->fd, &o2_write_set);
        }
    }
    o2_no_timeout.tv_sec = 0;
    o2_no_timeout.tv_usec = 0;
    if ((total = select(0, &o2_read_set, &o2_write_set, NULL, 
                        &o2_no_timeout)) == SOCKET_ERROR) {
        O2_DBo(printf("%s SOCKET_ERROR in o2n_recv", o2_debug_prefix));
        return O2_SOCKET_ERROR;
    }
    if (total == 0) { /* no messages waiting */
        return O2_SUCCESS;
    }
    for (int i = 0; i < o2n_fds.length; i++) {
        struct pollfd *pfd = GET_O2N_FDS(i);
        if (FD_ISSET(pfd->fd, &o2_read_set)) {
            o2n_info_ptr info = GET_O2N_INFO(i);
            if ((read_event_handler(pfd->fd, info)) == O2_TCP_HUP) {
                O2_DBo(printf("%s removing remote process after O2_TCP_HUP to "
                              "socket %ld", o2_debug_prefix, (long) pfd->fd));
                o2n_close_socket(info);
            }
        }
        if (FD_ISSET(pfd->fd, &o2_write_set)) {
            o2n_info_ptr info = GET_O2N_INFO(i);
            o2_message_ptr msg = info->proc.pending_msg; // unlink pending msg
            info->proc.pending_msg = NULL;
            o2_err_t rslt = o2n_send(info, false);
            assert(false); // need to handle multiple queued messages
            if (rslt == O2_SUCCESS) {
                // printf("clearing POLLOUT on %d\n", info->fds_index);
                pfd->events &= ~POLLOUT;
            }
        }            
        if (!o2_ensemble_name) { // handler called o2_finish()
            // o2n_fds are all freed and gone
            return O2_FAIL;
        }
    }
    // clean up any dead sockets before user has a chance to do anything
    // (actually, user handlers could have done a lot, so maybe this is
    // not strictly necessary.)
    if (o2n_socket_delete_flag) o2n_free_deleted_sockets();
    return O2_SUCCESS;
}

#else  // Use poll function to receive messages.

o2_err_t o2n_recv()
{
    int i;
        
    // if there are any bad socket descriptions, remove them now
    if (o2n_socket_delete_flag) o2n_free_deleted_sockets();

    poll((struct pollfd *) o2n_fds.array, o2n_fds.length, 0);
    int len = o2n_fds.length; // length can grow while we're looping!
    for (i = 0; i < len; i++) {
        o2n_info_ptr info;
        struct pollfd *pfd = GET_O2N_FDS(i);
        // if (pfd->revents) printf("%d:%p:%x ", i, d, d->revents);
        if (pfd->revents & POLLERR) {
        } else if (pfd->revents & POLLHUP) {
            info = GET_O2N_INFO(i);
            O2_DBo(printf("%s removing remote process after POLLHUP to "
                          "socket %ld index %d\n", o2_debug_prefix,
                          (long) (pfd->fd), i));
            o2n_close_socket(info);
        // do this first so we can change PROCESS_CONNECTING to
        // PROCESS_CONNECTED when socket becomes writable
        } else if (pfd->revents & POLLOUT) {
            info = GET_O2N_INFO(i); // find process info
            if (info->net_tag == NET_TCP_CONNECTING) { // connect() completed
                info->net_tag = NET_TCP_CLIENT;
                // tell next layer up that connection is good, e.g. O2 sends
                // notification that a new process is connected
                (*o2n_connected_callout)(info);
            }
            // now we have a completed connection and events has POLLOUT
            if (info->out_message) {
                o2_err_t rslt = o2n_send(info, false);
                if (rslt == O2_SUCCESS) {
                    pfd->events &= ~POLLOUT;
                }
            } else { // no message to send, clear polling
                pfd->events &= ~POLLOUT;
            }
        } else if (pfd->revents & POLLIN) {
            info = GET_O2N_INFO(i);
            assert(info->in_length_got < 5);
            if (read_event_handler(pfd->fd, info)) {
                O2_DBo(printf("%s removing remote process after handler "
                              "reported error on socket %ld", o2_debug_prefix, 
                              (long) (pfd->fd)));
                o2n_close_socket(info);
            }
        }
        if (!o2_ensemble_name) { // handler called o2_finish()
            // o2n_fds are all free and gone now
            return O2_FAIL;
        }
    }
    // clean up any dead sockets before user has a chance to do anything
    // (actually, user handlers could have done a lot, so maybe this is
    // not strictly necessary.)
    if (o2n_socket_delete_flag) o2n_free_deleted_sockets();
    return O2_SUCCESS;
}
#endif


/******* handlers for socket events *********/

// clean up info to prepare for next message
//
static void info_message_cleanup(o2n_info_ptr info)
{
    info->in_message = NULL;
    info->in_msg_got = 0;
    info->in_length = 0;
    info->in_length_got = 0;
}


// returns O2_SUCCESS if whole message is read.
//         O2_FAIL if whole message is not read yet.
//         O2_TCP_HUP if socket is closed
//
static o2_err_t read_whole_message(SOCKET sock, o2n_info_ptr info)
{
    int n;
    assert(info->in_length_got < 5);
    if (info->raw_flag) {
        // allow raw messages up to 512 bytes
        info->in_message = O2N_MESSAGE_ALLOC(512);
        n = (int) recvfrom(sock, info->in_message->payload, 512, 0, NULL, NULL);
        if (n < 0) {
            goto error_exit;
        }
        info->in_message->length = n;
    } else {
        /* first read length if it has not been read yet */
        if (info->in_length_got < 4) {
            // coerce to int to avoid compiler warning; requested length is
            // int, so int is ok for n
            n = (int) recvfrom(sock,
                               PTR(&info->in_length) + info->in_length_got,
                               4 - info->in_length_got, 0, NULL, NULL);
            if (n <= 0) {
                goto error_exit;
            }
            info->in_length_got += n;
            assert(info->in_length_got < 5);
            if (info->in_length_got < 4) {
                return O2_FAIL; // length is not received yet, get more later
            }
            // done receiving length bytes
            info->in_length = htonl(info->in_length);
            assert(!info->in_message);
            info->in_message = o2n_message_new(info->in_length);
            info->in_msg_got = 0; // just to make sure
        }
        
        /* read the full message */
        if (info->in_msg_got < info->in_length) {
            // coerce to int to avoid compiler warning; will not overflow
            n = (int) recvfrom(sock,
                          info->in_message->payload + info->in_msg_got,
                          info->in_length - info->in_msg_got, 0, NULL, NULL);
            if (n <= 0) {
                goto error_exit;
            }
            info->in_msg_got += n;
            if (info->in_msg_got < info->in_length) {
                return O2_FAIL; // message is not complete, get more later
            }
        }
        info->in_message->length = info->in_length;
    }
    return O2_SUCCESS; // we have a full message now
  error_exit:
    if (n == 0) { /* socket was gracefully closed */
        O2_DBo(printf("recvfrom returned 0: deleting socket\n"));
        info_message_cleanup(info);
        return O2_TCP_HUP;
    } else if (n < 0) { /* error: close the socket */
        if (TERMINATING_SOCKET_ERROR) {
            perror("recvfrom in read_whole_message");
            if (info->in_message) {
                O2_FREE(info->in_message);
            }
            info_message_cleanup(info);
            return O2_TCP_HUP;
        }
    }
    return O2_FAIL; // not finished reading
}


static int read_event_handler(SOCKET sock, o2n_info_ptr info)
{
    if (info->net_tag == NET_TCP_CONNECTION ||
        info->net_tag == NET_TCP_CLIENT) {
        int n = read_whole_message(sock, info);
        if (n == O2_FAIL) { // not ready to process message yet
            return O2_SUCCESS; // not a problem, but we're done for now
        } else if (n != O2_SUCCESS) {
            return n; // some other error, i.e. O2_TCP_HUP
        }
        // fall through and send message
    } else if (info->net_tag == NET_UDP_SERVER) {
        int len;
        if (ioctlsocket(sock, FIONREAD, &len) == -1) {
            perror("udp_recv_handler");
            return O2_FAIL;
        }
        assert(!info->in_message);
        info->in_message = o2n_message_new(len);
        if (!info->in_message) return O2_FAIL;
        int n;
        // coerce to int to avoid compiler warning; ok because len is int
        if ((n = (int) recvfrom(sock, (char *) &info->in_message->payload, len,
                                0, NULL, NULL)) <= 0) {
            // I think udp errors should be ignored. UDP is not reliable
            // anyway. For now, though, let's at least print errors.
            perror("recvfrom in udp_recv_handler");
            O2_FREE(info->in_message);
            info->in_message = NULL;
            return O2_FAIL;
        }
#if CLOSE_SOCKET_DEBUG
        printf("***UDP received %d bytes at %g.\n", n, o2_local_time());
#endif
        info->in_message->length = n;
        // fall through and send message
    } else if (info->net_tag == NET_TCP_SERVER) {
        // note that this handler does not call read_whole_message()
        SOCKET connection = o2_accept(sock, NULL, NULL, "read_event_handler");
        if (connection == INVALID_SOCKET) {
            O2_DBG(printf("%s tcp_accept_handler failed to accept\n",
                          o2_debug_prefix));
            return O2_FAIL;
        }
        int set = 1;
#ifdef __APPLE__
        setsockopt(connection, SOL_SOCKET, SO_NOSIGPIPE,
                   (void *) &set, sizeof set);
#endif
        o2n_info_ptr conn = socket_info_new(connection, NET_TCP_CONNECTION);
        O2_DBdo(printf("%s O2 server socket %ld accepts client as socket "
                       "%ld index %d\n", o2_debug_prefix, (long) sock,
                       (long) connection, conn->fds_index));
        assert(conn);
        (*o2n_accept_callout)(info, conn);
        return O2_SUCCESS;
    } else {
        assert(false);
    }
    // COMMON CODE for TCP and UDP receive message:
    // endian corrections are done in handler
    o2n_message_source = info;
    if ((*o2n_recv_callout)(info) == O2_SUCCESS) {
        info_message_cleanup(info);
    } else if (info->net_tag == NET_TCP_CONNECTING ||
               info->net_tag == NET_TCP_CLIENT ||
               info->net_tag == NET_TCP_CONNECTION) {
        o2n_close_socket(info);
    }
    return O2_SUCCESS;
}

#ifndef O2_NO_DEBUG
static const char *entry_tags[6] = {
    "NET_UDP_SERVER", "NET_TCP_SERVER", "NET_TCP_CONNECTING",
    "NET_TCP_CLIENT", "NET_TCP_CONNECTION", "NET_INFO_CLOSED" };

const char *o2n_tag_to_string(int tag)
{
    if (tag >= NET_UDP_SERVER && tag <= NET_INFO_CLOSED)
        return entry_tags[tag - NET_UDP_SERVER];
    static char unknown[32];
    snprintf(unknown, 32, "Tag-%d", tag);
    return unknown;
}

const char *o2n_tag(int i)
{
    return o2n_tag_to_string(o2n_get_info(i)->net_tag);
}

int o2n_socket(int i)
{
    return GET_O2N_FDS(i)->fd;
}
#endif
