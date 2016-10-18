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
 * The fds_info structure tells us info about each socket. For Unix, there
 * is a parallel structure, fds, that contains an fds parameter for poll().
 *
 * In fds_info, we have the socket, a handler for the socket, and buffer info
 * to store incoming data, and the service the socket is attached to.
 */
#define UDP_SOCKET          0
#define TCP_SOCKET          1
#define OSC_SOCKET          2
#define DISCOVER_SOCKET     3
#define TCP_SERVER_SOCKET   4

struct process_info;

#ifdef WIN32
typedef struct ifaddrs
{
    struct ifaddrs  *ifa_next;    /* Next item in list */
    char            *ifa_name;    /* Name of interface */
    unsigned int     ifa_flags;   /* Flags from SIOCGIFFLAGS */
    struct sockaddr *ifa_addr;    /* Address of interface */
    struct sockaddr *ifa_netmask; /* Netmask of interface */
    union
    {
        struct sockaddr *ifu_broadaddr; /* Broadcast address of interface */
        struct sockaddr *ifu_dstaddr; /* Point-to-point destination address */
    } ifa_ifu;
#define              ifa_broadaddr ifa_ifu.ifu_broadaddr
#define              ifa_dstaddr   ifa_ifu.ifu_dstaddr
    void            *ifa_data;    /* Address-specific data */
} ifaddrs;


#endif

typedef struct fds_info {
    int tag;                    // UDP_SOCKET, TCP_SOCKET, OSC_SOCKET, DISCOVER_SOCKET,
    // TCP_SERVER_SOCKET
    //int port;                   // Record the port number of the socket.
    uint32_t length;            // message length
    o2_message_ptr message;     // message data from TCP stream goes here
    int length_got;             // how many bytes of length have been read?
    int message_got;            // how many bytes of message have been read?
    int (*handler)(SOCKET sock, struct fds_info *info); // handler for socket
    union {
        struct process_info *process_info;  // if not OSC
        char *osc_service_name;           // for incoming OSC port
    } u;
} fds_info, *fds_info_ptr;

extern char o2_local_ip[24];
extern int o2_local_tcp_port;
extern dyn_array o2_fds_info;
extern int o2_found_network; // true if we have an IP address, which implies a network connection
// if false, we only talk to 127.0.0.1 (localhost)


//#ifndef WIN32
extern dyn_array o2_fds;///< pre-constructed fds parameter for poll()
//#endif

/**
 *  In windows, before we want to use the socket to transport, we need to initialize
 *  the socket first. Call this function.
 *
 *  @returns: return the state of the socket
 */
#ifdef WIN32
int initWSock();
int getifaddrs(struct ifaddrs **ifpp);
void freeifaddrs(struct ifaddrs *ifp);
#endif

int init_sockets();
int make_udp_recv_socket(int tag, int port /* , int reuse_flag */);
// TODO: does process_info_ptr work?
int make_tcp_recv_socket(int tag, struct process_info *process);

/**
 *  When we get the raw data from the socket, we call this function. This function
 *  will first serialize the data into o2_message and then pass the message to
 *  the method handler according to the path in o2_message.
 *
 *  @param data The raw data of the message.
 *  @param size The size of the data
 *
 *  @return Return the size.
 */
int dispatch_data(void *data, size_t size);

/**
 *  When we finish our working or need to delete a service, call this function to
 *  free the service.
 *
 *  @param service_name The name of the service.
 */
void o2_service_free(const char *service_name);

/**
 *  Print the information of the application.
 *  Including IP, UDP port number, TCP server port number and all the services's
 *  names.
 */
void o2_application_info();


void o2_remove_socket(int i);

#endif /* o2_socket_h */
