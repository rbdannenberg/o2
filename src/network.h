/* network.h -- interface for network communication */
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

#ifdef WIN32
#include <winsock2.h> // define SOCKET, INVALID_SOCKET
#else
#include "arpa/inet.h"  // Headers for all inet functions
typedef int SOCKET;  // In O2, we'll use SOCKET to denote the type of a socket
#define INVALID_SOCKET -1
#endif
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

// messages are received in containers with a link so messages 
// may be queued in the application

typedef struct o2n_message {
    union {
        struct o2n_message *next; ///< link for application use
        int64_t pad_if_needed;    ///< make sure allocated is 8-byte aligned
    };
    int32_t length;              ///< length of message in data part
    ///< the message, be careful that compiler does not assume length == 4
    char data[4];
} o2n_message, *o2n_message_ptr;

// macro to make a byte pointer
#define PTR(addr) ((char *) (addr))

/// how many bytes are used by next and length fields before data
#define O2N_MESSAGE_EXTRA (((o2n_message_ptr) 0)->data - \
                            PTR(&((o2n_message_ptr) 0)->next))

/// how big should whole o2_message be to leave len bytes for the data part?
#define O2N_MESSAGE_SIZE_FROM_DATA_SIZE(len) ((len) + O2N_MESSAGE_EXTRA)
#define O2N_MESSAGE_ALLOC(len) \
        ((o2n_message_ptr) O2_MALLOC(O2N_MESSAGE_SIZE_FROM_DATA_SIZE(len)))

// net_tag values
#define NET_UDP_SERVER     60   // receives UDP messages
#define NET_TCP_SERVER     61   // server port for TCP connections
#define NET_TCP_CONNECTING 62   // client side socket during async connection
#define NET_TCP_CLIENT     63   // client side of a TCP connection
#define NET_TCP_CONNECTION 64   // server side accepted TCP connection
#define NET_INFO_CLOSED    65   // o2n_close_socket() has been called on this

typedef struct o2n_info {
    int net_tag;    // the type of socket: see list above
    int fds_index;  // the index of this object in the fds and fds_info arrays
    bool delete_me;  // set to true when socket should be removed (note that
                    //   removing array elements while scanning for events would
                    // be very tricky, so we make a second cleanup pass).
    bool raw_flag;  // if true, message data is sent as is with no length
                    // count (unless it is in the message data). Incoming
                    // bytes are formed into o2n_messages with length field
                    // and bytes, but there is no segmentation of the byte 
                    // stream as a sequence of alternating length fields and
                    // message payloads. Only meaningful for TCP since UDP
                    // connections are inherently packetized.
    int32_t in_length;    // incoming message length
    o2n_message_ptr in_message;  // message data from TCP stream goes here
    int in_length_got;    // how many bytes of length have been read?
    int in_msg_got;       // how many bytes of message have been read?
    
    o2n_message_ptr out_message; // list of pending output messages with
                                 //      data in network byte order
    int out_msg_sent;     // how many bytes of message have been sent?
    int port;       // used to save port number if this is a UDP receive socket,
                    // or the server port if this is a process
    void *application; // pointer to application-specific info if any
} o2n_info, *o2n_info_ptr;


typedef struct o2n_address {
    struct sockaddr_in sa; // address includes port number in network order
} o2n_address, *o2n_address_ptr;

int o2n_address_get_port(o2n_address_ptr address);
void o2n_address_set_port(o2n_address_ptr address, int port);

extern char o2n_local_ip[24];
extern bool o2n_found_network; // true if we have an IP address, which implies a
// network connection; if false, we only talk to 127.0.0.1 (localhost)

typedef int (*o2n_recv_callout_type)(o2n_info_ptr info);
typedef int (*o2n_accept_callout_type)(o2n_info_ptr info, o2n_info_ptr conn);
typedef int (*o2n_connected_callout_type)(o2n_info_ptr info);
typedef int (*o2n_close_callout_type)(o2n_info_ptr info);
extern o2n_info_ptr o2n_message_source; ///< socket info for current message

// initialize this module
int o2n_initialize(o2n_recv_callout_type recv, o2n_accept_callout_type acc,
              o2n_connected_callout_type conn, o2n_close_callout_type clos);

o2n_message_ptr o2n_message_new(int size);

    
const char *o2n_get_local_process_name(int port);


o2_err_t o2n_address_init(o2n_address_ptr remote_addr_ptr, const char *ip,
                     int port_num, bool tcp_flag);

void o2n_close_socket(o2n_info_ptr info);

// prepare to exit this module
void o2n_finish(void);

// free sockets that have been flagged as freed (sockets are normally not
//     freed immediately because doing so will cause other sockets to move
//     to a new position in fds and fds_info arrays, which is a problem if
//     you are iterating through the array)
// Since this is an O(N) search for deleted sockets, this function is only
//     called when o2n_socket_delete_flag is set, and it is only set when
//     a socket is marked for deletion.
void o2n_free_deleted_sockets(void);

// poll for messages
int o2n_recv(void);

o2n_info_ptr o2n_get_info(int i);

o2_err_t o2n_send_tcp(o2n_info_ptr info, int block, o2n_message_ptr msg);

o2_err_t o2n_send_udp(o2n_address_ptr ua, o2n_message_ptr msg);

o2_err_t o2n_send_udp_via_info(o2n_info_ptr info, o2n_address_ptr ua, o2n_message_ptr msg);

// send a UDP message to localhost
void o2n_send_udp_local(int port, o2n_message_ptr msg);

ssize_t o2n_send_broadcast(int port, o2n_message_ptr msg);

// create a socket for UDP broadcasting messages
o2_err_t o2n_broadcast_socket_new(SOCKET *sock);

// create a socket for sending UDP messages
o2_err_t o2n_udp_send_socket_new(SOCKET *sock);

o2n_info_ptr o2n_udp_server_new(int *port, void *application);

o2n_info_ptr o2n_tcp_server_new(int port, void *application);

o2n_info_ptr o2n_tcp_socket_new(int net_tag, int port, void *application);

void o2_disable_sigpipe(SOCKET sock);

// create a TCP connection to a server
//
o2n_info_ptr o2n_connect(const char *ip, int tcp_port, void * application);

// Take next step to send a message. If block is true, this call will 
//     block until all queued messages are sent or an error or closed
//     socket breaks the connection. If block is false, sending is 
//     asynchronous and only one step is taken, e.g. sending the next 
//     message in the queue. This function is normally used internally
//     without blocking. To avoid queuing up more than one, user-level
//     message, the o2_send() function will call this *with* blocking
//     when a message is already pending and o2_send is called again.
//
o2_err_t o2n_send(o2n_info_ptr info, int block);

// Send a message. Named "enqueue" to emphasize that this is asynchronous.
// Follow this call with o2n_send(info, true) to force a blocking
// (synchronous) send.
// msg must be in network byte order
//
int o2n_enqueue(o2n_info_ptr info, o2n_message_ptr msg);

#ifndef O2_NO_DEBUG
const char *o2n_tag_to_string(int tag);

const char *o2n_tag(int i);

int o2n_socket(int i);
#endif
