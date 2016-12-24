//
//  o2_socket.c
//  O2
//
//  Created by 弛张 on 2/4/16.
//  Copyright © 2016 弛张. All rights reserved.
//
#include "ctype.h"
#include "o2.h"
#include "o2_dynamic.h"
#include "o2_socket.h"
#include "o2_search.h"
#include "o2_internal.h"
#include "o2_sched.h"
#include "o2_send.h"
#include "o2_interoperation.h"
#include "o2_socket.h"

#ifdef WIN32
#include <stdio.h> 
#include <stdlib.h> 
#include <windows.h>
#include <errno.h>
#else
#include "sys/ioctl.h"
#include <ifaddrs.h>
#endif


char o2_local_ip[24];
int o2_local_tcp_port = 0;
SOCKET local_send_sock = INVALID_SOCKET; // socket for sending all UDP msgs

dyn_array o2_fds; ///< pre-constructed fds parameter for poll()
dyn_array o2_fds_info; ///< info about sockets

fds_info_ptr o2_process = NULL; ///< the process descriptor for this process

int o2_found_network = FALSE;

#if defined(WIN32) || defined(_MSC_VER)

static int stateWSock = -1;
int initWSock()
{
    WORD reqversion;
    WSADATA wsaData;
    if (stateWSock >= 0) {
        return stateWSock;
    }
    /* TODO - which version of Winsock do we actually need? */
    
    reqversion = MAKEWORD(2, 2);
    if (WSAStartup(reqversion, &wsaData) != 0) {
        /* Couldn't initialize Winsock */
        stateWSock = 0;
    } else if (LOBYTE(wsaData.wVersion) != LOBYTE(reqversion) ||
               HIBYTE(wsaData.wVersion) != HIBYTE(reqversion)) {
        /* wrong version */
        WSACleanup();
        stateWSock = 0;
    } else {
        stateWSock = 1;
    }    
    return stateWSock;
}


static int detect_windows_service_2003_or_later()
{
    OSVERSIONINFOEX osvi;
    DWORDLONG dwlConditionMask = 0;
    int op=VER_GREATER_EQUAL;
    
    ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
    osvi.dwMajorVersion = 5;
    osvi.dwMinorVersion = 2;
    
    VER_SET_CONDITION( dwlConditionMask, VER_MAJORVERSION, op );
    VER_SET_CONDITION( dwlConditionMask, VER_MINORVERSION, op );
    
    return VerifyVersionInfo(
                                      &osvi,
                                      VER_MAJORVERSION | VER_MINORVERSION |
                                      VER_SERVICEPACKMAJOR | VER_SERVICEPACKMINOR,
                                      dwlConditionMask);
}

#endif


void deliver_or_schedule(fds_info_ptr info)
{
    // make sure endian is compatible
    if (info->proc.little_endian) {
        o2_msg_swap_endian(info->message, FALSE);
    }

#ifndef O2_NO_DEBUGGING
    if (o2_debug > 2 || // non-o2-system messages only if o2_debug <= 2
        (o2_debug > 1 && info->message->data.address[1] != '_' &&
                         !isdigit(info->message->data.address[1]))) {
            printf("O2: received ");
            o2_print_msg(info->message);
            printf("\n");
    }
#endif
    if (info->message->data.timestamp > 0.0) {
        if (o2_gtsched_started) {
            if (info->message->data.timestamp > o2_global_now) {
                o2_schedule(&o2_gtsched, info->message);
            } else {
                find_and_call_handlers(info->message, NULL);
            }
        } // else drop the message, no timestamps allowed before clock sync
    } else {
        find_and_call_handlers(info->message, NULL);
    }
}


fds_info_ptr o2_add_new_socket(SOCKET sock, int tag, o2_socket_handler handler)
{
    // expand socket arrays for new port
    DA_EXPAND(o2_fds_info, struct fds_info);
    DA_EXPAND(o2_fds, struct pollfd);
    fds_info_ptr info = DA_LAST(o2_fds_info, struct fds_info);
    struct pollfd *pfd = DA_LAST(o2_fds, struct pollfd);
    info->tag = tag;
    info->handler = handler;
    info->length = 0;
    info->length_got = 0;
    info->message = NULL;
    info->message_got = 0;
    // info->delete_flag = FALSE;
    pfd->fd = sock;
    pfd->events = POLLIN;
    // printf("%s: added new socket at %d", debug_prefix, o2_fds.length - 1);
    // if (process == &o2_process) printf(" for local process");
    // if (process) printf(" key %s",  process->name);
    // else printf("\n");
    return info;
}


