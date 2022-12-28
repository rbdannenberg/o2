// network.cpp -- implementation of network communication
//
// Roger B. Dannenberg, 2020
//

#include <fcntl.h>
#include <sys/types.h>
#include <ctype.h>
#include "o2internal.h"
#include <errno.h>

#ifdef WIN32
#include <stdio.h> 
#include <stdlib.h> 

// test after recvfrom() < 0 to see if the socket should close
#define TERMINATING_SOCKET_ERROR \
    (WSAGetLastError() != WSAEWOULDBLOCK && WSAGetLastError() != WSAEINTR)

void print_socket_error(int err, const char *source)
{
    char errbuf[256];
    errbuf[0] = 0;
    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                  NULL, err, 0, errbuf, sizeof(errbuf), NULL);
    if (!errbuf[0]) {
        sprintf(errbuf, "%d", err);  // error as number if no string 
    }
    O2_DBo(fprintf(stderr, "%s SOCKET_ERROR in %s: %s\n",
                   o2_debug_prefix, source, errbuf);)
}
#else
#include <sys/socket.h>
#include <unistd.h>    // define close()
#include <netdb.h>
#include <sys/time.h>

#include "sys/ioctl.h"
#include <ifaddrs.h>
#include <sys/poll.h>
#include <netinet/tcp.h>

#define TERMINATING_SOCKET_ERROR (errno != EAGAIN && errno != EINTR)

void print_socket_error(int err, const char *source)
{
    O2_DBo(fprintf(stderr, "%s in %s:\n", o2_debug_prefix, source);
           perror("SOCKET_ERROR");)
}
#endif

static Vec<struct pollfd> o2n_fds;  ///< pre-constructed fds parameter for poll()
Vec<Fds_info *> o2n_fds_info; ///< info about sockets

char o2n_public_ip[O2N_IP_LEN] = "";
char o2n_internal_ip[O2N_IP_LEN] = "";

static struct sockaddr_in o2_serv_addr;

//static int tcp_recv_handler(SOCKET sock, Net_interface *info);
//static int udp_recv_handler(SOCKET sock, Net_interface *info);
//static void tcp_message_cleanup(Net_interface *info);

// a socket for sending broadcast messages:
SOCKET o2n_broadcast_sock = INVALID_SOCKET;
// address for sending broadcast messages:
Net_address o2n_broadcast_to_addr;

// a socket for general UDP message sends:
SOCKET o2n_udp_send_sock = INVALID_SOCKET;
// address for sending discovery UDP messages to local host:
static struct sockaddr_in local_to_addr;

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

SOCKET o2_accept(SOCKET socket, struct sockaddr *address,
                 socklen_t *address_len, const char *who)
{
    SOCKET sock = accept(socket, address, address_len);
    if (sock >= 0) {
        long s = (long) sock; // whatever the type is, get it and print it
        printf("**** accepted socket %ld for %s\n", s, who);
    }
    return sock;
}

void o2_closesocket(SOCKET sock, const char *who)true;
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


// initialize an o2n_address from ip and port number
//   ip is domain name, "localhost", or dot notation, not hex
O2err Net_address::init(const char *ip, int port_num, bool tcp_flag)
{
    O2err rslt = O2_SUCCESS;
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
        memcpy(&sa, aiptr->ai_addr, sizeof sa);
        if (sa.sin_port == 0) {
            sa.sin_port = htons((short) port_num);
        }
    }
    if (aiptr) freeaddrinfo(aiptr);
    return rslt;
}


O2err Net_address::init_hex(const char *ip, int port_num, bool tcp_flag)
{
    char ip_dot_form[O2N_IP_LEN];
    o2_hex_to_dot(ip, ip_dot_form);
    return init(ip_dot_form, port_num, tcp_flag);
}


O2err o2n_send_udp_via_socket(SOCKET socket, Net_address *ua, 
                                 O2netmsg_ptr msg)
{
    ssize_t err = sendto(socket, &msg->payload[0], msg->length,
                         0, (struct sockaddr *) &ua->sa,
                         sizeof ua->sa);
    O2_FREE(msg);
    if (err < 0) {
        printf("error sending udp to port %d ", ntohs(ua->sa.sin_port));
        perror("o2n_send_udp_via_socket");
        return O2_FAIL;
    }
    return O2_SUCCESS;
}


// send a udp message to an address, free the msg
O2err o2n_send_udp(Net_address *ua, O2netmsg_ptr msg)
{
    return o2n_send_udp_via_socket(o2n_udp_send_sock, ua, msg);
}


// send udp message to local port. msg is owned/freed by this function.
// msg must be in network byte order
//
void o2n_send_udp_local(int port, O2netmsg_ptr msg)
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


O2err Fds_info::can_send()
{
    // O2_SUCCESS if TCP socket and !out_message
    // otherwise O2_BLOCKED
    if ((net_tag & NET_TCP_MASK) != 0) {
        return (out_message == NULL) ? O2_SUCCESS : O2_BLOCKED;
    } else if (net_tag & NET_TCP_CONNECTING) {
        return O2_BLOCKED;
    }
    // all the TCP cases are handled above. If this is UDP it
    // could be a server port, but you cannot send or block on
    // that, so we return O2_FAIL.
    return O2_FAIL;
}


