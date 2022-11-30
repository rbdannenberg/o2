//* network.h -- interface for network communication */
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
#include <WS2tcpip.h>
#define inet_ntop InetNtop
typedef SSIZE_T ssize_t;
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
//
// When sending a message, options are "raw": send only length bytes
// starting at payload, or "default": send length + 4 bytes starting
// at length, but convert length field to network byte order and then
// restore the field. For UDP, the length is always assigned to the
// packet length, so only length bytes of payload are in the packet.

typedef struct O2netmsg {
    union {
        struct O2netmsg *next; ///< link for application use
        int64_t pad_if_needed;    ///< make sure allocated is 8-byte aligned
    };
    int32_t length;               ///< length of message in data part
    char payload[4];              ///< data
} O2netmsg, *O2netmsg_ptr;

// macro to make a byte pointer
#define PTR(addr) ((char *) (addr))

/// how many bytes are used by next and length fields before data
#define O2N_MESSAGE_EXTRA (offsetof(O2netmsg, payload))

/// how big should whole O2message be to leave len bytes for the data part?
#define O2N_MESSAGE_SIZE_FROM_DATA_SIZE(len) ((len) + O2N_MESSAGE_EXTRA)
#define O2N_MESSAGE_ALLOC(len) \
        ((O2netmsg_ptr) O2_MALLOC(O2N_MESSAGE_SIZE_FROM_DATA_SIZE(len)))

// net_tag values
// server socket to receive UDP messages: (0x200000)
#define NET_UDP_SERVER (O2TAG_HIGH << 1)

// server port for accepting TCP connections: (0x400000)
#define NET_TCP_SERVER (O2TAG_HIGH << 2)

// client side socket during async connection: (0x800000)
#define NET_TCP_CONNECTING (O2TAG_HIGH << 3)

// client side of a TCP connection: (0x1000000)
#define NET_TCP_CLIENT (O2TAG_HIGH << 4)

// server side accepted TCP connection: (0x2000000)
#define NET_TCP_CONNECTION (O2TAG_HIGH << 5)

// o2n_close_socket() has been called on this socket: (0x4000000)
#define NET_INFO_CLOSED (O2TAG_HIGH << 6)

// an input file for asynchronous reads -- treated as a socket (0x8000000)
#define NET_INFILE (O2TAG_HIGH << 7)

// Any open, sendable TCP socket (NET_TCP_SERVER is not actually sendable
// as a socket, but if we get the Proc_info that owns this, it is the 
// local process, and we can always "send" to the local process because
// we just find and invoke the local handler.
#define NET_TCP_MASK (NET_TCP_SERVER | NET_TCP_CLIENT | NET_TCP_CONNECTION)

class Net_address {
public:
    struct sockaddr_in sa; // address includes port number in network order

    O2err init(const char *ip, int port_num, bool tcp_flag);
    O2err init_hex(const char *ip, int port_num, bool tcp_flag);
    int get_port() { return ntohs(sa.sin_port); }
    void set_port(int port) { sa.sin_port = htons(port); }
    struct sockaddr *get_sockaddr() { return (struct sockaddr *) &sa; }
    struct in_addr *get_in_addr() { return &sa.sin_addr; }
    const char *to_dot(char *ip) {  // returns ip (or NULL on error)
        return inet_ntop(AF_INET, get_in_addr(), ip, O2N_IP_LEN); }
};

class Fds_info;  // forward declaration

class Net_interface {
public:
    Fds_info *fds_info;
    virtual O2err accepted(Fds_info *conn) = 0;
    virtual O2err connected() {
            printf("ERROR: connected called by mistake\n"); return O2_FAIL; }
    virtual O2err deliver(O2netmsg_ptr msg) = 0;

    // override writeable iff WRITE_CUSTOM:
    virtual O2err writeable() { return O2_SUCCESS; };

    // since Net_interface is a just an interface (set of methods), it is
    // always multiple-inherited along with some other class that you can
    // actually delete. This remove method converts "this" to the proper
    // class that provides the delete operator and invokes it.  It would be
    // nice if we could declare operator delete here and have it implemented
    // by the other class, but I don't think C++ works that way.
    virtual void remove() = 0;
};

typedef enum {READ_O2, READ_RAW, READ_CUSTOM} Read_type;
typedef enum {WRITE_O2, WRITE_CUSTOM} Write_type;

// an abstract class -- subclass with application-specific
//     message/event handlers
//
class Fds_info : public O2obj {
  public:
    int net_tag;    // the type of socket: see list above
    int fds_index;  // the index of this object in the fds and fds_info arrays
    int delete_me;  // set to 1 to request the socket should be removed 
                    // set to 2 when the socket is not blocked (is writable)
                    // (note that removing array elements while scanning for 
                    // events would be very tricky, so we make a second
                    // cleanup pass).

    Read_type read_type;  // READ_RAW means message data is sent as is with
                    // no length count (unless it is in the message data).
                    // Incoming bytes are formed into O2netmsgs with length
                    // field and bytes, but there is no segmentation of the
                    // byte stream as a sequence of alternating length fields
                    // and message payloads. Only meaningful for TCP since UDP
                    // connections are inherently packetized.
                    // READ_CUSTOM means the Net_interface deliver method is
                    // responsible for reading from the socket.
                    // READ_O2 means o2network.h reads length counts, buffers
                    // incoming messages until complete, and then delivers the
                    // message by calling Net_interface deliver method.

