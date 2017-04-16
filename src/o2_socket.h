//
//  o2_socket.h
//  O2
//
//  Created by 弛张 on 2/4/16.
//  Copyright © 2016 弛张. All rights reserved.
//

#ifndef o2_socket_h
#define o2_socket_h

/**
 *  TCP and UDP head for different system
 */
#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>                   // Header for tcp connections

#define socklen_t int
#define sleep Sleep
#define strdup _strdup

/* Define pollfd for Windows */
struct poll_fd {
    __int64 fd; /* the windows socket number */
    int events; /* not used, but needed for compatibility */
};

#else
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include "arpa/inet.h"  // Headers for all inet functions
//#ifdef ENABLE_THREADS
//#include <pthread.h>
//#endif
#include <netinet/tcp.h>
#define closesocket close
#include <netdb.h>      // Header for socket transportations. Included "stdint.h"
typedef int SOCKET;     // In O2, we'll use SOCKET to denote the type of a socket
#define INVALID_SOCKET -1
#include <poll.h>
#endif

/**
 * The process_info structure tells us info about each socket. For Unix, there
 * is a parallel structure, fds, that contains an fds parameter for poll().
 *
 * In process_info, we have the socket, a handler for the socket, and buffer 
 * info to store incoming data, and the service the socket is attached to.
 * This structure is also used to represent a remote service if the
 * tag is TCP_SOCKET.
 * 
 */
#define UDP_SOCKET 100
#define TCP_SOCKET 101
#define OSC_SOCKET 102
#define DISCOVER_SOCKET 103
#define TCP_SERVER_SOCKET 104
#define OSC_TCP_SERVER_SOCKET 105
#define OSC_TCP_SOCKET 106
#define OSC_TCP_CLIENT 107

struct process_info;

struct fds_info; // recursive declarations o2_socket_handler and fds_info

typedef int (*o2_socket_handler)(SOCKET sock, struct process_info *info);

/* info->proc.status values */
#define PROCESS_LOCAL 0       // process is the local process
#define PROCESS_CONNECTED 1   // connect call returned or accepted connection
#define PROCESS_NO_CLOCK 2    // process initial message received, not clock synced
#define PROCESS_OK 3          // process is clock synced


// anything with a tag is of the "abstract superclass" o2_info
// subclasses are process_info, osc_info, and o2_entry
typedef struct o2_info {
    int tag;
} o2_info, *o2_info_ptr;


typedef struct process_info { // "subclass" of o2_info
    int tag;  // UDP_SOCKET, TCP_SOCKET, DISCOVER_SOCKET, TCP_SERVER_SOCKET
              // OSC_SOCKET, OSC_TCP_SERVER_SOCKET,
              // OSC_TCP_SOCKET, OSC_TCP_CLIENT

    int fds_index;              // index of socket in o2_fds and o2_fds_info
                                //   -1 if process known but not connected
    int delete_me;              // set to TRUE when socket should be removed
    int32_t length;             // message length
    o2_message_ptr message;     // message data from TCP stream goes here
    int length_got;             // how many bytes of length have been read?
    int message_got;            // how many bytes of message have been read?
    o2_socket_handler handler;  // handler for socket
    int port; // port number: if this is a TCP_SOCKET, this is the UDP port
              // number so that we can check for changes in discovery, and 
              // port is set by an incoming init message, the first message
              // received on the port.
              // If this is an OSC_SOCKET or OSC_TCP_SERVER_SOCKET,
              // this is the corresponding port number that is used by
              // o2_osc_port_free(). If this is an OSC_TCP_SOCKET, then
              // this is the port number of the OSC_TCP_SERVER_SOCKET from
              // which this socket was accepted. (It is used by
              // o2_osc_port_free() to identify the sockets to close.)
    union {
        struct {
            o2string name; // e.g. "128.2.1.100:55765", this is used so that when
            // we add a service, we can enumerate all the processes and send
            // them updates. Updates are addressed using this name field. Also,
            // when a new process is connected, we send an /in message to this
            // name. name is "owned" by the process_info struct and will be
            // deleted when the struct is freed
            int status; // PROCESS_LOCAL through PROCESS_OK
            int uses_hub; // indicates the remote process treats this as 
            // its hub -- discovery messages are sent over TCP
            dyn_array services; // these are the keys of remote_service_entry
                        // objects, owned by the service entries (do not free)
            struct sockaddr_in udp_sa;  // address for sending UDP messages
        } proc;
        struct {
            o2string service_name;
        } osc;
    };        
} process_info, *process_info_ptr;

extern process_info_ptr o2_message_source;

extern char o2_local_ip[24];
extern int o2_local_tcp_port;
extern dyn_array o2_fds_info;
#define GET_PROCESS(i) (*DA_GET(o2_fds_info, process_info_ptr, (i)))

extern int o2_found_network; // true if we have an IP address, which implies a
// network connection; if false, we only talk to 127.0.0.1 (localhost)

extern dyn_array o2_fds;  ///< pre-constructed fds parameter for poll()
extern int o2_socket_delete_flag;

/**
 * In windows, before we want to use the socket to transport, we need 
 * to initialize the socket first. Call this function.
 *
 *  @returns: return the state of the socket
 */
#ifdef WIN32
int o2_initWSock();
#endif

#ifndef O2_NO_DEBUGGING
#define SOCKET_DEBUG
#ifdef SOCKET_DEBUG
void o2_sockets_show();
#endif
#endif

process_info_ptr o2_add_new_socket(SOCKET sock, int tag, o2_socket_handler handler);

void o2_socket_remove(int i);

void o2_disable_sigpipe(SOCKET sock);

int o2_process_initialize(process_info_ptr info, int status, int hub_flag);

void o2_socket_mark_to_free(process_info_ptr info);

int o2_sockets_initialize();

int o2_make_tcp_recv_socket(int tag, int port, o2_socket_handler handler,
                            process_info_ptr *info);

int o2_make_udp_recv_socket(int tag, int *port, process_info_ptr *info);

int o2_osc_delegate_handler(SOCKET sock, process_info_ptr info);

void o2_free_deleted_sockets();

/**
 *  o2_recv will check all the set up sockets of the local process,
 *  including the udp socket and all the tcp sockets. The message will be
 *  dispatched to a matching method if one is found.
 *  Note: the recv will not set up new socket, as o2_discover will do that
 *  for the local process.
 *
 *  @return O2_SUCESS if succeed, O2_FAIL if not.
 */
int o2_recv();


int o2_tcp_initial_handler(SOCKET sock, process_info_ptr info);

int o2_osc_tcp_accept_handler(SOCKET sock, process_info_ptr info);

#endif /* o2_socket_h */
