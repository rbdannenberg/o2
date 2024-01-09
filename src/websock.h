// websock.h -- websocket server for O2
//
// Roger Dannenberg
// based on websocketserver.srp by Hongbo Fang and Roger Dannenberg
// Feb 2020

/**
 * Websocket protocol:
 * All messages are O2 messages encoded as text.
 * Addresses offered by O2 hosts are:
 *    /_o2/ws/dy "s"  --  discovery with ensemble name. This must be the 
 *            initial message.
 *    /_o2/ws/sv "isiisi" -- service announcement. Parameters are
 *            service name, exists-flag, service-flag, 
 *            tapper-or-properties string, send_mode (for taps).
 *    /_o2/ws/cs/get "iis" -- clock request. Parameters are bridge_id,
 *            clock_sync_id (a message identifier), and a reply path.
 *    /_o2/ws/cs/cs "" -- send this when clock synchronization is obtained.
 *
 * Addresses handled by O2lite (browser client) side are:
 *    /_o2/id "i" -- This message confirms connection and assigns the 
 *            bridge_id, which is a unique number among all O2lite clients
 *            connected to the same O2 host.
 *
 * Messages are encoded as follows:
 *   <address> ETX <types> ETX <time> ETX <T/F> ETX
             [ <value> ETX ]*
 *   where <types> contains i (integer), f (float), d (double), 
 *           t (time), s (string)
 */

#ifndef O2_NO_WEBSOCKETS
class Http_conn;

// An Http_server serves web pages. O2 processes can be web servers to
// serve web pages to browsers, which can then connect to O2 using 
// Web Sockets. Thus, a browser-based interface or application using
// O2 can run without an additional server process. The O2 HTTP service
// is minimal and basically just for serving static pages and setting up
// a Web Socket connection.
//
// An Http_server is a subclass of Proxy_info, which normally is a proxy
// object that helps to forward O2 messages to a remote process offering
// a service. Here, there is no service name, and the "proxy" has the
// special tag O2TAG_HTTP_SERVER, so Proxy_info is used mainly as a socket
// that can accept TCP connections.
//
// Http_server holds the path to the root directory for web pages. The
// main function is accepted() which creates Http_conn object to manage
// each HTTP client connection to this server.
//
class Http_server: public Proxy_info {
public:
    const char *root;
    Http_server(int port, const char *root);
    virtual ~Http_server();

    // Implement the Net_interface:
    // do nothing, just start receiving messages:
    virtual O2err accepted(Fds_info *conn);
    virtual O2err deliver(O2netmsg_ptr msg) { return O2_FAIL; } // server
};


class Http_reader;

// Http_conn objects are created for each HTTP client connection.
// Because all O2 sockets and file reads are asynchronous, each HTTP GET
// request creates an Http_reader object. The reader list is serviced
// in order to serialize replies to the clients. Connections can be
// upgraded to Web Socket connections, which are also managed by this
// Http_conn class.
// 
class Http_conn: public Bridge_info {
public:
    const char *root;  // root of web pages
    int port; // server port
    Vec<char> inbuf;
    int inf;
    Http_reader *reader;
    // link used to create protocol's pending_ws_senders list, which is needed
    // to implement send_msg_later() which is called when sends are nested:
    // Http_conn::send() calls Http_conn::send_msg_later(), which appends the
    // O2 message to outgoing. The Http_conn is then inserted at the end of
    // o2ws_protocol->pending_ws_senders, using next_pending fields as links.
    // Then, on the next O2ws_protocol::bridge_poll(), we can traverse the
    // pending_ws_senders list and send all of their outgoing messages. It is
    // possible that messages may be queued again in the networks layer as
    // outgoing data to an (asynchronous) Web Socket.
    Http_conn *next_pending; // could probably use next instead for this list,
        // but next is an O2node* used for hash table collisions. Right now,
        // Http_conn is unnamed and not hashed, but it seems safer to reserve
        // next for hash table use.
    // websocket info:
    bool is_web_socket;
    bool sent_close_command;  // sent a CLOSE command
    bool confirmed_ensemble; // first message must be /_o2/ws/dy "ensemble"
    int maskx;  // index for masking key
    int ws_msg_len;
    O2message_ptr outgoing;  // pending outgoing messages: To send a message,
    // we parse it, and if we're already sending a message, we could already
    // be using the message parsing memory. In that case, we append the
    // message to this outgoing queue (a list). After each message is sent,
    // we sequentially send messages from this outgoing queue. They may end
    // up in another queue associated with the websocket's socket. These
    // will contain "raw" data ready for TCP send.

    Http_conn(Fds_info *conn, const char *root, int port);

    virtual ~Http_conn();

    O2err websocket_upgrade(const char *key, int msg_len);
    O2err process_as_web_socket();
    const char *find_field(const char *name, const char *value, int length);
    
    O2err ws_msg_is_complete(const char **error);
    O2err handle_websocket_msg(const char **error);
    O2err send_msg_later(O2message_ptr msg);

    virtual O2err close(); // close the connection
    virtual O2err accepted(Fds_info *conn) { return O2_FAIL; }  // not a server
    virtual O2err deliver(O2netmsg_ptr msg);
    virtual O2err send(bool block);
};


// file reader to asynchronously read web pages
#ifdef WIN32
class Http_reader: public O2obj {
#else
class Http_reader: public Proxy_info {
#endif
public:
    int port; // server port
    O2netmsg_ptr data;
    O2netmsg_ptr *last_ref;  // pointer to last msg in data, could be &data
                             // *last_ref is where async read puts the content
    long data_len;  // how much have we read?
    Fds_info *fds_info;  // file descriptor for file to be read
    Http_conn *conn;
#ifdef WIN32
    HANDLE inf; // handle for the opened file to be read
    Http_reader *next; // used to make a list of active readers
    OVERLAPPED overlapped;
    bool ready_for_read;
    void poll(); // poll method for async reads
#else
    aiocb cb;  // asynchronous read control block
#endif

    Http_reader(const char *c_path, Http_conn *connection, int port);

    ~Http_reader();

    virtual O2err accepted(Fds_info *conn) { return O2_FAIL; } // not a server
    virtual O2err deliver(O2netmsg_ptr msg);
    virtual O2netmsg_ptr prepare_new_read();
    virtual void read_operation_completed(int n);
    virtual O2err read_eof();

};
#endif
