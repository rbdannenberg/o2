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

#ifdef WIN32
#include <stdio.h> 
#include <stdlib.h> 
#include <windows.h>
#else
#include "sys/ioctl.h"
#include <ifaddrs.h>
#endif


//#include <netdb.h>
//#include <sys/un.h>

char o2_local_ip[24];
int o2_local_tcp_port = 0;
SOCKET local_send_sock = INVALID_SOCKET; // socket for sending all UDP msgs

dyn_array o2_fds; ///< pre-constructed fds parameter for poll()
dyn_array o2_fds_info; ///< info about sockets

process_info o2_process; ///< the process descriptor for this process

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


void deliver_or_schedule(o2_message_ptr msg)
{
#ifndef O2_NO_DEBUGGING
    if (o2_debug > 2 || // non-o2-system messages only if o2_debug <= 2
        (o2_debug > 1 && msg->data.address[1] != '_' &&
                         !isdigit(msg->data.address[1]))) {
            printf("O2: received ");
            o2_print_msg(msg);
            printf("\n");
    }
#endif
    if (msg->data.timestamp > 0.0) {
        if (o2_gtsched_started) {
            if (msg->data.timestamp > o2_global_now) {
                o2_schedule(&o2_gtsched, msg);
            } else {
                find_and_call_handlers(msg);
            }
        } // else drop the message, no timestamps allowed before clock sync
    } else {
        find_and_call_handlers(msg);
    }
}


void add_new_socket(SOCKET sock, int tag, process_info_ptr process,
                    void (*handler)(SOCKET sock, struct fds_info *info))
{
    // expand socket arrays for new port
    DA_EXPAND(o2_fds_info, struct fds_info);
    DA_EXPAND(o2_fds, struct pollfd);
    fds_info_ptr info = DA_LAST(o2_fds_info, struct fds_info);
    struct pollfd *pfd = DA_LAST(o2_fds, struct pollfd);
    info->tag = tag;
    info->u.process_info = process;
    info->handler = handler;
    info->length = 0;
    info->length_got = 0;
    info->message = NULL;
    info->message_got = 0;
    pfd->fd = sock;
    pfd->events = POLLIN;
    // printf("%s: added new socket at %d", debug_prefix, o2_fds.length - 1);
    // if (process == &o2_process) printf(" for local process");
    // if (process) printf(" key %s",  process->name);
}


// remove the i'th socket from o2_fds and o2_fds_info
//   does not remove a process, see o2_remove_remote_process()
//
void o2_remove_socket(int i)
{
    if (o2_fds.length > i + 1) { // move last to i
        struct pollfd *fd = DA_LAST(o2_fds, struct pollfd);
        memcpy(DA_GET(o2_fds, struct pollfd, i), fd, sizeof(struct pollfd));
        fds_info_ptr info = DA_LAST(o2_fds_info, fds_info);
        // fix the back-reference from the process if applicable
        if (info->tag == TCP_SOCKET && info->u.process_info &&
            info->u.process_info->tcp_fd_index == o2_fds.length - 1) {
            info->u.process_info->tcp_fd_index = i;
        }
        memcpy(DA_GET(o2_fds_info, fds_info, i), info, sizeof(fds_info));
    }
    o2_fds.length--;
    o2_fds_info.length--;
}


static struct sockaddr_in o2_serv_addr;