// This function takes ownership of msg
O2err Fds_info::send_tcp(bool block, O2netmsg_ptr msg)
{
    // if proc has a pending message, we must send with blocking
    if (out_message && block) {
        O2err rslt = send(true);
        if (rslt != O2_SUCCESS) { // process is dead and removed
            O2_FREE(msg); // we drop the message
            return rslt;
        }
    }
    // now send the new msg
    enqueue(msg);
    return O2_SUCCESS;
}


// Important: msg is owned by caller, msg is in network order except for length
ssize_t o2n_send_broadcast(int port, O2netmsg_ptr msg)
{
    o2n_broadcast_to_addr.set_port(port);
    ssize_t err = sendto(o2n_broadcast_sock,
                         ((char *) &msg->length) + sizeof msg->length,
                         msg->length, 0,
                         o2n_broadcast_to_addr.get_sockaddr(),
                         sizeof(struct sockaddr_in));
    if (err < 0) {
        perror("Error attempting to broadcast discovery message");
    }
    return err;
}


// create a UDP send socket for broadcast or general sends
//
SOCKET o2n_udp_send_socket_new()
{
    SOCKET sock = o2_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP,
                            "o2n_udp_send_socket_new");
    if (sock == INVALID_SOCKET) {
        perror("allocating udp send socket");
    } else {
        O2_DBo(printf("%s allocating udp send socket %ld\n",
                      o2_debug_prefix, (long) sock));
    }
    return sock;
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


static int bind_recv_socket(SOCKET sock, int *port, bool tcp_recv_flag,
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
        if (tcp_recv_flag) {
            perror("Bind receive socket");
            fprintf(stderr, "    (Address is INADDR_ANY on port %d)\n", *port);
        }
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
    O2_DBo(printf("*   %s bind socket %ld port %d\n", o2_debug_prefix,
                  (long) sock, *port));
    assert(*port != 0);
    return O2_SUCCESS;
}


// add a new socket to the fds and fds_info arrays,
// on success, Net_interface *descriptor is initialized
//
// was o2n_socket_info_new()
Fds_info::Fds_info(SOCKET sock, int net_tag_, int port_, Net_interface *own)
{
    net_tag = net_tag_;
    fds_index = o2n_fds.size(); // this will be the last element
    delete_me = 0;
    read_type = READ_O2;
    in_length = 0;
    in_message = NULL;
    in_length_got = 0;
    in_msg_got = 0;
    out_message = NULL;
    out_msg_sent = 0;
    port = port_;
    owner = own;
#ifndef O2_NO_DEBUG
    trace_socket_flag = false;  // option to report when this closes
#endif
    
    o2n_fds_info.push_back(this);
    struct pollfd *pfd = o2n_fds.append_space(1);
    pfd->fd = sock;
    assert(sock != INVALID_SOCKET);
    pfd->events = POLLIN;
    pfd->revents = 0;
    O2_DBo(printf("%s new Fds_info %p socket %ld index %d\n", o2_debug_prefix,
                  this, (long) sock, fds_index));
#if CLOSE_SOCKET_DEBUG
    printf("**Fds_info constructor:\n");
    for (int i = 0; i < o2n_fds.size(); i++) {
        long s = (long) o2n_fds[i].fd;
        printf("    %d: %ld\n", i, s);
    }
#endif
}


static void set_nodelay_option(SOCKET sock)
{
    int option = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *) &option,
               sizeof option);
}


Fds_info *Fds_info::cleanup(const char *error, SOCKET sock)
{
    perror(error);
    o2_closesocket(sock, "socket_cleanup");
    delete this;  // this Fds_info will be removed from socket arrays 
    return NULL;  // so caller can "return info->cleanup(...)"
}


Fds_info *Fds_info::create_tcp_server(int *port, Net_interface *own)
{
    SOCKET sock = o2n_tcp_socket_new();
    if (sock == INVALID_SOCKET) {
        return NULL;
    }
    // bind server port
    if (bind_recv_socket(sock, port, true, true) != O2_SUCCESS ||
        listen(sock, 10) != 0) {
        o2_closesocket(sock, "tcp_server bind_recv-socket & listen");
        return NULL;
    }
    O2_DBo(printf("%s bind and listen called on socket %ld\n",
                  o2_debug_prefix, (long) sock));
    return new Fds_info(sock, NET_TCP_SERVER, *port, own);
}


// creates a server listening to port, or can also be a client
// where you send messages to socket and expect a UDP reply to port
// 
Fds_info *Fds_info::create_udp_server(int *port, bool reuse)
{
    SOCKET sock = o2_socket(AF_INET, SOCK_DGRAM, 0, "create_udp_server");
    if (sock == INVALID_SOCKET) {
        return NULL;
    }
    // Bind the socket
    int err;
    if ((err = bind_recv_socket(sock, port, false, reuse))) {
        o2_closesocket(sock, "bind failed in create_udp_server");
        return NULL;
    }
    return new Fds_info(sock, NET_UDP_SERVER, *port, NULL);
}


SOCKET o2n_broadcast_socket_new()
{
    // Set up a socket for broadcasting discovery messages
    SOCKET sock = o2n_udp_send_socket_new();
    if (sock == INVALID_SOCKET) {
        return sock;
    }
    // Set the socket's option to broadcast
    int optval = true; // type is correct: int, not bool
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST,
                   (const char *) &optval, sizeof optval) == -1) {
        perror("Set socket to broadcast");
        o2_closesocket(sock, "setsockopt failed in o2n_broadcast_socket_new");
        return INVALID_SOCKET;
    }
    return sock;
}


