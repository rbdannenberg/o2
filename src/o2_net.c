// o2_net.c -- implementation of network communication
//
// Roger B. Dannenberg, 2019
//

#include <fcntl.h>
#include <sys/socket.h>
#include <ctype.h>
#include "o2.h"
#include "o2_internal.h"
#include "o2_message.h"
#include "o2_send.h"

#ifdef WIN32
#include <stdio.h> 
#include <stdlib.h> 
#include <windows.h>
#include <errno.h>
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
#include "sys/ioctl.h"
#include <ifaddrs.h>
#define TERMINATING_SOCKET_ERROR \
    (errno != EAGAIN && errno != EINTR)
#endif

char o2_local_ip[24];
int o2_local_tcp_port = 0;
// we have not been able to connect to network
// and (so far) we only talk to 127.0.0.1 (localhost)
int o2_found_network = FALSE;


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

int o2n_socket_delete_flag = FALSE;

o2n_info_ptr o2_message_source = NULL; ///< socket info for current message

// this indirection is used so that testing code can grab incoming messages
// directly from this o2_net level of abstraction, so the full
// o2_initialize() can be skipped.
int (*o2n_send_by_tcp)(o2n_info_ptr info);

// create a UDP send socket for broadcast or general sends
//
int o2n_udp_send_socket_new(SOCKET *sock)
{
    if ((*sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
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
                   (void *) &set, sizeof(int)) < 0) {
        perror("in setsockopt in o2_disable_sigpipe");
    }
#endif    
}


static int bind_recv_socket(SOCKET sock, int *port, int tcp_recv_flag)
{
    memset(PTR(&o2_serv_addr), 0, sizeof(o2_serv_addr));
    o2_serv_addr.sin_family = AF_INET;
    o2_serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // local IP address
    o2_serv_addr.sin_port = htons(*port);
    unsigned int yes = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                   PTR(&yes), sizeof(yes)) < 0) {
        perror("setsockopt(SO_REUSEADDR)");
        return O2_FAIL;
    }
    if (bind(sock, (struct sockaddr *) &o2_serv_addr, sizeof(o2_serv_addr))) {
        if (tcp_recv_flag) perror("Bind receive socket");
        return O2_FAIL;
    }
    if (*port == 0) { // find the port that was (possibly) allocated
        socklen_t addr_len = sizeof(o2_serv_addr);
        if (getsockname(sock, (struct sockaddr *) &o2_serv_addr,
                        &addr_len)) {
            perror("getsockname call to get port number");
            return O2_FAIL;
        }
        *port = ntohs(o2_serv_addr.sin_port);  // set actual port used
    }
    // printf("*   %s: bind socket %d port %d\n", o2_debug_prefix, sock, *port);
    assert(*port != 0);
    return O2_SUCCESS;
}


// add a new socket to the fds and fds_info arrays,
// on success, o2n_info_ptr descriptor is initialized and
// the proc.uses_hub is set to O2_NO_HUB
//
static o2n_info_ptr socket_info_new(SOCKET sock, int tag, int net_tag)
{
    // expand socket arrays for new port
    o2n_info_ptr info = O2_CALLOC(1, sizeof(o2n_info)); // create info struct
    memset(info, 0, sizeof(o2n_info)); // initialize by setting all to zero
    // info->delete_me = FALSE;
    // info->port = 0;
    DA_EXPAND(o2_context->fds_info, o2n_info_ptr); // make room in fds arrays
    DA_EXPAND(o2_context->fds, struct pollfd);
    // set last fds_info to info:
    *DA_LAST(o2_context->fds_info, o2n_info_ptr) = info;
    //printf("new info %p\n", *DA_LAST(o2_context->fds_info, o2n_info_ptr));
    info->tag = tag;
    info->net_tag = net_tag;
    info->fds_index = o2_context->fds.length - 1; // last element
    DA_INIT(info->proc.services, o2string, 0);

    struct pollfd *pfd = DA_LAST(o2_context->fds, struct pollfd);
    pfd->fd = sock;
    pfd->events = POLLIN;
    pfd->revents = 0;
    return info;
}