// remove the i'th socket from o2_fds and o2_fds_info
//
void o2_remove_socket(int i)
{
    if (o2_fds.length > i + 1) { // move last to i
        struct pollfd *fd = DA_LAST(o2_fds, struct pollfd);
        memcpy(DA_GET(o2_fds, struct pollfd, i), fd, sizeof(struct pollfd));
        fds_info_ptr info = DA_LAST(o2_fds_info, fds_info);
        // fix the back-reference from the services if applicable
        if (info->tag == TCP_SOCKET) {
            for (int j = 0; j < info->proc.services.length; j++) {
                char *service_name = *DA_GET(info->proc.services, char *, j);
                int h;
                generic_entry_ptr entry = *lookup(&path_tree_table, service_name, &h);
                ((remote_service_entry_ptr) entry)->process_index = i;
            }
        }
        memcpy(DA_GET(o2_fds_info, fds_info, i), info, sizeof(fds_info));
    }
    o2_fds.length--;
    o2_fds_info.length--;
}


static struct sockaddr_in o2_serv_addr;

int bind_recv_socket(SOCKET sock, int *port, int tcp_recv_flag)
{
    memset(PTR(&o2_serv_addr), 0, sizeof(o2_serv_addr));
    o2_serv_addr.sin_family = AF_INET;
    o2_serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // local IP address
    o2_serv_addr.sin_port = htons(*port);
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
    // printf("*   %s: bind socket %d port %d\n", debug_prefix, sock, *port);
    assert(*port != 0);
    return O2_SUCCESS;
}


int udp_recv_handler(SOCKET sock, struct fds_info *info)
{
    int len;
    if (ioctlsocket(sock, FIONREAD, &len) == -1) {
        perror("udp_recv_handler");
        return O2_FAIL;
    }
    info->message = o2_alloc_size_message(len);
    if (!info->message) return O2_FAIL;
    int n;
    // coerce to int to avoid compiler warning; len is int, so int is good for n
    if ((n = (int) recvfrom(sock, (char *) &(info->message->data), len, 
                            0, NULL, NULL)) <= 0) {
        // I think udp errors should be ignored. UDP is not reliable
        // anyway. For now, though, let's at least print errors.
        perror("recvfrom in udp_recv_handler");
        o2_free_message(info->message);
        return O2_FAIL;
    }
    info->message->length = n;
    // endian corrections are done in handler
    if (info->tag == UDP_SOCKET || info->tag == DISCOVER_SOCKET) {
        deliver_or_schedule(info);
    } else if (info->tag == OSC_SOCKET) {
        o2_deliver_osc(info);
    } else {
        assert(FALSE); // unexpected tag in fd_info
        return O2_FAIL;
    }
    info->message = NULL; // message is deleted by now
    return O2_SUCCESS;
}


void tcp_message_cleanup(struct fds_info *info)
{
    /* clean up for next message */
    info->message = NULL;
    info->message_got = 0;
    info->length = 0;
    info->length_got = 0;
}    


