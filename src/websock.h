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

class Http_server: public Proxy_info {
public:
    long page_len;     // string length of page
    const char *root;
    Http_server(int port, const char *root);
    virtual ~Http_server();

    // Implement the Net_interface:
    // do nothing, just start receiving messages:
    virtual O2err accepted(Fds_info *conn);
    virtual O2err deliver(O2netmsg_ptr msg) { return O2_FAIL; } // server
};


class Http_reader;

class Http_conn: public Bridge_info {
public:
    const char *root;  // root of web pages
    int port; // server port
    Vec<char> inbuf;
    int inf;
    Http_reader *reader;
    // link used to create protocol's pending_ws_sender list:
    Http_conn *next_pending; // could probably use next instead for this list,
        // but next is an O2node* used for hash table collisions. Right now,
        // Http_conn is unnamed and not hashed, but it seems safer to reserve
        // next for hash table use.
    // websocket info:
    bool is_web_socket;
    bool confirmed_ensemble; // first message must be /_o2/ws/dy "ensemble"
    int maskx;  // index for masking key
    int payload_offset;  // state to help parse incoming data
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

    virtual O2err accepted(Fds_info *conn) { return O2_FAIL; }  // not a server
    virtual O2err deliver(O2netmsg_ptr msg);
    virtual O2err send(bool block);

};


// file reader to asynchronously read web pages
class Http_reader: public Proxy_info {
public:
    int port; // server port
    O2netmsg_ptr data;
    O2netmsg_ptr *last_ref;  // pointer to last msg in data, could be &data
    long data_len;  // how much have we read?
    Fds_info *fds_info;  // file descriptor for file to be read
    Http_conn *conn;
    aiocb cb;  // asynchronous read control block
    
    Http_reader(Fds_info *fds_info, Http_conn *connection, int port);

    ~Http_reader();

    virtual O2err accepted(Fds_info *conn) { return O2_FAIL; } // not a server
    virtual O2err deliver(O2netmsg_ptr msg);
};
#endif
