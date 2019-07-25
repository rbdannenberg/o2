// o2_net.h -- interface for network communication
//
// Roger B. Dannenberg, 2019
//
// This module isolates low-level network communication from higher-level
// O2 protocols. The main abstraction here is asynchronous message passing
// over UDP and TCP. This abstraction layer handle asynchrony and 
// assembling messages.
//
// Uses o2n_ prefix to try to distinguish the "network" abstraction layer (o2n)
// from the "O2" abstraction layer (o2).

// The data structures are similar to the original O2 implementation:
// 2 parallel arrays: 
//     fds -- pollfd file descriptors
//     fds_info -- additional information/state
// Each fds_info object has an index so that the corresponding fds can be
//     retrieved. When a socket is removed, the last element of each
//     array is copied to the position that just opened up, and the index
//     is updated to the new location.
//
// On initialization, there is:
// - one TCP server socket to receive connections, asynchronous
// - one pre-allocated UDP broadcast socket, sends are synchronous
// - one pre-allocated UDP send socket, sends are synchronous

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

struct o2n_info;

/**
 * The o2n_info structure tells us info about each socket. For Unix, there
 * is a parallel structure, fds, that contains an fds parameter for poll().
 *
 * In o2n_info, we have the socket, a handler for the socket, and buffer
 * info to store incoming data, the state of outgoing data, and the service
 * the socket is attached to.
 * This structure is also used to represent a remote service if the
 * net_tag is NET_TCP_SOCKET.
 */

// tag values:
#define INFO_TCP_SERVER         20 // the local process
#define INFO_TCP_NOCLOCK        22 // not-synced client or server remote proc
#define INFO_TCP_SOCKET         23 // clock-synced client or server remote proc
#define INFO_UDP_SOCKET         24 // UDP receive socket for this process
#define INFO_OSC_UDP_SERVER     25 // provides an OSC-over-UDP service
#define INFO_OSC_TCP_SERVER     26 // provides an OSC-over-TCP service
#define INFO_OSC_TCP_CONNECTION 27 // an accepted OSC-over-TCP connection
#define INFO_OSC_TCP_CONNECTING 28 // OSC client socket during connection
#define INFO_OSC_TCP_CLIENT     29 // client side OSC-over-TCP socket
//#define NODE_BRIDGE_SERVICE    13   // this is a bridge to another protocol

// predicate to test if tag represents a TCP connection to a remote process
#define TAG_IS_REMOTE(tag) ((tag) == INFO_TCP_NOCLOCK || (tag) == INFO_TCP_SOCKET)

// net_tag values
#define NET_UDP_SOCKET     30   // receives UDP messages
#define NET_TCP_SERVER     31   // server port for TCP connections
#define NET_TCP_CONNECTING 32   // client side socket during async connection
#define NET_TCP_CLIENT     33   // client side of a TCP connection
#define NET_TCP_CONNECTION 34   // server side accepted TCP connection

/* Here are all the types of o2n_info structures and their life-cycles:

Local Process (tag = TCP_SERVER, net_tag = TCP_SERVER)
    Socket is created initially and only closed by o2n_finish.
UDP Broadcast Socket (just a socket for UDP, there is no o2n_info structure)
UDP Send Socket (another socket for UDP, there is no o2n_info structure)
UDP Receive Socket (tag = UDP_SOCKET, net_tag = UDP_SOCKET)
    Socket is created initially and only closed by o2n_finish, used for both
    discovery messages and incoming O2 UDP messages.
Remote Process (tag = INFO_TCP_SOCKET, net_tag = NET_TCP_CLIENT or NET_TCP_CONNECTION)
    1. Upon discovery, if we are the client, issue a connect request.
       tag                   net_tag               notes
       INFO_TCP_CONNECTING   NET_TCP_CONNECTING    waiting for connection
       INFO_TCP_NOMSGYET     NET_TCP_CLIENT        waiting for init msg
       INFO_TCP_NOCLOCK      NET_TCP_CLIENT        no clock sync yet
       INFO_TCP_SOCKET       NET_TCP_CLIENT        connected and synchronized
    2. If we accept a connection request from the server port
       tag                   net_tag               notes
       INFO_TCP_NOMSGYET     NET_TCP_CONNECTION    waiting for init msg
       INFO_TCP_NOCLOCK      NET_TCP_CONNECTION    no clock sync yet
       INFO_TCP_SOCKET       NET_TCP_CONNECTION    connected and synchronized
    3. If we discover remote process, but we are server, send dy message by UDP.
       No socket or o2n_info struct is created until the client contacts us.
       (See case 2 above.)
OSC UDP Server Port (tag = INFO_OSC_UDP_SERVER, net_tag = NET_UDP_SOCKET)
    This receives OSC messages via UDP.
OSC Over UDP Client Socket (tag = INFO_OSC_UDP_CLIENT)
    This is NOT a tag for o2n_info structure. It is for an osc_info structure.
OSC TCP Server Port (tag = INFO_OSC_TCP_SERVER, net_tag = NET_TCP_SERVER)
    This TCP server port is created to offer an OSC service over TCP.
OSC Over TCP Client Socket (tag = INFO_OSC_TCP_CLIENT, net_tag = NET_TCP_CLIENT)
       tag                     net_tag             notes
       INFO_OSC_TCP_CONNECTING NET_TCP_CONNECTING  waiting for connection
       INFO_OSC_TCP_CLIENT     NET_TCP_CLIENT      connected, ready to send
OSC TCP Socket (tag = INFO_OSC_TCP_CONNECTION, net_tag = NET_TCP_CONNECTION)
    This receives OSC messages via TCP. Accepted from OSC TCP Server Port.

 */