// returns O2_SUCCESS if whole message is read.
//         O2_FAIL if whole message is not read yet.
//         O2_TCP_HUP if socket is closed
//
int read_whole_message(SOCKET sock, struct fds_info *info)
{
    assert(info->length_got < 5);
    // printf("--   %s: read_whole message length_got %d length %d message_got %d\n",
    //       debug_prefix, info->length_got, info->length, info->message_got);
    /* first read length if it has not been read yet */
    if (info->length_got < 4) {
        // coerce to int to avoid compiler warning; requested length is
        // int, so int is ok for n
        int n = (int) recvfrom(sock, PTR(&(info->length)) + info->length_got,
                               4 - info->length_got, 0, NULL, NULL);
        if (n <= 0) { /* error: close the socket */
#ifdef WIN32
            if ((errno != EAGAIN && errno != EINTR) ||
                (GetLastError() != WSAEWOULDBLOCK &&
                 GetLastError() != WSAEINTR)) {
                if (errno == ECONNRESET || GetLastError() == WSAECONNRESET) {
                    return O2_TCP_HUP;
                }
#else
            if (errno != EAGAIN && errno != EINTR) {
#endif
                perror("recvfrom in read_whole_message getting length");
                tcp_message_cleanup(info);
                return O2_TCP_HUP;
            }
        }
        info->length_got += n;
        assert(info->length_got < 5);
        if (info->length_got < 4) {
            return FALSE;
        }
        // done receiving length bytes
        info->length = htonl(info->length);
        info->message = o2_alloc_size_message(info->length);
        info->message_got = 0; // just to make sure
    }

    /* read the full message */
    if (info->message_got < info->length) {
        // coerce to int to avoid compiler warning; message length is int, so n can be int
        int n = (int) recvfrom(sock,
                               PTR(&(info->message->data)) + info->message_got,
                               info->length - info->message_got, 0, NULL, NULL);
        if (n <= 0) {
#ifdef WIN32
            if ((errno != EAGAIN && errno != EINTR) ||
                (GetLastError() != WSAEWOULDBLOCK &&
                 GetLastError() != WSAEINTR)) {
                if (errno == ECONNRESET || GetLastError() == WSAECONNRESET) {
                    return O2_TCP_HUP;
                }
#else
            if (errno != EAGAIN && errno != EINTR) {
#endif
                perror("recvfrom in read_whole_message getting data");
                o2_free_message(info->message);
                tcp_message_cleanup(info);
                return O2_TCP_HUP;
            }
        }
        info->message_got += n;
        if (info->message_got < info->length) {
            return O2_FAIL; 
        }
    }
    info->message->length = info->length;
    // printf("-    %s: received tcp msg %s\n", debug_prefix, info->message->data.address);
    return O2_SUCCESS; // we have a full message now
}



int tcp_recv_handler(SOCKET sock, struct fds_info *info)
{
    int n = read_whole_message(sock, info);
    if (n != O2_SUCCESS) {
        return n;
    }

    // endian fixup is included in this handler:
    deliver_or_schedule(info);
    // info->message is now freed
    tcp_message_cleanup(info);
    return O2_SUCCESS;
}

// socket handler to forward incoming OSC to O2 service
//
int osc_tcp_handler(SOCKET sock, struct fds_info *info)
{
    int n = read_whole_message(sock, info);
    if (n != O2_SUCCESS) {
        return n;
    }
    /* got the message, deliver it */
    // endian corrections are done in handler
    RETURN_IF_ERROR(o2_deliver_osc(info));
    // info->message is now freed
    tcp_message_cleanup(info);
    return O2_SUCCESS;
}
        
        
// When service is delegated to OSC via TCP, a TCP connection
// is created. Incoming messages are delivered to this
// handler, but they are ignored.
//
int o2_osc_delegate_handler(SOCKET sock, fds_info_ptr info)
{
    int n = read_whole_message(sock, info);
    if (n != O2_SUCCESS) {
        return n;
    }
    o2_free_message(info->message);
    tcp_message_cleanup(info);
    return O2_SUCCESS;
}


// When a connection is accepted, we cannot know the name of the
// connecting process, so the first message that arrives will have
// to be to /o2_/dy, type="sssii", big/little endian, application name,
// ip, udp, tcp
// We then create a process (if not discovered yet) and associate
// this socket with the process
//
int o2_tcp_initial_handler(SOCKET sock, fds_info_ptr info)
{
    int n = read_whole_message(sock, info);
    if (n != O2_SUCCESS) {
        return n;
    }

    // message should be addressed to !*/in, where * is (hopefully) this
    // process, but we're not going to check that (could also be "!_o2/in")
    char *ptr = info->message->data.address;
    if (*ptr != '!') return O2_TCP_HUP;
    ptr = strstr(ptr + 1, "/in");
    if (!ptr) return O2_TCP_HUP;
    if (ptr[3] != 0) return O2_TCP_HUP;
    
    // types will be after "!IP:TCP_PORT/in<0>,"
    // this is tricky: ptr + 3 points to end-of-string after address; there
    // could be 1 to 4 zeros, so to point into the next word, use ptr + 7 in
    // case there are 4 zeros. WORD_ALIGN_POINTER backs up to beginning of the
    // next word, which is where types begin, then add 1 to skip the ',' that
    // begins the type string
    ptr = WORD_ALIGN_PTR(ptr + 7) + 1; // skip over the ','
    o2_discovery_init_handler(info->message, ptr, NULL, 0, info);
    info->handler = &tcp_recv_handler;
    // printf("%s: o2_tcp_initial_handler completed for %s\n", debug_prefix,
    //       info->proc_info->name);
    // since we called o2_discovery_init_handler directly,
    //   we need to free the message
    o2_free_message(info->message);
    tcp_message_cleanup(info);
    return O2_SUCCESS;
}


// This handler is for the tcp server listen socket. When it is
// "readable" this handler is called to accept the connection
// request.
//
int tcp_accept_handler(SOCKET sock, fds_info_ptr info)
{
    // note that this handler does not call read_whole_message()
    // printf("%s: accepting a tcp connection\n", debug_prefix);
    SOCKET connection = accept(sock, NULL, NULL);
    int set = 1;
#ifdef __APPLE__
    setsockopt(connection, SOL_SOCKET, SO_NOSIGPIPE,
               (void *) &set, sizeof(int));
#endif
    o2_add_new_socket(connection, TCP_SOCKET, &o2_tcp_initial_handler);
    return O2_SUCCESS;
}


// This handler is for an OSC tcp server listen socket. When it is
// "readable" this handler is called to accept the connection
// request, creating a OSC_TCP_SOCKET
//
int o2_osc_tcp_accept_handler(SOCKET sock, fds_info_ptr info)
{
    // note that this handler does not call read_whole_message()
    // printf("%s: accepting a tcp connection\n", debug_prefix);
    SOCKET connection = accept(sock, NULL, NULL);
    int set = 1;
#ifndef WIN32
    setsockopt(connection, SOL_SOCKET, SO_NOSIGPIPE,
               (void *) &set, sizeof(int));
#endif
    info = o2_add_new_socket(connection, OSC_TCP_SOCKET, &osc_tcp_handler);
    info->proc.name = NULL;
    info->proc.status = PROCESS_CONNECTING;
    DA_INIT(info->proc.services, char *, 0);
    info->proc.little_endian = FALSE;
    info->proc.udp_port = 0;
    memset(&(info->proc.udp_sa), 0, sizeof(info->proc.udp_sa));
    return O2_SUCCESS;
}


int make_udp_recv_socket(int tag, int *port, fds_info_ptr *info)
{
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET)
        return O2_FAIL;
    // Bind the socket
    int err;
    if ((err = bind_recv_socket(sock, port, FALSE))) {
        closesocket(sock);
        return err;
    }
    *info = o2_add_new_socket(sock, tag, &udp_recv_handler);
    // printf("%s: make_udp_recv_socket: listening on port %d\n", debug_prefix, o2_process.udp_port);
    return O2_SUCCESS;
}


int o2_init_process(fds_info_ptr info, const char *name, int status,
                    int is_little_endian)
{
    info->proc.name = o2_heapify(name);
    info->proc.status = status;
    if (!info->proc.name) return O2_FAIL;
    DA_INIT(info->proc.services, char *, 0);
    info->proc.little_endian = is_little_endian;
    info->proc.udp_port = 0;
    memset(&info->proc.udp_sa, 0, sizeof(info->proc.udp_sa));
    if (status != PROCESS_LOCAL) { // not local process
        // make an entry for the path_tree_table
        // put a remote service entry in the path_tree_table
        add_remote_service(info, info->proc.name);
        RETURN_IF_ERROR(o2_send_init(info));
        RETURN_IF_ERROR(o2_send_services(info));
    }
    return O2_SUCCESS;
}


/**
 *  Initialize discovery, tcp, and udp sockets.
 *
 *  @return 0 (O2_SUCCESS) on success, -1 (O2_FAIL) on failure.
 */
int init_sockets()
{
#ifdef WIN32
    // Initialize (in Windows)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (!initWSock()) {
        return O2_FAIL;
    }
#else
    DA_INIT(o2_fds, struct pollfd, 5);
#endif // WIN32
    
    DA_INIT(o2_fds_info, struct fds_info, 5);
    memset(o2_fds_info.array, 5 * sizeof(fds_info), 0);
    
    // Set a broadcast socket. If cannot set up,
    //   print the error and return O2_FAIL
    RETURN_IF_ERROR(o2_discovery_init());

    // make udp receive socket for incoming O2 messages
    int port = 0;
    fds_info_ptr info;
    RETURN_IF_ERROR(make_udp_recv_socket(UDP_SOCKET, &port, &info));
    // ignore the info for udp, get the info for tcp:
    // Set up the tcp server socket.
    RETURN_IF_ERROR(make_tcp_recv_socket(TCP_SERVER_SOCKET,
                                         &tcp_accept_handler, &o2_process));
    o2_process->proc.udp_port = port;
    
    // more initialization in discovery, depends on tcp port which is now set
    RETURN_IF_ERROR(o2_discovery_msg_init());

/* IF THAT FAILS, HERE'S OLDER CODE TO GET THE TCP PORT
    struct sockaddr_in sa;
    socklen_t restrict sa_len = sizeof(sa);
    if (getsockname(o2_process.tcp_socket, (struct sockaddr *restrict)
                    &sa, &sa_len) < 0) {
        perror("Getting port number from tcp server socket");
        return O2_FAIL;
    }
    o2_process.tcp_port = ntohs(sa.sin_port);
*/
    return O2_SUCCESS;
}