int o2n_udp_recv_socket_new(int tag, int *port)
{
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        return NULL;
    }
    // Bind the socket
    int err;
    if ((err = bind_recv_socket(sock, port, FALSE))) {
        closesocket(sock);
        return O2_FAIL;
    }
    o2n_info_ptr info = socket_info_new(sock, tag, NET_UDP_SOCKET);
    assert(info);
    O2_DBo(printf("%s created socket %ld index %d and bind to port %d to receive UDP\n",
                  o2_debug_prefix, (long) sock, info->fds_index, *port));
    info->port = *port;
    // printf("%s: o2n_udp_recv_socket_new: listening on port %d\n",
    //        o2_debug_prefix, o2_context->info.port);
    return O2_SUCCESS;
}


static void set_nodelay_option(SOCKET sock)
{
    int option = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *) &option,
               sizeof(option));
}


int o2n_tcp_server_new(int tag, int *port)
{
    if (o2n_tcp_socket_new(tag, NET_TCP_SERVER, 0) != O2_SUCCESS) {
        return NULL;
    }
    struct pollfd *pfd = DA_LAST(o2_context->fds, struct pollfd);
    int sock = pfd->fd;
    // bind server port
    RETURN_IF_ERROR(bind_recv_socket(sock, port, TRUE));
    RETURN_IF_ERROR(listen(sock, 10));
    O2_DBo(printf("%s bind and listen called on socket %ld\n",
                  o2_debug_prefix, (long) sock));
    return O2_SUCCESS;
}


int o2n_broadcast_socket_new(SOCKET *sock)
{
    // Set up a socket for broadcasting discovery info
    RETURN_IF_ERROR(o2n_udp_send_socket_new(sock));
    // Set the socket's option to broadcast
    int optval = TRUE;
    if (setsockopt(*sock, SOL_SOCKET, SO_BROADCAST,
                   (const char *) &optval, sizeof(int)) == -1) {
        perror("Set socket to broadcast");
        return O2_FAIL;
    }
    return O2_SUCCESS;
}


// this is really a higher-level protocol function because the name
// is only needed by O2, not by low-level communication functions,
// but since it takes low-level digging around to get the IP address
// and construct the local host's name for O2, we put the code here
// among all the other low-level network code
//
void set_local_process_name(o2n_info_ptr info)
{
    struct ifaddrs *ifap, *ifa;
    char name[32]; // "100.100.100.100:65000" -> 21 chars
    name[0] = 0;   // initially empty
    struct sockaddr_in *sa;
    // look for AF_INET interface. If you find one, copy it
    // to name. If you find one that is not 127.0.0.1, then
    // stop looking.
        
    if (getifaddrs(&ifap)) {
        perror("getting IP address");
        return;
    }
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr->sa_family==AF_INET) {
            sa = (struct sockaddr_in *) ifa->ifa_addr;
            if (!inet_ntop(AF_INET, &sa->sin_addr, o2_local_ip,
                           sizeof(o2_local_ip))) {
                perror("converting local ip to string");
                break;
            }
            sprintf(name, "%s:%d", o2_local_ip, info->port);
            if (!streql(o2_local_ip, "127.0.0.1")) {
                assert(info->port == o2_local_tcp_port);
                o2_found_network = TRUE;
                break;
            }
        }
    }
    freeifaddrs(ifap);
    info->proc.name = o2_heapify(name);
}


// initialize this module
// - create UDP broadcast socket
// - create UDP send socket
// - create UDP recv socket
// - create TCP server socket
int o2n_initialize()
{
    int err;
#ifdef WIN32
    // Initialize (in Windows)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif // WIN32

    // Initialize addr for broadcasting
    o2n_broadcast_to_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, "255.255.255.255",
                  &(o2n_broadcast_to_addr.sin_addr.s_addr)) != 1) {
        return O2_FAIL;
    }
    // note: returning an error will result in o2_initialize calling
    // o2_finish, which calls o2n_finish, so all is properly shut down
    RETURN_IF_ERROR(o2n_broadcast_socket_new(&o2n_broadcast_sock));

    // Initialize addr for local sending
    local_to_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, "127.0.0.1",
                  &(local_to_addr.sin_addr.s_addr)) != 1) {
        return O2_FAIL;
    }
    if ((err = o2n_udp_send_socket_new(&o2n_udp_send_sock))) {
        o2n_finish();
        return err;
    }

    DA_INIT(o2_context->fds, struct pollfd, 5);
    DA_INIT(o2_context->fds_info, o2n_info_ptr, 5);

    RETURN_IF_ERROR(o2n_tcp_server_new(INFO_TCP_SERVER, &o2_local_tcp_port));
    o2_context->info = *DA_LAST(o2_context->fds_info, o2n_info_ptr);
    o2_context->info->port = o2_local_tcp_port;
    // note that there might not be a network connection here. We can
    // still use O2 locally without an IP address.

    set_local_process_name(o2_context->info);
    o2n_send_by_tcp = &o2_message_deliver;
    return O2_SUCCESS;
}