    Write_type write_type;  // WRITE_O2 means o2network.h delivers messages
                    // with length counts. Messages can be pending without
                    // blocking and O2 provides a way to block to avoid
                    // accumulating a long queue of outgoing messages.
                    // WRITE_CUSTOM means the Net_interface writeable method
                    // is called when the socket is writeable, and the 
                    // socket creator is responsible for setting events
                    // and writing to the socket. (Used for Avahi_info).

    int32_t in_length;    // incoming message length
    O2netmsg_ptr in_message;  // message data from TCP stream goes here
    int in_length_got;    // how many bytes of length have been read?
    int in_msg_got;       // how many bytes of message have been read?
    
    O2netmsg_ptr out_message; // list of pending output messages with
                                 //      data in network byte order
    int out_msg_sent;     // how many bytes of message have been sent?
    int port;       // used to save port number if this is a UDP receive socket,
                    // or the server port if this is a process
    Net_interface *owner;

    Fds_info(SOCKET sock, int net_tag, int port, Net_interface *own);
    ~Fds_info();

    void set_events(short events);
    short get_events();

    static Fds_info *create_tcp_client(const char *ip, int port,
                                       Net_interface *own);
    static Fds_info *create_tcp_client(Net_address *remote_addr,
                                       Net_interface *own);

    // create a UDP server port. Set reuse to true unless this is a discovery
    // port. We want discovery ports to be unique and not shared. Other
    // ports might be stuck in TIME_WAIT state, and setting reuse = true
    // might allow the process to reopen a recently used port.
    static Fds_info *create_udp_server(int *port, bool reuse);

    static Fds_info *create_tcp_server(int *port, Net_interface *own);
    O2err connect(const char *ip, int tcp_port);
    O2err can_send();
    O2err send_tcp(bool block, O2netmsg_ptr msg);

    // Send a message. Named "enqueue" to emphasize that this is asynchronous.
    // Follow this call with o2n_send(info, true) to force a blocking
    // (synchronous) send.
    // msg must be in network byte order
    void enqueue(O2netmsg_ptr msg);

    // Take next step to send a message. If block is true, this call will 
    // block until all queued messages are sent or an error or closed
    // socket breaks the connection. If block is false, sending is 
    // asynchronous and only one step is taken, e.g. sending the next 
    // message in the queue. This function is normally used internally
    // without blocking. To avoid queuing up more than one, user-level
    // message, the o2_send() function will call this *with* blocking
    // when a message is already pending and o2_send is called again.
    O2err send(bool block);

    int read_event_handler();
    O2err read_whole_message(SOCKET sock);
    void message_cleanup();
    Fds_info *cleanup(const char *error, SOCKET sock);
    void reset();
    void close_socket(bool now);

#ifndef O2_NO_DEBUG
// trace_socket_flag and TRACE_SOCKET allow us to trace sockets
// individually or according to type or function. Tracing all socket
// actions can be overwhelming.
#define TRACE_SOCKET(obj) ((obj)->trace_socket_flag)
    static const char *tag_to_string(int tag);
    bool trace_socket_flag;  // report when this closes
#else
#define TRACE_SOCKET(obj) false
#endif
    SOCKET get_socket();
};

extern Vec<Fds_info *> o2n_fds_info;

extern bool o2n_network_enabled;  // network connections are permitted
extern bool o2n_network_found;    // local area network exists
// if !o2n_network_found, o2n_internal_ip will be "7f000001" (localhost)
extern char o2n_public_ip[O2N_IP_LEN];     // in 8 hex characters
extern char o2n_internal_ip[O2N_IP_LEN];   // in 8 hex characters

// initialize this module
O2err o2n_initialize();

O2netmsg_ptr O2netmsg_new(int size);
    
// prepare to exit this module
void o2n_finish(void);

// free sockets that have been flagged as freed (sockets are normally not
// freed immediately because doing so will cause other sockets to move
// to a new position in fds and fds_info arrays, which is a problem if
// you are iterating through the array)
//
// Since this is an O(N) search for deleted sockets, this function is only
// called when o2n_socket_delete_flag is set, and it is only set when
// a socket is marked for deletion.
//
void o2n_free_deleted_sockets(void);

// poll for messages
O2err o2n_recv(void);

O2err o2n_send_udp(Net_address *ua, O2netmsg_ptr msg);

O2err o2n_send_udp_via_socket(SOCKET socket, Net_address *ua,
                              O2netmsg_ptr msg);

#define o2n_send_udp_via_info(info, ua, msg) \
    o2n_send_udp_via_socket(info->get_socket(), ua, msg);

// send a UDP message to localhost
void o2n_send_udp_local(int port, O2netmsg_ptr msg);

ssize_t o2n_send_broadcast(int port, O2netmsg_ptr msg);

// create a socket for UDP broadcasting messages
SOCKET o2n_broadcast_socket_new();

// create a socket for sending UDP messages
SOCKET o2n_udp_send_socket_new();

SOCKET o2n_tcp_socket_new();

void o2_disable_sigpipe(SOCKET sock);