// Add a socket for TCP to sockets arrays o2_fds and o2_fds_info
// As a side effect, if this is the TCP server socket, the
// o2_process.key will be set to the server IP address
int make_tcp_recv_socket(int tag, o2_socket_handler handler, fds_info_ptr *info)
{
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    char name[32]; // "100.100.100.100:65000" -> 21 chars
    struct ifaddrs *ifap, *ifa;
    name[0] = 0;   // initially empty
    if (sock == INVALID_SOCKET) {
        printf("tcp socket set up error");
        return O2_FAIL;
    }
    int port = 0;
    if (tag == TCP_SERVER_SOCKET) { // only bind server port
        RETURN_IF_ERROR(bind_recv_socket(sock, &port, TRUE));
        o2_local_tcp_port = port;
    
        RETURN_IF_ERROR(listen(sock, 10));

        struct sockaddr_in *sa;
        // look for AF_INET interface. If you find one, copy it
        // to name. If you find one that is not 127.0.0.1, then
        // stop looking.
        
        if (getifaddrs (&ifap)) {
            perror("getting IP address");
            return O2_FAIL;
        }
        for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr->sa_family==AF_INET) {
                sa = (struct sockaddr_in *) ifa->ifa_addr;
                if (!inet_ntop(AF_INET, &sa->sin_addr, o2_local_ip,
                               sizeof(o2_local_ip))) {
                    perror("converting local ip to string");
                    break;
                }
                sprintf(name, "%s:%d", o2_local_ip, port);
                if (!streql(o2_local_ip, "127.0.0.1")) {
                    o2_found_network = O2_TRUE;
                    break;
                }
            }
        }
    }
    *info = o2_add_new_socket(sock, tag, handler);
    if (tag == TCP_SERVER_SOCKET) {
        RETURN_IF_ERROR(o2_init_process(*info, name, PROCESS_LOCAL, 
                                        IS_LITTLE_ENDIAN));
        freeifaddrs(ifap);
    }
    return O2_SUCCESS;
}

                                    
#ifdef WIN32