// cleanup and prepare to exit module
//
void o2n_finish()
{
    // o2_context->info has been freed
    // local process name was removed as part of tcp server removal
    // tcp server socket was removed already by o2_finish
    // udp receive socket was removed already by o2_finish
    DA_FINISH(o2_context->fds_info);
    DA_FINISH(o2_context->fds);
    if (o2n_udp_send_sock != INVALID_SOCKET) {
        closesocket(o2n_udp_send_sock);
        o2n_udp_send_sock = INVALID_SOCKET;
    }
    if (o2n_broadcast_sock != INVALID_SOCKET) {
        closesocket(o2n_broadcast_sock);
        o2n_broadcast_sock = INVALID_SOCKET;
    }
#ifdef WIN32
    WSACleanup();    
#endif
}


int o2n_tcp_socket_new(int tag, int net_tag, int port)
{
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        printf("tcp socket creation error");
        return O2_FAIL;
    }
    // make the socket non-blocking
    fcntl(sock, F_SETFL, O_NONBLOCK);

    o2n_info_ptr info = socket_info_new(sock, tag, net_tag);
    assert(info);
    O2_DBo(printf("%s created tcp socket %ld index %d tag %s\n",
                  o2_debug_prefix, (long) sock, info->fds_index, o2_tag_to_string(tag)));
    // a "normal" TCP connection: set NODELAY option
    // (NODELAY means that TCP messages will be delivered immediately
    // rather than waiting a short period for additional data to be
    // sent. Waiting might allow the outgoing packet to consolidate 
    // sent data, resulting in greater throughput, but more latency.
    set_nodelay_option(sock);

    info->port = port;
    return O2_SUCCESS;
}


void o2n_socket_mark_to_free(o2n_info_ptr info)
{
    info->delete_me = TRUE;
    o2n_socket_delete_flag = TRUE;
}


// remove the i'th socket from o2_context->fds and o2_context->fds_info
//
void o2_socket_remove(int i)
{
    struct pollfd *pfd = DA_GET(o2_context->fds, struct pollfd, i);

    O2_DBo(printf("%s o2_socket_remove: tag %d port %d closing socket %lld index %d\n",
                  o2_debug_prefix, GET_PROCESS(i)->tag, GET_PROCESS(i)->port,
                  (long long) pfd->fd, i));
    SOCKET sock = pfd->fd;
#ifdef SHUT_WR
    shutdown(sock, SHUT_WR);
#endif
    O2_DBo(printf("calling closesocket(%lld).\n", (int64_t) (pfd->fd)));
    if (closesocket(pfd->fd)) perror("closing socket");
    if (o2_context->fds.length > i + 1) { // move last to i
        struct pollfd *lastfd = DA_LAST(o2_context->fds, struct pollfd);
        memcpy(pfd, lastfd, sizeof(struct pollfd));
        o2n_info_ptr info = *DA_LAST(o2_context->fds_info, o2n_info_ptr);
        GET_PROCESS(i) = info; // move to new index
        info->fds_index = i;
    }
    o2_context->fds.length--;
    o2_context->fds_info.length--;
}


// assumes that if delete_me is set, the info structure has already been 
// cleaned up so that it no longer points to any heap structures and it
// is now safe to free the info structure itself.
//
void o2n_free_deleted_sockets()
{
    for (int i = 0; i < o2_context->fds_info.length; i++) {
        o2n_info_ptr info = GET_PROCESS(i);
        if (info->delete_me) {
            o2_socket_remove(i);
            O2_FREE(info);
            i--;
        }
    }
    o2n_socket_delete_flag = FALSE;
}