// initialize this module
// - create UDP broadcast socket
// - create UDP send socket
O2err o2n_initialize()
{
#ifdef WIN32
    // Initialize (in Windows)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif // WIN32
    o2n_network_found = false;
    if (o2n_network_enabled) {
        o2n_internal_ip[0] = 0;  // IP addresses are looked up, initially
        o2n_public_ip[0] = 0;    // they are unknown
        o2n_get_internal_ip(o2n_internal_ip);
        // Initialize addr for broadcasting
        o2n_broadcast_to_addr.get_sockaddr()->sa_family = AF_INET;
        if (inet_pton(AF_INET, "255.255.255.255",
                      &o2n_broadcast_to_addr.get_in_addr()->s_addr) != 1) {
            return O2_FAIL;
        }
        // create UDP broadcast socket
        // note: returning an error will result in o2_initialize calling
        // o2_finish, which calls o2n_finish, so all is properly shut down
        o2n_broadcast_sock = o2n_broadcast_socket_new();
        if (o2n_broadcast_sock == INVALID_SOCKET) {
            return O2_FAIL;
        }
    } else {
        strcpy(o2n_public_ip, "00000000");
        strcpy(o2n_internal_ip, "7f000001");
    }

    // Initialize addr for local sending
    local_to_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, "127.0.0.1",
                  &local_to_addr.sin_addr.s_addr) != 1) {
        return O2_FAIL;
    }
    // create UDP send socket
    o2n_udp_send_sock = o2n_udp_send_socket_new();
    if (o2n_udp_send_sock == INVALID_SOCKET) {
        o2n_finish();
        return O2_FAIL;
    }

    o2n_fds.init(5);
    o2n_fds_info.init(5);

    return O2_SUCCESS;
}


// cleanup and prepare to exit module
//
void o2n_finish()
{
    // o2_ctx->proc has been freed
    // local process name was removed as part of tcp server removal
    // tcp server socket was removed already by o2_finish
    // udp receive socket was removed already by o2_finish
    o2n_fds_info.finish();
    o2n_fds.finish();
    if (o2n_udp_send_sock != INVALID_SOCKET) {
        o2_closesocket(o2n_udp_send_sock, "o2n_finish (o2n_udp_send_sock)");
        o2n_udp_send_sock = INVALID_SOCKET;
    }
    if (o2n_broadcast_sock != INVALID_SOCKET) {
        o2_closesocket(o2n_broadcast_sock, "o2n_finish (o2n_broadcast_sock)");
        o2n_broadcast_sock = INVALID_SOCKET;
    }
    o2n_network_found = false;
#ifdef WIN32
    WSACleanup();    
#endif
}


/// allocate a message big enough for size bytes of data
// message also contains next and size fields
O2netmsg_ptr O2netmsg_new(int size)
{
    O2netmsg_ptr msg = O2N_MESSAGE_ALLOC(size);
    msg->length = size;
    return msg;
}


SOCKET o2n_tcp_socket_new()
{
    SOCKET sock = o2_socket(AF_INET, SOCK_STREAM, 0, "o2n_tcp_socket_new");
    if (sock == INVALID_SOCKET) {
        printf("tcp socket creation error");
        return sock;
    }
#ifdef WIN32
    u_long nonblocking_enabled = TRUE;
    ioctlsocket(sock, FIONBIO, &nonblocking_enabled);
#else
    // make the socket non-blocking
    fcntl(sock, F_SETFL, O_NONBLOCK);
#endif
    O2_DBo(printf("%s created tcp socket %ld\n", o2_debug_prefix, (long) sock));
    // a "normal" TCP connection: set NODELAY option
    // (NODELAY means that TCP messages will be delivered immediately
    // rather than waiting a short period for additional data to be
    // sent. Waiting might allow the outgoing packet to consolidate
    // sent data, resulting in greater throughput, but more latency.
    set_nodelay_option(sock);
    return sock;
}


// remove a socket from o2n_fds and o2n_fds_info
//
Fds_info::~Fds_info()
{
    assert(o2n_fds.bounds_check(fds_index));
    struct pollfd *pfd = &o2n_fds[fds_index];

    O2_DBc(if (owner) {
               Proxy_info *proxy = (Proxy_info *) owner;
               proxy->co_info(this, "deleting Fds_info");
           } else {
               printf("%s deleting Fds_info: net_tag %s port %d closing "
                      "socket %ld index %d (no owner)\n",
                      o2_debug_prefix, Fds_info::tag_to_string(net_tag),
                      port, (long) pfd->fd, fds_index);
           });
    if (o2n_fds.size() > fds_index + 1) { // move last to i
        struct pollfd *lastfd = &o2n_fds.last();
        memcpy(pfd, lastfd, sizeof *lastfd);
        Fds_info *replace = o2n_fds_info.last();
        o2n_fds_info[fds_index] = replace; // move to new index
        replace->fds_index = fds_index;
        O2_DBc(if (replace->owner) {
                   Proxy_info * proxy = (Proxy_info *) replace->owner;
                   proxy->co_info(replace, "moved to new index");
               } else {
                   printf("%s net_tag %s port %d moved socket %ld "
                          "to index %d\n", o2_debug_prefix,
                          Fds_info::tag_to_string(replace->net_tag),
                          replace->port, (long) pfd->fd, fds_index);
               });
    }
    o2n_fds.pop_back();
    o2n_fds_info.pop_back();
    assert(net_tag == NET_INFO_CLOSED);
    if (owner) {
        owner->remove();
    }
}