FD_SET o2_read_set;
struct timeval o2_no_timeout;

int o2_recv()
{
    int total;
    FD_ZERO(&o2_read_set);
    for (int i = 0; i < o2_fds.length; i++) {
        struct pollfd *d = DA_GET(o2_fds, struct pollfd, i);
        FD_SET(d->fd, &o2_read_set);
    }
    o2_no_timeout.tv_sec = 0;
    o2_no_timeout.tv_usec = 0;
    if ((total = select(0, &o2_read_set, NULL, NULL, &o2_no_timeout)) ==
        SOCKET_ERROR) {
        /* TODO: error handling here */
        return O2_FAIL; /* TODO: return a specific error code for this */
    }
    if (total == 0) { /* no messages waiting */
        return O2_SUCCESS;
    }
    for (int i = 0; i < o2_fds.length; i++) {
        struct pollfd *d = DA_GET(o2_fds, struct pollfd, i);
        if (FD_ISSET(d->fd, &o2_read_set)) {
            fds_info_ptr info = DA_GET(o2_fds_info, fds_info, i);
            if (((*(info->handler))(d->fd, info)) == O2_TCP_HUP) {
                o2_remove_remote_process(info);
                i--; // we moved last into i, so look at i again
            }
        }
    }
    return O2_SUCCESS;
}