// create a TCP connection to a server
//
int o2n_connect(const char *ip, int tcp_port, int tag)
{
    struct sockaddr_in remote_addr;
    //set up the sockaddr_in
#ifndef WIN32
    bzero(&remote_addr, sizeof(remote_addr));
#endif
    RETURN_IF_ERROR(o2n_tcp_socket_new(INFO_TCP_NOCLOCK, NET_TCP_CONNECTING, 0));
    // set up the connection
    remote_addr.sin_family = AF_INET;      //AF_INET means using IPv4
    inet_pton(AF_INET, ip, &(remote_addr.sin_addr));
    remote_addr.sin_port = htons(tcp_port);

    // note: our local port number is not recorded, not needed
    // get the socket just created by o2n_tcp_socket_new
    struct pollfd *pfd = DA_LAST(o2_context->fds, struct pollfd);
    SOCKET sock = pfd->fd;

    O2_DBo(printf("%s connect to %s:%d with socket %ld index %d\n",
                  o2_debug_prefix, ip, tcp_port, (long) sock, o2_context->fds.length - 1));
    if (connect(sock, (struct sockaddr *) &remote_addr,
                sizeof(remote_addr)) == -1) {
        if (errno != EINPROGRESS) {
            perror("Connect Error!\n");
            o2_context->fds_info.length--;   // restore socket arrays
            o2_context->fds.length--;
            return O2_FAIL;
        }
        // detect when we're connected by polling for writable
        pfd->events |= POLLOUT;
    } else { // wow, we're already connected, not sure this is possible
        (*DA_LAST(o2_context->fds_info, o2n_info_ptr))->net_tag = NET_TCP_CLIENT;
        o2_disable_sigpipe(sock);
        O2_DBd(printf("%s connected to %s:%d index %d\n",
                      o2_debug_prefix, ip, tcp_port, o2_context->fds.length - 1));
    }
    return O2_SUCCESS;
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
int o2n_send(o2n_info_ptr info, int block)
{
    int err;
    int flags = 0;
#ifndef __APPLE__
    flags = MSG_NOSIGNAL;
#endif
    if (info->net_tag == NET_TCP_CONNECTING) {
        assert(!block); // should never block if we haven't even connected
        printf("o2n_send - index %d tag is NET_TCP_CONNECTING, so we wait\n", info->fds_index);
        // we need to wait until connected before we can send
        return O2_SUCCESS;
    }
    if (!block) {
        flags |= MSG_DONTWAIT;
    }
    struct pollfd *pfd = DA_GET(o2_context->fds, struct pollfd, 
                                info->fds_index);
    o2_message_ptr msg;
    while ((msg = info->out_message)) { // more messages to send
        // Send the length of the message followed by the message.
        // We want to do this in one send; otherwise, we'll send 2
        // network packets due to the NODELAY socket option.
        int32_t len = msg->length;
        msg->length = htonl(len);
        char *from = ((char *) &(msg->length)) + info->out_msg_sent;
        int n = len + sizeof(int32_t) - info->out_msg_sent;
        printf("***** o2n_send calls send(index %d, %s)\n", info->fds_index, msg->data.address);
        err = send(pfd->fd, from, n, flags);
        msg->length = len; // restore byte-swapped len

        if (err < 0) {
            if (!block && !TERMINATING_SOCKET_ERROR) {
                printf("setting POLLOUT on %d\n", info->fds_index);
                pfd->events |= POLLOUT; // request event when it unblocks
                return O2_BLOCKED;
            } else if (errno != EINTR && errno != EAGAIN) {
                O2_DBo(printf("%s removing remote process after send error "
                           "%d to socket %ld index %d\n", o2_debug_prefix,
                            errno, (long) (pfd->fd), info->fds_index));
                o2_message_free(msg);
                o2_info_remove(info);
                return O2_FAIL;
            } // else EINTR or EAGAIN, so try again
        } else {
            // err >= 0, update how much we have sent
            info->out_msg_sent += err;
            if (err >= n) { // finished sending message
                assert(info->out_msg_sent == len + sizeof(int32_t));
                info->out_msg_sent = 0;
                o2_message_ptr next = msg->next;
                o2_message_free(msg);
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
// Follow this call with o2_net_send(info, msg, TRUE) to force a blocking
// (synchronous) send.
//
int o2n_enqueue(o2n_info_ptr info, o2_message_ptr msg)
{
    // if nothing pending yet, no send in progress;
    //    set up to send this message
    o2_msg_data_ptr mdp = &(msg->data);
    msg->next = NULL; // make sure this will be the end of list
    if (!info->out_message) {
        O2_DBs(if (mdp->address[1] != '_' && !isdigit(mdp->address[1]))
                   o2_dbg_msg("sending TCP", mdp, "to", info->proc.name));
        O2_DBS(if (mdp->address[1] == '_' || isdigit(mdp->address[1]))
                   o2_dbg_msg("sending TCP", mdp, "to", info->proc.name));
#if IS_LITTLE_ENDIAN
        o2_msg_swap_endian(mdp, TRUE);
#endif
        info->out_message = msg;
        info->out_msg_sent = 0;
        o2n_send(info, FALSE);
    } else {
        // insert message at end of queue; normally queue is empty
        o2_message_ptr *pending = &(info->out_message);
        while (*pending) pending = &((*pending)->next);
        // now *pending is where to put the new message
        O2_DBs(if (mdp->address[1] != '_' && !isdigit(mdp->address[1]))
                   o2_dbg_msg("queueing TCP", mdp, "to", info->proc.name));
        O2_DBS(if (mdp->address[1] == '_' || isdigit(mdp->address[1]))
                   o2_dbg_msg("queueing TCP", mdp, "to", info->proc.name));
#if IS_LITTLE_ENDIAN
        o2_msg_swap_endian(mdp, TRUE);
#endif
        *pending = msg;
    }
    return O2_SUCCESS;
}


void o2n_close_socket(o2n_info_ptr info)
{
    // (*info->close_handler)(info);
    if (info->in_message) O2_FREE(info->in_message);
    while (info->out_message) {
        o2_message_ptr p = info->out_message;
        info->out_message = p->next;
        O2_FREE(p);
    }
    info->delete_me = TRUE;
    o2n_socket_delete_flag = TRUE;
}


void o2n_local_udp_send(char *msg, int len, int port)
{
    local_to_addr.sin_port = port; // copy port number
    O2_DBd(printf("%s sending localhost msg to port %d\n",
                  o2_debug_prefix, ntohs(port)));
    if (sendto(o2n_udp_send_sock, msg, len, 0,
           (struct sockaddr *) &local_to_addr,
           sizeof(local_to_addr)) < 0) {
        perror("Error attempting to send udp message locally");
    }
}

#ifdef WIN32

FD_SET o2_read_set;
FD_SET o2_write_set;
struct timeval o2_no_timeout;

int o2n_recv()
{
    // if there are any bad socket descriptions, remove them now
    if (o2n_socket_delete_flag) o2n_free_deleted_sockets();
    
    int total;
    
    FD_ZERO(&o2_read_set);
    FD_ZERO(&o2_write_set);
    for (int i = 0; i < o2_context->fds.length; i++) {
        struct pollfd *d = DA_GET(o2_context->fds, struct pollfd, i);
        FD_SET(d->fd, &o2_read_set);
        o2n_info_ptr info = GET_PROCESS(i);
        if (info->tag == INFO_TCP_SOCKET && info->proc.pending_msg) {
            FD_SET(d->fd, &o2_write_set);
        }
    }
    o2_no_timeout.tv_sec = 0;
    o2_no_timeout.tv_usec = 0;
    if ((total = select(0, &o2_read_set, &o2_write_set, NULL, 
                        &o2_no_timeout)) == SOCKET_ERROR) {
        /* TODO: error handling here */
        return O2_FAIL; /* TODO: return a specific error code for this */
    }
    if (total == 0) { /* no messages waiting */
        return O2_SUCCESS;
    }
    for (int i = 0; i < o2_context->fds.length; i++) {
        struct pollfd *pfd = DA_GET(o2_context->fds, struct pollfd, i);
        if (FD_ISSET(pfd->fd, &o2_read_set)) {
            o2n_info_ptr info = GET_PROCESS(i);
            if ((read_event_handler(pfd->fd, info)) == O2_TCP_HUP) {
                O2_DBo(printf("%s removing remote process after O2_TCP_HUP to "
                              "socket %ld", o2_debug_prefix, (long) pfd->fd));
                o2n_close_socket(info);
            }
        }
        if (FD_ISSET(pfd->fd, &o2_write_set)) {
            o2n_info_ptr info = GET_PROCESS(i);
            o2_message_ptr msg = info->proc.pending_msg; // unlink the pending msg
            info->proc.pending_msg = NULL;
            int rslt = o2n_send(info, FALSE);
            assert(FALSE); // need to handle multiple queued messages
            if (rslt == O2_SUCCESS) {
                printf("clearing POLLOUT on %d\n", info->fds_index);
                pfd->events &= ~POLLOUT;
            }
        }            
        if (!o2_ensemble_name) { // handler called o2_finish()
            // o2_context->fds are all freed and gone
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

int o2n_recv()
{
    int i;
        
    // if there are any bad socket descriptions, remove them now
    if (o2n_socket_delete_flag) o2n_free_deleted_sockets();

    poll((struct pollfd *) o2_context->fds.array, o2_context->fds.length, 0);
    int len = o2_context->fds.length; // length can grow while we're looping!
    for (i = 0; i < len; i++) {
        o2n_info_ptr info;
        struct pollfd *pfd = DA_GET(o2_context->fds, struct pollfd, i);
        // if (d->revents) printf("%d:%p:%x ", i, d, d->revents);
        if (pfd->revents & POLLERR) {
        } else if (pfd->revents & POLLHUP) {
            info = GET_PROCESS(i);
            O2_DBo(printf("%s removing remote process after POLLHUP to "
                          "socket %ld index %d\n", o2_debug_prefix, (long) (pfd->fd),
                          i));
            o2n_close_socket(info);
        // do this first so we can change PROCESS_CONNECTING to PROCESS_CONNECTED
        // when socket becomes writable
        } else if (pfd->revents & POLLOUT) {
            info = GET_PROCESS(i); // find the process info
            printf("pollout for process %d %s\n", i, info->proc.name);
            if (info->net_tag == NET_TCP_CONNECTING) { // connect() completed
                info->net_tag = NET_TCP_CLIENT;
            }
            // now we have a completed connection and events has POLLOUT
            if (info->out_message) {
                int rslt = o2n_send(info, FALSE);
                if (rslt == O2_SUCCESS) {
                    printf("clearing POLLOUT on %d no more messages\n", info->fds_index);
                    pfd->events &= ~POLLOUT;
                }
            } else { // no message to send, clear polling
                printf("clearing POLLOUT because nothing to send %d?\n", i);
                pfd->events &= ~POLLOUT;
            }
        } else if (pfd->revents & POLLIN) {
            info = GET_PROCESS(i);
            assert(info->in_length_got < 5);
            if (read_event_handler(pfd->fd, info)) {
                O2_DBo(printf("%s removing remote process after handler "
                              "reported error on socket %ld", o2_debug_prefix, 
                              (long) (pfd->fd)));
                o2n_close_socket(info);
            }
        }
        if (!o2_ensemble_name) { // handler called o2_finish()
            // o2_context->fds are all free and gone now
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
static int read_whole_message(SOCKET sock, o2n_info_ptr info)
{
    assert(info->in_length_got < 5);
    /* first read length if it has not been read yet */
    if (info->in_length_got < 4) {
        // coerce to int to avoid compiler warning; requested length is
        // int, so int is ok for n
        int n = (int) recvfrom(sock, PTR(&(info->in_length)) + info->in_length_got,
                               4 - info->in_length_got, 0, NULL, NULL);
        if (n == 0) { /* socket was gracefully closed */
            O2_DBo(printf("recvfrom returned 0: deleting socket\n"));
            info_message_cleanup(info);
            return O2_TCP_HUP;
        } else if (n < 0) { /* error: close the socket */
            if (TERMINATING_SOCKET_ERROR) {
                perror("recvfrom in read_whole_message getting length");
                info_message_cleanup(info);
                return O2_TCP_HUP;
            }
            return O2_FAIL; // not finished reading
        }
        info->in_length_got += n;
        assert(info->in_length_got < 5);
        if (info->in_length_got < 4) {
            return O2_FAIL; // length is not received yet, get more later
        }
        // done receiving length bytes
        info->in_length = htonl(info->in_length);
        info->in_message = o2_alloc_size_message(info->in_length);
        info->in_msg_got = 0; // just to make sure
    }

    /* read the full message */
    if (info->in_msg_got < info->in_length) {
        // coerce to int to avoid compiler warning; message length is int, so n can be int
        int n = (int) recvfrom(sock,
                               PTR(&(info->in_message->data)) + info->in_msg_got,
                               info->in_length - info->in_msg_got, 0, NULL, NULL);
        if (n == 0) { /* socket was gracefully closed */
            O2_DBo(printf("recvfrom returned 0: deleting socket\n"));
            info_message_cleanup(info);
            return O2_TCP_HUP;
        } else if (n <= 0) {
            if (TERMINATING_SOCKET_ERROR) {
                perror("recvfrom in read_whole_message getting data");
                o2_message_free(info->in_message);
                info_message_cleanup(info);
                return O2_TCP_HUP;
            }
            return O2_FAIL; // not finished reading
        }
        info->in_msg_got += n;
        if (info->in_msg_got < info->in_length) {
            return O2_FAIL; // whole message is not read in yet, get more later
        }
    }
    info->in_message->length = info->in_length;
    return O2_SUCCESS; // we have a full message now
}


static int read_event_handler(SOCKET sock, o2n_info_ptr info)
{
    if (info->net_tag == NET_TCP_CONNECTION || info->net_tag == NET_TCP_CLIENT) {
        int n = read_whole_message(sock, info);
        if (n == O2_FAIL) { // not ready to process message yet
            return O2_SUCCESS; // not a problem, but we're done for now
        } else if (n != O2_SUCCESS) {
            return n; // some other error, i.e. O2_TCP_HUP
        }
        // fall through and send message
    } else if (info->net_tag == NET_UDP_SOCKET) {
        int len;
        if (ioctlsocket(sock, FIONREAD, &len) == -1) {
            perror("udp_recv_handler");
            return O2_FAIL;
        }
        info->in_message = o2_alloc_size_message(len);
        if (!info->in_message) return O2_FAIL;
        int n;
        // coerce to int to avoid compiler warning; len is int, so int is good for n
        if ((n = (int) recvfrom(sock, (char *) &(info->in_message->data), len,
                                0, NULL, NULL)) <= 0) {
            // I think udp errors should be ignored. UDP is not reliable
            // anyway. For now, though, let's at least print errors.
            perror("recvfrom in udp_recv_handler");
            o2_message_free(info->in_message);
            info->in_message = NULL;
            return O2_FAIL;
        }
        info->in_message->length = n;
        // fall through and send message
    } else if (info->net_tag == NET_TCP_SERVER) {
        // note that this handler does not call read_whole_message()
        SOCKET connection = accept(sock, NULL, NULL);
        if (connection == INVALID_SOCKET) {
            O2_DBg(printf("%s tcp_accept_handler failed to accept\n",
                          o2_debug_prefix));
            return O2_FAIL;
        }
        int set = 1;
#ifdef __APPLE__
        setsockopt(connection, SOL_SOCKET, SO_NOSIGPIPE,
                   (void *) &set, sizeof(int));
#endif
        int tag = (info->tag == INFO_TCP_SERVER ? INFO_TCP_SOCKET :
                                                  INFO_OSC_TCP_CLIENT);
        o2n_info_ptr conn = socket_info_new(connection, tag, NET_TCP_CONNECTION);
        O2_DBdo(printf("%s O2 server socket %ld accepts client as socket %ld index %d\n",
                       o2_debug_prefix, (long) sock, (long) connection, conn->fds_index));
        assert(conn);
        return O2_SUCCESS;
    } else {
        assert(FALSE);
    }
    // COMMON CODE for TCP and UDP receive message:
    // endian corrections are done in handler
    if ((*o2n_send_by_tcp)(info) == O2_SUCCESS) {
        info_message_cleanup(info);
    } else if (info->net_tag == NET_TCP_CONNECTING || info->net_tag == NET_TCP_CLIENT ||
               info->net_tag == NET_TCP_CONNECTION) {
        o2_info_remove(info);
    }
    return O2_SUCCESS;
}