// assumes that if delete_me is 2, the info structure has already been
// cleaned up so that it no longer points to any heap structures and it
// is now safe to free the info structure itself.
//
void o2n_free_deleted_sockets()
{
    // while deleting sockets, we might mark another socket for deletion
    // so we need to iterate the scan and delete loop until we make a
    // full pass with no more deletions. (Example: if we delete the OSC
    // TCP server socket, then all accepted sockets are then marked for
    // deletion, so there could be a second pass to remove them.)
    while (o2n_socket_delete_flag) {
        o2n_socket_delete_flag = false;
        int i = 0;
        while (i < o2n_fds_info.size()) {
            Fds_info *fi = o2n_fds_info[i];
            if (fi->delete_me == 2) {  // if we delete fi at index i, it will
                delete fi;             // replace itself with the last Fds_info
            } else {                   // so we iterate and reexamine index i;
                i++;                   // otherwise, move on to index i+1.
            }
        }
    }
}


// create a TCP connection to a server
//    ip is in dot format or domain name or localhost, not hex format
Fds_info *Fds_info::create_tcp_client(const char *ip, int tcp_port,
                                      Net_interface *own)
{
    Net_address remote_addr;
    if (remote_addr.init(ip, tcp_port, true) != O2_SUCCESS) {
        return NULL;
    }
    return create_tcp_client(&remote_addr, own);
}