int bind_recv_socket(SOCKET sock, int *port, int tcp_recv_flag)
{
    memset((char *) &o2_serv_addr, 0, sizeof(o2_serv_addr));
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


void udp_recv_handler(SOCKET sock, struct fds_info *info)
{
    o2_message_ptr msg;
    int len;
#ifndef WIN32
    if (ioctl(sock, FIONREAD, &len) == -1) {
#else
	if (ioctlsocket(sock, FIONREAD, &len) == -1) {
#endif
        perror("udp_recv_handler");
        return;
    }
    if (len <= MESSAGE_SIZE_FROM_ALLOCATED(MESSAGE_DEFAULT_SIZE)) {
        msg = alloc_message();
    } else {
        msg = (o2_message_ptr) o2_malloc(MESSAGE_SIZE_FROM_ALLOCATED(len));
    }
    if (!msg) return;
    int n;
    if ((n = recvfrom(sock, &(msg->data), len, 0, NULL, NULL)) <= 0) {
        // I think udp errors should be ignored. UDP is not reliable
        // anyway. For now, though, let's at least print errors.
        perror("recvfrom in udp_recv_handler");
        o2_free_message(info->message);
        return;
    }
    msg->length = n;
    // endian corrections are done in handler
    deliver_or_schedule(msg);
}


void tcp_message_cleanup(struct fds_info *info)
{
    /* clean up for next message */
    info->message = NULL;
    info->message_got = 0;
    info->length = 0;
    info->length_got = 0;
}    


int read_whole_message(SOCKET sock, struct fds_info *info)
{
    assert(info->length_got < 5);
    // printf("--   %s: read_whole message length_got %d length %d message_got %d\n",
    //       debug_prefix, info->length_got, info->length, info->message_got);
    /* first read length if it has not been read yet */
    if (info->length_got < 4) {
        int n = recvfrom(sock, ((char *) &(info->length)) + info->length_got,
                         4 - info->length_got, 0, NULL, NULL);
        if (n <= 0) { /* error: close the socket */
            if (errno != EAGAIN && errno != EINTR) {
                perror("recvfrom in read_whole_message getting length");
                tcp_message_cleanup(info);
                return O2_FAIL;
            }
        }
        info->length_got += n;
        assert(info->length_got < 5);
        if (info->length_got < 4) {
            return FALSE;
        }
        // done receiving length bytes
        info->length = htonl(info->length);
        info->message = alloc_size_message(info->length);
        info->message_got = 0; // just to make sure
    }

    /* read the full message */
    if (info->message_got < info->length) {
        int n = recvfrom(sock,
                         ((char *) &(info->message->data)) + info->message_got,
                         info->length - info->message_got, 0, NULL, NULL);
        if (n <= 0) {
            if (errno != EAGAIN && errno != EINTR) {
                perror("recvfrom in read_whole_message getting data");
                o2_free_message(info->message);
                tcp_message_cleanup(info);
                return O2_FAIL;
            }
        }
        info->message_got += n;
        if (info->message_got < info->length) {
            return FALSE; 
        }
    }
    info->message->length = info->length;
    // printf("-    %s: received tcp msg %s\n", debug_prefix, info->message->data.address);
    return TRUE; // we have a full message now
}


void tcp_recv_handler(SOCKET sock, struct fds_info *info)
{
    if (!read_whole_message(sock, info)) return;
    
    /* got the message, deliver it */
    // endian corrections are done in handler
    deliver_or_schedule(info->message);
    // info->message is now freed
    tcp_message_cleanup(info);
}


// When a connection is accepted, we cannot know the name of the
// connecting process, so the first message that arrives will have
// to be to /o2_/dy, type="sssii", big/little endian, application name,
// ip, udp, tcp
// We then create a process (if not discovered yet) and associate
// this socket with the process
//
void tcp_initial_handler(SOCKET sock, struct fds_info *info)
{
    if (!read_whole_message(sock, info)) return;

    // message should be addressed to !*/in, where * is (hopefully) this
    // process, but we're not going to check that (could also be "!_o2/in")
    char *ptr = info->message->data.address;
    if (*ptr != '!') return;
    ptr = strstr(ptr + 1, "/in");
    if (!ptr) return;
    if (ptr[3] != 0) return;
    
    // types will be after "!IP:TCP_PORT/in<0>,"
    // this is tricky: ptr + 3 points to end-of-string after address; there
    // could be 1 to 4 zeros, so to point into the next word, use ptr + 7 in
    // case there are 4 zeros. WORD_ALIGN_POINTER backs up to beginning of the
    // next word, which is where types begin, then add 1 to skip the ',' that
    // begins the type string
    ptr = WORD_ALIGN_PTR(ptr + 7) + 1; // skip over the ','
    o2_discovery_init_handler(info->message, ptr, NULL, 0, info);
    info->handler = &tcp_recv_handler;
    info->u.process_info->tcp_fd_index =
        info - (struct fds_info *) (o2_fds_info.array);
    // printf("%s: tcp_initial_handler completed for %s\n", debug_prefix,
    //       info->u.process_info->name);
    // since we called o2_discovery_init_handler directly,
    //   we need to free the message
    o2_free_message(info->message);
    tcp_message_cleanup(info);
    return;
}


// This handler is for the tcp server listen socket. When it is
// "readable" this handler is called to accept the connection
// request.
//
void tcp_accept_handler(SOCKET sock, struct fds_info *info)
{
    // note that this handler does not call read_whole_message()
    // printf("%s: accepting a tcp connection\n", debug_prefix);
    SOCKET connection = accept(sock, NULL, NULL);
    int set = 1;
    setsockopt(connection, SOL_SOCKET, SO_NOSIGPIPE,
               (void *) &set, sizeof(int));
    add_new_socket(connection, TCP_SOCKET, NULL, &tcp_initial_handler);
}


int make_udp_recv_socket(int tag, int port)
{
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET)
        return O2_FAIL;
    int specified_port = port;
    // Bind the socket
    int err;
    if ((err = bind_recv_socket(sock, &port, FALSE))) {
#ifndef WIN32
		close(sock);
#else
		closesocket(sock);
#endif
        return err;
    }
    add_new_socket(sock, tag, &o2_process, &udp_recv_handler);
    // If port was 0 (unspecified), we must be creating the general
    // UDP message receive port for this process. Remember it.
    if (!specified_port) {
        o2_process.udp_port = port;
    }
    // printf("%s: make_udp_recv_socket: listening on port %d\n", debug_prefix, o2_process.udp_port);
    return O2_SUCCESS;
}


/**
 *  Initialize discovery, tcp, and udp sockets.
 *
 *  @return 0 (O2_SUCCESS) on success, -1 (O2_FAIL) on failure.
 */
int init_sockets()
{
#ifdef _WIN32
    // Initialize (in Windows)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (!initWSock()) {
        return O2_FAIL;
    }
#else
    DA_INIT(o2_fds, struct pollfd, 5);
#endif // _WIN32
    
    DA_INIT(o2_fds_info, struct fds_info, 5);
    memset(o2_fds_info.array, 5 * sizeof(fds_info), 0);
    
    // Set a broadcast socket. If cannot set up,
    //   print the error and return O2_FAIL
    int err;
    if ((err = o2_discovery_init())) return err;

    // make udp receive socket for incoming O2 messages
    if ((err = make_udp_recv_socket(UDP_SOCKET, 0))) return err;

    // Set up the tcp server socket.
    if ((err = make_tcp_recv_socket(TCP_SERVER_SOCKET, &o2_process))) return err;

    // more initialization in discovery, depends on tcp port which is now set
    if ((err = o2_discovery_msg_init())) return err;

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
int make_tcp_recv_socket(int tag, process_info_ptr process)
{
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        printf("tcp socket set up error");
        return O2_FAIL;
    }
    int port = 0;
    if (process == &o2_process) { // only bind server port
        int err;
        if ((err = bind_recv_socket(sock, &port, TRUE))) return err;
        o2_local_tcp_port = port;
    
        if ((err = listen(sock, 10))) return err;

        struct ifaddrs *ifap, *ifa;
        struct sockaddr_in *sa;
        char name[32]; // "100.100.100.100:65000" -> 21 chars
        name[0] = 0;   // initially empty
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
        o2_process.name = o2_heapify(name);
        // printf("%s: **** Process Key (IP:TCP_PORT) is %s\n", debug_prefix, name);
        freeifaddrs(ifap);

        if (!o2_process.name) return O2_FAIL;
        process->tcp_fd_index = o2_fds.length;
    }
    add_new_socket(sock, tag, process,
                   (process == &o2_process ? &tcp_accept_handler :
                                             &tcp_initial_handler));
    return O2_SUCCESS;
}

                                    
#ifdef _WIN32

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
    if ((total = select(0, &o2_read_set, NULL, NULL, &o2_no_timeout)) == SOCKET_ERROR) {
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
            (*(info->handler))(d->fd, info);
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
    
    for (i = 0; i < o2_fds.length; i++) {
        struct pollfd *d = DA_GET(o2_fds, struct pollfd, i);
        // printf("%p:%x ", d, d->revents);
        if (d->revents & POLLERR) {
            printf("d->revents & POLLERR %d, d->revents & POLLHUP %d\n",
                   d->revents & POLLERR, d->revents & POLLHUP);
        } else if (d->revents & POLLHUP) {
            fds_info_ptr info = DA_GET(o2_fds_info, fds_info, i);
            o2_remove_remote_process(info->u.process_info);
            i--; // we moved last into i, so look at i again
        } else if (d->revents) {
            fds_info_ptr info = DA_GET(o2_fds_info, fds_info, i);
            assert(info->length_got < 5);
            (*(info->handler))(d->fd, info);
        }
    }
  
    return O2_SUCCESS;
}
#endif