typedef int (*o2_socket_handler)(SOCKET sock, struct o2n_info *info);

typedef struct o2n_info {
    int tag;        // the purpose in O2, also distinguishes this from
                    //     a service_data struct.
    int net_tag;    // the type of socket: TCP_SERVER, UDP_SOCKET, TCP_CLIENT,
                    //     TCP_CONNECTION, TCP_CONNECTING
    int fds_index;  // the index of this object in the fds and fds_info arrays
    int delete_me;  // set to TRUE when socket should be removed (note that
                    //   removing array elements while scanning for events would
                    // be very tricky, so we make a second cleanup pass).
    int32_t in_length;             // incoming message length
    o2_message_ptr in_message;     // message data from TCP stream goes here
    int in_length_got;             // how many bytes of length have been read?
    int in_msg_got;                // how many bytes of message have been read?
    
    o2_message_ptr out_message;    // list of pending output messages with
                                   //      data in network byte order
    int out_msg_sent;              // how many bytes of message have been sent?
    int port;       // used to save port number if this is a UDP receive socket,
                    // or the server port if this is a process
    union {
        struct {
            // process name, e.g. "128.2.1.100:55765". This is used so that
            // when we add a service, we can enumerate all the processes and
            // send them updates. Updates are addressed using this name field.
            // Also, when a new process is connected, we send an /in message
            // to this name. name is "owned" by this process_info struct and
            // will be deleted when the struct is freed:
            o2string name; 
            // O2_HUB_REMOTE indicates this remote process is our hub
            // O2_I_AM_HUB means this remote process treats local process as hub
            // O2_NO_HUB means neither case is true
            int uses_hub;
            // these proc_service_data elements in services
            // describe services offered by this process, including
            // property strings for the services. Property strings are
            // owned by these elements, so free them if the element is
            // removed from this array. (see proc_service_data below)
            dyn_array services;
            // taps asserted by this process are of type proc_tap_data
            // (see below)
            dyn_array taps;
            SOCKET udp_port; // the incoming UDP port associated with process
            struct sockaddr_in udp_sa;  // address for sending UDP messages
        } proc;
        struct {                   // in the case of TCP, this name is created
            o2string service_name; // for the OSC_TCP_SERVER and is shared
        } osc;                     // by every accepted  OSC_TCP_SOCKET.
    };
} o2n_info, *o2n_info_ptr;

struct services_entry; // break mutual dependency with o2_search.h

typedef struct proc_service_data {
    struct services_entry *services; // entry for the service
    char *properties; // a property string, e.g. ";name:rbd;type:drummer"
} proc_service_data, *proc_service_data_ptr;


typedef struct proc_tap_data {
    struct services_entry *services; // entry for the tappee's service
    o2string tapper; // the tapper - owned by services, do not free
} proc_tap_data, *proc_tap_data_ptr;


extern struct sockaddr_in o2n_broadcast_to_addr;
extern SOCKET o2n_broadcast_sock;
extern SOCKET o2n_udp_send_sock;
extern char o2_local_ip[24];
extern int o2_local_tcp_port;
extern int o2_found_network; // true if we have an IP address, which implies a
// network connection; if false, we only talk to 127.0.0.1 (localhost)

extern int (*o2n_send_by_tcp)(o2n_info_ptr info);
extern int o2n_socket_delete_flag;
extern o2n_info_ptr o2_message_source; ///< socket info for current message

// initialize this module
int o2n_initialize();

// this sort of crosses abstraction layers between o2n and o2, but
// since o2n_info is declared here, the main initializer for it is too.
int o2_process_initialize(o2n_info_ptr proc, int hub_flag);

// prepare to exit this module
void o2n_finish();

void o2_socket_remove(int i);

void o2n_socket_mark_to_free(o2n_info_ptr info);

// free sockets that have been flagged as freed (sockets are normally not
//     freed immediately because doing so will cause other sockets to move
//     to a new position in fds and fds_info arrays, which is a problem if
//     you are iterating through the array)
// Since this is an O(N) search for deleted sockets, this function is only
//     called when o2n_socket_delete_flag is set, and it is only set when
//     a socket is marked for deletion.
void o2n_free_deleted_sockets();

// poll for messages
int o2n_recv();

// create a socket for UDP broadcasting messages
int o2n_broadcast_socket_new(SOCKET *sock);

// create a socket for sending UDP messages
int o2n_udp_send_socket_new(SOCKET *sock);

// create a socket that receives UDP
int o2n_udp_recv_socket_new(int tag, int *port);

// static int o2n_tcp_server_new(int tag, int *port);

int o2n_tcp_socket_new(int tag, int net_tag, int port);

void o2_disable_sigpipe(SOCKET sock);

// create a TCP connection to a server
//
int o2n_connect(const char *ip, int tcp_port, int tag);

// Take next step to send a message. If block is true, this call will 
//     block until all queued messages are sent or an error or closed
//     socket breaks the connection. If block is false, sending is 
//     asynchronous and only one step is taken, e.g. sending the next 
//     message in the queue. This function is normally used internally
//     without blocking. To avoid queuing up more than one, user-level
//     message, the o2_send() function will call this *with* blocking
//     when a message is already pending and o2_send is called again.
//
int o2n_send(o2n_info_ptr info, int block);


// Send a message. Named "enqueue" to emphasize that this is asynchronous.
// Follow this call with o2_net_send(info, msg, TRUE) to force a blocking
// (synchronous) send.
//
int o2n_enqueue(o2n_info_ptr info, o2_message_ptr msg);

// send a UDP message to localhost
void o2n_local_udp_send(char *msg, int len, int port);