Fds_info *Fds_info::create_tcp_client(Net_address *remote_addr,
                                      Net_interface *own)
{
    SOCKET sock = o2n_tcp_socket_new();
    if (sock == INVALID_SOCKET) {
        return NULL;
    }
    // add the socket to our list of sockets
    Fds_info *info = new Fds_info(sock, NET_TCP_CONNECTING, 0, own);
    // note: our local port number is not recorded, not needed
    // get the socket just created by init_as_tcp_socket
    struct pollfd *pfd = &o2n_fds[info->fds_index];

    O2_DBo(printf("%s connect to %x:? with socket %ld index %d\n",
                  o2_debug_prefix, remote_addr->get_in_addr()->s_addr,
                  (long) sock, o2n_fds.size() - 1));
    if (::connect(sock, remote_addr->get_sockaddr(),
                        sizeof(struct sockaddr)) == -1) {
#ifdef WIN32
        if (WSAGetLastError() != WSAEWOULDBLOCK) {
#else
        if (errno != EINPROGRESS) {
#endif
            O2_DBo(perror("o2n_connect making TCP connection"));
            return info->cleanup("connect error", sock);
        }
        // detect when we're connected by polling for writable
        pfd->events |= POLLOUT;
    } else { // wow, we're already connected, not sure this is possible
        o2n_fds_info[info->fds_index]->net_tag = NET_TCP_CLIENT;
        o2_disable_sigpipe(sock);
        O2_DBdo(printf("%s connected to %x:? index %d\n",
                       o2_debug_prefix, remote_addr->get_in_addr()->s_addr,
                       o2n_fds.size() - 1));
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
O2err Fds_info::send(bool block)
{
    int err;
    int flags = 0;
#if __linux__
    flags = MSG_NOSIGNAL;
#endif
    if (net_tag == NET_INFO_CLOSED) {
        return O2_FAIL;
    }
    struct pollfd *pfd = &o2n_fds[fds_index];
    if (net_tag == NET_TCP_CONNECTING && block) {
        O2_DBo(printf("%s o2n_send - index %d tag is NET_TCP_CONNECTING, "
                      "so we poll\n", o2_debug_prefix, fds_index));
        // we need to wait until connected before we can send
        while (o2n_recv() == O2_SUCCESS && net_tag == NET_TCP_CONNECTING) {
            o2_sleep(1);
        }
    }
    // if we are already in o2n_recv(), it will return O2_ALREADY_RUNNING
    // and no progress will be made, so as a last resort we just block
    // with select.
    if (net_tag == NET_TCP_CONNECTING && block) {
        O2_DBo(printf("%s o2n_send - index %d tag is NET_TCP_CONNECTING, "
                          "so we wait\n", o2_debug_prefix, fds_index));
        fd_set write_set;
        FD_ZERO(&write_set);
        FD_SET(pfd->fd, &write_set);
        int total;
        // try while a signal interrupts us
        while ((total = select((int) (pfd->fd + 1), NULL,
                               &write_set, NULL, NULL)) != 1) {
#ifdef WIN32
            int err;
            if (total == SOCKET_ERROR && (err = WSAGetLastError()) != WSAEINTR) {
                print_socket_error(err, "Fds_info::send");
#else
            if (total < 0 && errno != EINTR) {
                print_socket_error(errno, "Fds_info::send");
#endif
                return O2_SOCKET_ERROR;
            }
        }
        if (total != 1) {
            return O2_SOCKET_ERROR;
        }
        int socket_error;
        socklen_t errlen = sizeof socket_error;
        // linux uses (void *), Windows uses (char *) for arg 4
        getsockopt(pfd->fd, SOL_SOCKET, SO_ERROR, (char *) &socket_error, &errlen);
        if (socket_error) {
            return O2_SOCKET_ERROR;
        }
        // otherwise, socket is writable, thus connected now
        net_tag = NET_TCP_CLIENT;
        if (owner) owner->connected();
    }
#ifndef WIN32
    if (!block) {
        flags |= MSG_DONTWAIT;
    }
#endif
    O2netmsg_ptr msg;
    while ((msg = out_message)) { // more messages to send
        // Send the length of the message followed by the message.
        // We want to do this in one send; otherwise, we'll send 2
        // network packets due to the NODELAY socket option.
        int32_t len = msg->length;
        int n;
        char *from;
        if (read_type == READ_RAW) {
            /*
            if (out_msg_sent == 0) {
                printf("READ_RAW info sending: ||");
                for (int i = 0; i < msg->length; i++) {
                    int c = msg->payload[i];
                    if (isgraph(c)) putchar(c);
                    else printf("X%02x", c & 0xff);
                }
                printf("||\n");
            }
            */
            from = msg->payload + out_msg_sent;
            n  = len - out_msg_sent;
        } else {  // need to send length field in network byte order:
            msg->length = htonl(len);
            from = ((char *) &msg->length) + out_msg_sent;
            n = len + sizeof msg->length - out_msg_sent;
        }
        // send returns ssize_t, but we will never send a big message, so
        // conversion to int will never overflow
        err = (int) ::send(pfd->fd, from, n, flags);
        msg->length = len; // restore byte-swapped len (noop if READ_RAW)

        if (err < 0) {
            O2_DBo(perror("Net_interface::send sending a message"));
            if (!block && !TERMINATING_SOCKET_ERROR) {
                pfd->events |= POLLOUT; // request event when it unblocks
                return O2_BLOCKED;
            } else if (TERMINATING_SOCKET_ERROR) {
                O2_DBo(printf("%s removing remote process after send error "
                              "%d err %d to socket %ld index %d\n",
                              o2_debug_prefix, errno, err, (long) (pfd->fd),
                              fds_index));
                close_socket(true);  // this will free any pending messages
                return O2_FAIL;
            } // else EINTR or EAGAIN, so try again
        } else {
            // err >= 0, update how much we have sent
            out_msg_sent += err;
            if (err >= n) { // finished sending message
                assert(err == n);
                out_msg_sent = 0;
                O2netmsg_ptr next = msg->next;
                O2_FREE(msg);
                out_message = next;
                // now, while loop will send the next message if any
            } else if (!block) { // next send call would probably block
                pfd->events |= POLLOUT; // request event when writable
                return O2_BLOCKED;
            } // else we're blocking, so loop and send more data
        }
    }
    return O2_SUCCESS;
}



// Send a message. Named "enqueue" to emphasize that this is asynchronous.
// Follow this call with o2n_send(interf, true) to force a blocking
// (synchronous) send.
//
// msg content must be in network byte order
//
void Fds_info::enqueue(O2netmsg_ptr msg)
{
    // if nothing pending yet, no send in progress;
    //    set up to send this message
    msg->next = NULL; // make sure this will be the end of list
    if (!out_message && !(net_tag & NET_TCP_CONNECTING)) {
        // nothing to block sending the message
        out_message = msg;
        out_msg_sent = 0;
        send(false);
    } else {
        // insert message at end of queue; normally queue is empty
        O2netmsg_ptr *pending = &out_message;
        while (*pending) pending = &(*pending)->next;
        // now *pending is where to put the new message
        *pending = msg;
        O2_DBo(printf("%s blocked message %p queued for fds_info %p (%s) "
                      "socket %ld fds_info %p msg %p\n", o2_debug_prefix, 
                      msg, this,
                      Fds_info::tag_to_string(net_tag),
                      (long) o2n_fds[fds_index].fd, this, out_message));
    }
}
    

// remove messages (if any), but do not close. Called after error.
void Fds_info::reset()
{
    if (in_message) O2_FREE(in_message);
    in_message = NULL; // in case we're closed again
    while (out_message) {
        O2netmsg_ptr p = out_message;
        out_message = p->next;
        O2_FREE(p);
    }
    out_message = NULL;
}


// if now, then close socket immediately. If !now, which happens when we
// send an error response via HTTP (there may be other examples), then
// wait for the pending messages to be sent; then close the socket
// if read_type is READ_CUSTOM, we do not actually close the socket
void Fds_info::close_socket(bool now)
{
    reset();
    struct pollfd *pfd = &o2n_fds[fds_index];
    SOCKET sock = pfd->fd;
    if ((o2_debug & (O2_DBc_FLAG | O2_DBo_FLAG)) ||
        TRACE_SOCKET(this)) {
        if (owner) {
            Proxy_info *proxy = (Proxy_info *) owner;
            proxy->co_info(this, "closing socket");
        } else {
            printf("%s close_socket called on fds_info %p (%s) socket %ld\n",
                   o2_debug_prefix, this, Fds_info::tag_to_string(net_tag),
                   (long) sock);
        }
    }
    // a custom (e.g. ZeroConf) connection, the owner closes the socket
    if  (read_type == READ_CUSTOM) {
        owner->remove();
        owner = NULL;
    } else if (sock != INVALID_SOCKET) { // check in case we're closed again
        if ((net_tag & (NET_TCP_CLIENT | NET_TCP_CONNECTION)) && !now) {
            delete_me = 1;
            pfd->events |= POLLOUT;
            return; // wait for socket to be writeable
        } else {
            #ifdef SHUT_WR
                shutdown(sock, SHUT_WR);
            #endif
            o2_closesocket(sock, "o2n_close_socket");
        }
    }
    delete_me = 2;
    pfd->fd = INVALID_SOCKET;
    net_tag = NET_INFO_CLOSED;
    o2n_socket_delete_flag = true;
}


static bool in_o2n_recv = false;
    
#ifdef WIN32

FD_SET o2_read_set;
FD_SET o2_write_set;
FD_SET o2_except_set;
struct timeval o2_no_timeout;

static void report_error(const char *msg, SOCKET socket)
{
    int err;
    int errlen = sizeof(err);
    getsockopt(socket, SOL_SOCKET, SO_ERROR, (char *) &err, &errlen);
    O2_DBo(printf("%s Socket %ld error %s: %d", o2_debug_prefix,
                   (long) socket, msg, err));
}
  

O2err o2n_recv()
{
    if (in_o2n_recv) return O2_ALREADY_RUNNING; // reentrant call
    in_o2n_recv = true;
    // if there are any bad socket descriptions, remove them now
    if (o2n_socket_delete_flag) o2n_free_deleted_sockets();
    
    int total;
    
    FD_ZERO(&o2_read_set);
    FD_ZERO(&o2_write_set);
    FD_ZERO(&o2_except_set);
    // careful here! o2n_fds_info can grow if we accept a connection
    // and the FD_SETs will be invalid with respect to a new socket
    int socket_count = o2n_fds_info.size();
    for (int i = 0; i < socket_count; i++) {
        SOCKET fd = o2n_fds[i].fd;
        FD_SET(fd, &o2_read_set);
        Fds_info *fi = o2n_fds_info[i];
        if (fi->out_message || fi->delete_me == 1) {
            FD_SET(fd, &o2_write_set);
        }
        FD_SET(fd, &o2_except_set);
    }
    o2_no_timeout.tv_sec = 0;
    o2_no_timeout.tv_usec = 0;
    if ((total = select(0, &o2_read_set, &o2_write_set, &o2_except_set, 
                        &o2_no_timeout)) == SOCKET_ERROR) {
        int err = WSAGetLastError();
        print_socket_error(err, "o2n_recv");
        in_o2n_recv = false;
        return O2_SOCKET_ERROR;
    }
    if (total == 0) { /* no messages waiting */
        in_o2n_recv = false;
        return O2_SUCCESS;
    }
    for (int i = 0; i < socket_count; i++) {
        struct pollfd *pfd = &o2n_fds[i];
        
        if (FD_ISSET(pfd->fd, &o2_except_set)) {
            Fds_info *fi = o2n_fds_info[i];
            report_error("generated exception event", pfd->fd);
            fi->close_socket(true);
        } else {
            if (FD_ISSET(pfd->fd, &o2_read_set)) {
                Fds_info *fi = o2n_fds_info[i];
                if (fi->read_event_handler()) {
                    report_error("reported by read_event_handler", pfd->fd);
                    fi->close_socket(true);
                }
            }
            if (FD_ISSET(pfd->fd, &o2_write_set)) {
                Fds_info *fi = o2n_fds_info[i];
                if (fi->net_tag & NET_TCP_CONNECTING) { // connect completed
                    fi->net_tag = NET_TCP_CLIENT;
                    O2_DBo(printf("%s connection completed, socket %ld index "
                                  "%d\n", o2_debug_prefix, (long)pfd->fd, i));
                    // tell next layer up that connection is good, e.g. O2 sends
                    // notification that a new process is connected
                    if (fi->owner) fi->owner->connected();
                }
                if (fi->delete_me == 1) {
                    fi->delete_me = 2;
                    #ifdef SHUT_WR
                        shutdown(sock, SHUT_WR);
                    #endif
                    o2_closesocket(pfd->fd, "o2n_close_socket");
                    pfd->fd = INVALID_SOCKET;
                    fi->net_tag = NET_INFO_CLOSED;
                    o2n_socket_delete_flag = true;
                } else if (fi->out_message) {
                    O2err rslt = fi->send(false);
                }
            }
        }
        if (!o2_ensemble_name) { // handler called o2_finish()
            // o2n_fds are all freed and gone
            in_o2n_recv = false;
            return O2_FAIL;
        }
    }
    // clean up any dead sockets before user has a chance to do anything
    // (actually, user handlers could have done a lot, so maybe this is
    // not strictly necessary.)
    if (o2n_socket_delete_flag) o2n_free_deleted_sockets();
    in_o2n_recv = false;
    return O2_SUCCESS;
}

#else  // Use poll function to receive messages.

O2err o2n_recv()
{
    int i;
    if (in_o2n_recv) return O2_ALREADY_RUNNING; // reentrant call
    in_o2n_recv = true;
    // if there are any bad socket descriptions, remove them now
    if (o2n_socket_delete_flag) o2n_free_deleted_sockets();

    poll(&o2n_fds[0], o2n_fds.size(), 0);
    int len = o2n_fds.size(); // length can grow while we're looping!
    for (i = 0; i < len; i++) {
        Fds_info *fi;
        struct pollfd *pfd = &o2n_fds[i];
        // if (pfd->revents) printf("%d:%p:%04x ", i, d, d->revents);
        if (pfd->revents & POLLERR) {
        } else if (pfd->revents & POLLHUP) {
            fi = o2n_fds_info[i];
            if ((o2_debug & O2_DBo_FLAG) ||
                TRACE_SOCKET(fi)) {
                printf("%s removing remote process after POLLHUP to "
                       "socket %ld index %d\n", o2_debug_prefix,
                       (long) (pfd->fd), i);
            }
            fi->close_socket(true);
        // do this first so we can change PROCESS_CONNECTING to
        // PROCESS_CONNECTED when socket becomes writable
        } else if (pfd->revents & POLLOUT) {
            fi = o2n_fds_info[i]; // find socket info
            if (fi->net_tag & NET_TCP_CONNECTING) { // connect() completed
                fi->net_tag = NET_TCP_CLIENT;
                O2_DBo(printf("%s connection completed, socket %ld index %d\n",
                              o2_debug_prefix, (long) (pfd->fd), i));
                // tell next layer up that connection is good, e.g. O2 sends
                // notification that a new process is connected
                if (fi->owner) fi->owner->connected();
            }
            // now we have a completed connection and events has POLLOUT
            if (fi->owner && fi->write_type == WRITE_CUSTOM) {
                fi->owner->writeable();
            } else if (fi->delete_me == 1) {
                fi->delete_me = 2;
                #ifdef SHUT_WR
                    shutdown(pfd->fd, SHUT_WR);
                #endif
                o2_closesocket(pfd->fd, "o2n_close_socket");
                pfd->fd = INVALID_SOCKET;
                fi->net_tag = NET_INFO_CLOSED;
                o2n_socket_delete_flag = true;
            } else if (fi->out_message) {
                O2err rslt = fi->send(false);
                if (rslt == O2_SUCCESS) {
                    pfd->events &= ~POLLOUT;
                }
            } else { // no message to send, clear polling
                pfd->events &= ~POLLOUT;
            }
        } else if (pfd->revents & POLLIN) {
            fi = o2n_fds_info[i];
            /*
            if (fi->net_tag == NET_INFILE) {
                printf("got file info\n");
            }
             */
            if (fi->read_event_handler()) {
                O2_DBo(printf("%s removing remote process after handler "
                              "reported error on socket %ld", o2_debug_prefix, 
                              (long) (pfd->fd)));
                fi->close_socket(true);
            }
        }
        if (!o2_ensemble_name) { // handler called o2_finish()
            // o2n_fds are all free and gone now
            in_o2n_recv = false;
            return O2_FAIL;
        }
    }
    // clean up any dead sockets before user has a chance to do anything
    // (actually, user handlers could have done a lot, so maybe this is
    // not strictly necessary.)
    if (o2n_socket_delete_flag) o2n_free_deleted_sockets();
    in_o2n_recv = false;
    return O2_SUCCESS;
}
#endif


/******* handlers for socket events *********/

// clean up interf to prepare for next message
//
void Fds_info::message_cleanup()
{
    in_message = NULL;
    in_msg_got = 0;
    in_length = 0;
    in_length_got = 0;
}


// returns O2_SUCCESS if whole message is read.
//         O2_FAIL if whole message is not read yet.
//         O2_TCP_HUP if socket is closed
//
O2err Fds_info::read_whole_message(SOCKET sock)
{
    int n;
    if (read_type == READ_RAW) {
        // allow raw messages up to 512 bytes
        assert(net_tag & NET_TCP_MASK);
        in_message = O2N_MESSAGE_ALLOC(512);
        n = (int) recvfrom(sock, in_message->payload, 512, 0, NULL, NULL);
        O2_DBw(printf("%s READ_RAW read %d bytes\n", o2_debug_prefix, n));
        if (n < 0) {
            goto error_exit;
        }
        in_message->length = n;
    } else if (read_type == READ_O2) {
        /* first read length if it has not been read yet */
        if (in_length_got < 4) {
            // coerce to int to avoid compiler warning; requested length is
            // int, so int is ok for n
            n = (int) recvfrom(sock,
                               PTR(&in_length) + in_length_got,
                               4 - in_length_got, 0, NULL, NULL);
            if (n <= 0) {
                goto error_exit;
            }
            in_length_got += n;
            assert(in_length_got < 5);
            if (in_length_got < 4) {
                return O2_FAIL; // length is not received yet, get more later
            }
            // done receiving length bytes
            in_length = htonl(in_length);
            assert(!in_message);
            // if someone grabs our IP and port from Bonjour and sends a
            // random message or even visits the URL with a browser, the
            // incoming message could appear to have a large length count
            // that could crash O2. We do not have much security, but at
            // least we can shut down the connection when we get an
            // implausible message length.
            if (in_length >= 0x10000) {
                O2_DBo(printf("bad message length in read_whole_message; "
                              "closing connection\n"));
                n = 0;  // means the socket to be closed, no message to free
                goto error_exit;
            }
            in_message = O2netmsg_new(in_length);
            in_msg_got = 0; // just to make sure
        }
        
        /* read the full message */
        if (in_msg_got < in_length) {
            // coerce to int to avoid compiler warning; will not overflow
            n = (int) recvfrom(sock,
                          in_message->payload + in_msg_got,
                          in_length - in_msg_got, 0, NULL, NULL);
            if (n <= 0) {
                goto error_exit;
            }
            in_msg_got += n;
            if (in_msg_got < in_length) {
                return O2_FAIL; // message is not complete, get more later
            }
        }
        in_message->length = in_length;
    } // else READ_CUSTOM -- do not read here, in_message is NULL
    return O2_SUCCESS; // we have a full message now
  error_exit:
    if (n == 0) { /* socket was gracefully closed */
        O2_DBo(printf("recvfrom returned 0: deleting socket\n"));
        message_cleanup();
        return O2_TCP_HUP;
    } else if (n < 0) { /* error: close the socket */
        if (TERMINATING_SOCKET_ERROR) {
            perror("recvfrom in read_whole_message");
            if (in_message) {
                O2_FREE(in_message);
            }
            message_cleanup();
            return O2_TCP_HUP;
        }
    }
    return O2_FAIL; // not finished reading
}


int Fds_info::read_event_handler()
{
    SOCKET sock = o2n_fds[fds_index].fd;
    if (net_tag & (NET_TCP_CONNECTION | NET_TCP_CLIENT | NET_INFILE)) {
        int n = read_whole_message(sock);
        if (n == O2_FAIL) { // not ready to process message yet
            return O2_SUCCESS; // not a problem, but we're done for now
        } else if (n != O2_SUCCESS) {
            return n; // some other error, i.e. O2_TCP_HUP
        }
        // fall through and send message
    } else if (net_tag == NET_UDP_SERVER) {
#ifdef WIN32
        u_long len; // CAREFUL! This type not unix compatible!
#else
        int len;
#endif
        if (ioctlsocket(sock, FIONREAD, &len) == -1) {
            perror("udp_recv_handler");
            return O2_FAIL;
        }
#ifdef OLD_DEBUG_CODE
#ifndef O2_NO_DEBUG
        // make sure owner is not associated with some other socket --
        // sometimes, in_message is not NULL, but it should be. How
        // does it get set? Aliasing? Probably not - we failed the
        // assert(!in_message) below, so we got past this loop.
        for (int j = 0; j < o2n_fds_info.size(); j++) {
            if (j != fds_index && o2n_fds_info[j].owner == owner) {
                printf("ALIAS!!!!!! %d %d\n", j, fds_index);
                assert(false);
            }
        }
        if (in_message) {
            printf("in_message, len %d:\n", in_message->length);
            o2_node_show((O2node *) owner, 2);
            printf("    message is ");
            o2_message_print((O2message_ptr) in_message);
        }
#endif
#endif
        assert(!in_message);
        in_message = O2netmsg_new(len);
        if (!in_message) return O2_FAIL;
        int n;
        // coerce to int to avoid compiler warning; ok because len is int
        if ((n = (int) recvfrom(sock, (char *) &in_message->payload, len,
                                0, NULL, NULL)) <= 0) {
            // I think udp errors should be ignored. UDP is not reliable
            // anyway. For now, though, let's at least print errors.
            perror("recvfrom in udp_recv_handler");
            O2_FREE(in_message);
            in_message = NULL;
            return O2_FAIL;
        }
#if CLOSE_SOCKET_DEBUG
        printf("***UDP received %d bytes at %g.\n", n, o2_local_time());
#endif
        in_message->length = n;
        // fall through and send message
    } else if (net_tag == NET_TCP_SERVER) {
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
        Fds_info *conn = new Fds_info(connection, NET_TCP_CONNECTION, 0, NULL);
        O2_DBdo(printf("%s O2 server socket %ld accepts client as socket "
                       "%ld index %d\n", o2_debug_prefix, (long) sock,
                       (long) connection, conn->fds_index));
        assert(conn);
        if (owner) owner->accepted(conn);
        else conn->close_socket(true);  // not sure if this could happen
        return O2_SUCCESS;
    } else {  // socket has a read error, but this could be our local proc
        // TCP server socket, so don't close it; just clean up.
        reset();
        return O2_SUCCESS;  // any error returned will close socket, so don't
    }
    // COMMON CODE for TCP and UDP receive message:
    // endian corrections are done in handler
    O2netmsg_ptr msg = in_message;
    message_cleanup();  // get ready for next incoming message
    O2err err = O2_FAIL;
    O2_DBo(printf("%s delivering message from net_tag %s socket %ld index %d "
                  "to %p\n", o2_debug_prefix, tag_to_string(net_tag),
                  (long) sock, fds_index, owner));
    if (owner && !delete_me) {
        // note that for READ_CUSTOM (e.g. asynchronous file read), msg is NULL
        err = owner->deliver(msg);
    } else {
        O2_FREE(msg);
    }
    if (err != O2_SUCCESS &&
        (net_tag == NET_TCP_CONNECTING ||
         net_tag == NET_TCP_CLIENT ||
         net_tag == NET_TCP_CONNECTION)) {
         close_socket(true);
    }
    return O2_SUCCESS;
}

#ifndef O2_NO_DEBUG

const char *Fds_info::tag_to_string(int tag)
{
    switch (tag) {
        case NET_UDP_SERVER: return "NET_UDP_SERVER"; 
        case NET_TCP_SERVER: return "NET_TCP_SERVER"; 
        case NET_TCP_CONNECTING: return "NET_TCP_CONNECTING"; 
        case NET_TCP_CLIENT: return "NET_TCP_CLIENT"; 
        case NET_TCP_CONNECTION: return "NET_TCP_CONNECTION"; 
        case NET_INFO_CLOSED: return "NET_INFO_CLOSED";
        case NET_INFILE: return "NET_INFILE";
        default: /* fall through */ ;
    }
    static char unknown[32];
    snprintf(unknown, 32, "Tag-%d(%x)", tag, tag);
    return unknown;
}
#endif
    
SOCKET Fds_info::get_socket()
{
    return o2n_fds[fds_index].fd;
}


short Fds_info::get_events()
{
    return o2n_fds[fds_index].revents;
}


void Fds_info::set_events(short events)
{
    o2n_fds[fds_index].events = events;
}