static struct sockaddr *dupaddr(const sockaddr_gen * src)
{
    sockaddr_gen * d = malloc(sizeof(*d));
    if (d) {
        memcpy(d, src, sizeof(*d));
    }
    return (struct sockaddr *) d;
}

int getifaddrs(struct ifaddrs **ifpp)
{
    SOCKET sock = INVALID_SOCKET;
    size_t intarray_len = 8192;
    int ret = -1;
    INTERFACE_INFO *intarray = NULL;

    *ifpp = NULL;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET)
        return -1;

    for (;;) {
        DWORD cbret = 0;

        intarray = malloc(intarray_len);
        if (!intarray)
            break;

        ZeroMemory(intarray, intarray_len);

        if (WSAIoctl(sock, SIO_GET_INTERFACE_LIST, NULL, 0,
            (LPVOID)intarray, (DWORD)intarray_len, &cbret,
            NULL, NULL) == 0) {
            intarray_len = cbret;
            break;
        }

        free(intarray);
        intarray = NULL;

        if (WSAGetLastError() == WSAEFAULT && cbret > intarray_len) {
            intarray_len = cbret;
        }
        else {
            break;
        }
    }

    if (!intarray)
        goto _exit;

    /* intarray is an array of INTERFACE_INFO structures.  intarray_len has the
    actual size of the buffer.  The number of elements is
    intarray_len/sizeof(INTERFACE_INFO) */

    {
        size_t n = intarray_len / sizeof(INTERFACE_INFO);
        size_t i;

        for (i = 0; i < n; i++) {
            struct ifaddrs *ifp;

            ifp = malloc(sizeof(*ifp));
            if (ifp == NULL)
                break;

            ZeroMemory(ifp, sizeof(*ifp));

            ifp->ifa_next = NULL;
            ifp->ifa_name = NULL;
            ifp->ifa_flags = intarray[i].iiFlags;
            ifp->ifa_addr = dupaddr(&intarray[i].iiAddress);
            ifp->ifa_netmask = dupaddr(&intarray[i].iiNetmask);
            ifp->ifa_broadaddr = dupaddr(&intarray[i].iiBroadcastAddress);
            ifp->ifa_data = NULL;

            *ifpp = ifp;
            ifpp = &ifp->ifa_next;
        }

        if (i == n)
            ret = 0;
    }

_exit:

    if (sock != INVALID_SOCKET)
        closesocket(sock);

    if (intarray)
        free(intarray);

    return ret;
}

void freeifaddrs(struct ifaddrs *ifp)
{
    free(ifp);
}

#else  // Use poll function to receive messages.

int o2_recv()
{
    int i;
        
    poll((struct pollfd *) o2_fds.array, o2_fds.length, 0);
    int len = o2_fds.length; // length can grow while we're looping!
    for (i = 0; i < len; i++) {
        struct pollfd *d = DA_GET(o2_fds, struct pollfd, i);
        // if (d->revents) printf("%d:%p:%x ", i, d, d->revents);
        if (d->revents & POLLERR) {
            printf("d->revents & POLLERR %d, d->revents & POLLHUP %d\n",
                   d->revents & POLLERR, d->revents & POLLHUP);
        } else if (d->revents & POLLHUP) {
            fds_info_ptr info = DA_GET(o2_fds_info, fds_info, i);
            // info->delete_flag = TRUE;
            o2_remove_remote_process(info);
            i--; // we moved last array elements to i, so visit i again
            len--; // length gets smaller when we remove an element
        } else if (d->revents) {
            fds_info_ptr info = DA_GET(o2_fds_info, fds_info, i);
            assert(info->length_got < 5);
            (*(info->handler))(d->fd, info);
        }
    }
  
    return O2_SUCCESS;
}
#endif

