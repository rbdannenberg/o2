#ifndef O2_NO_WEBSOCKETS

// Enable verbose debug printing for websocket operation:
// #define O2WS_DEBUG

#include "o2internal.h"
#include <sys/types.h>
#include <fcntl.h>
#ifndef WIN32
#include <aio.h>
#include <unistd.h>
#endif
#include <errno.h>
#include <string.h>
#include "o2zcdisc.h"
#include "websock.h"
#include "o2sha1.h"
#include "pathtree.h"
#include "services.h"
#include "message.h"
#include "msgsend.h"
#include <inttypes.h>
#include "o2sha1.h"


#define WSOP_TEXT 1
#define WSOP_CLOSE 8
#define WSOP_PING 9
#define WSOP_PONG 10
#define WSBIT_FIN 128
#define WSBIT_MASK 128
#define ETX 3


#define ISA_O2WS(node) (ISA_BRIDGE(node) && \
                        ((Bridge_info *) node)->proto == o2ws_protocol)

#ifdef O2_NO_DEBUG
#define TO_O2WS(node) ((Http_conn *) (node))
#else
#define TO_O2WS(node) (assert(ISA_O2WS(node)), ((Http_conn *) (node)))
#endif


#ifndef __APPLE__
// linux and Windows do not define strnstr.
char *strnstr(const char *haystack, const char *needle, size_t len)
{
    while (len > 0) {
        int i = 0;
        while (needle[i] && i < len && needle[i] == haystack[i]) {
            i++;
        }
        if (needle[i] == 0) {
            return (char *) haystack;
        } else if (i == len) {
            return NULL;
        } // else needle[i] != haystack[i], so try next position
        len--;
        haystack++;
    }
    // in case len == 0 and needle is "":
    return (needle[0] == 0 ? (char *) haystack : NULL);
}
#endif


class O2ws_protocol : public Bridge_protocol {
public:
    Http_conn *pending_ws_senders;
#ifdef WIN32
    Http_reader *readers;  // list of file readers to poll for async read

    // insert reader to list so that it is polled to read file
    void add_reader(Http_reader *reader) {
        reader->next = readers;
        readers = reader;
    }

    // remove a reader from the list of readers to poll
    void remove_reader(Http_reader *reader) {
        Http_reader **reader_ptr = &readers;
        while (*reader_ptr) {
            if (*reader_ptr == reader) {
                *reader_ptr = reader->next;
                return;
            }
            reader_ptr = &(*reader_ptr)->next;
        }
    }
#endif
    O2ws_protocol() : Bridge_protocol("O2ws") {
        O2_DBw(printf("%s new O2ws_protocol %p\n", o2_debug_prefix, this));
       pending_ws_senders = NULL;
    }
    
    virtual ~O2ws_protocol() {
        O2_DBw(printf("%s: delete O2ws_protocol %p\n", o2_debug_prefix, this));
        o2_method_free("/_o2/o2ws"); // remove all o2ws support handlers
        pending_ws_senders = NULL;
    }
    

    // Insert an Http_conn object on o2ws_protocol->pending_ws_senders list.
    // If http_conn is already on the list do nothing. The list uses
    // Http_conn->next_pending as the link to the next list item.
    void insert_pending_ws_sender(Http_conn *http_conn) {
        Http_conn **ptr = &pending_ws_senders;
        while (*ptr) {
            if (*ptr == http_conn) {
                return;  // already on list
            }
            ptr = &((*ptr)->next_pending);
        }
        // reached end of list and did not find http_conn
        *ptr = http_conn; // append to list
        http_conn->next_pending = NULL;
    }

    void remove_pending_ws_sender(Http_conn *http_conn) {
        Http_conn **ptr = &pending_ws_senders;
        while (*ptr) {
            if (*ptr == http_conn) {
                *ptr = (*ptr)->next_pending;
                return;
            }
            ptr = &((*ptr)->next_pending);
        }
    }
    
    virtual O2err bridge_poll() {
        if (o2_do_not_reenter) return O2_FAIL;  // should never happen
        while (pending_ws_senders) {
            Http_conn *sender = pending_ws_senders;
            pending_ws_senders = pending_ws_senders->next_pending;
            while (sender->outgoing) {
                O2message_ptr msg = sender->outgoing;
                sender->outgoing = sender->outgoing->next;
                o2_prepare_to_deliver(msg);
                sender->send(false);
            }
        }
#ifdef WIN32
        for (Http_reader *reader = readers; reader; reader = reader->next) {
            reader->poll();
        }
#endif
        return O2_SUCCESS;
    }

};

O2ws_protocol *o2ws_protocol = NULL;



// Handler for !_o2/ws/dy message. This must be the first message and
// it must contain the correct ensemble name.
static void o2ws_dy_handler(O2msg_data_ptr msgdata, const char *types,
                            O2arg_ptr *argv, int argc, const void *user_data)
{
    O2_DBw(o2_dbg_msg("o2ws_dy_handler gets", NULL, msgdata, NULL, NULL));
    // get the arguments: ensemble
    const char *ens = argv[0]->s;
    Http_conn *http_conn = TO_O2WS(o2_message_source);
    if (!streql(ens, o2_ensemble_name)) {
        fprintf(stderr, "Warning: Websocket connection presented the wrong"
                "ensemble name (%s). Connection will be dropped.\n", ens);
        delete http_conn;
        o2_message_source = NULL;
    }
    http_conn->confirmed_ensemble = true;
    // successful connection, reply by granting Bridge ID
    o2_send_start();
    o2_add_int32(http_conn->id);
    O2message_ptr msg = o2_message_finish(0.0, "!_o2/id", true);
    O2_DBd(o2_dbg_msg("websocket_upgrade sending", msg, &msg->data,
                      NULL, NULL));
    o2_prepare_to_deliver(msg);
    O2err err = http_conn->send(false);
    if (err) {
        char errmsg[80];
        snprintf(errmsg, 80, "websocket_upgrade sending id %s",
                 o2_error_to_string(err));
        o2_drop_msg_data(errmsg, msgdata);
    }
}
    

// Handler for !_o2/ws/cs/get message. This is to get the time for
// a websocket client. Parameters are: id, sequence-number, reply-to
//
void o2ws_csget_handler(O2msg_data_ptr msgdata, const char *types,
                        O2arg_ptr *argv, int argc, const void *user_data)
{
    O2_DBk(o2_dbg_msg("o2ws_csget_handler gets", NULL, msgdata, NULL, NULL));
    // assumes websockets (http) is initialized, but it must be
    // because the handler is installed
    // ignore id: int id = argv[0]->i32;
    int seqno = argv[1]->i32;
    const char *replyto = argv[2]->s;
    o2_bridge_csget_handler(msgdata, seqno, replyto);
}


#ifdef O2_NO_WEBSOCKETS
O2err o2_http_initialize(int port, const char *root)
{
    sprintf(stderr, "HTTP/WebSockets are not enabled. "
                    "Recompile without O2_NO_WEBSOCKETS\n");
}

#else

Http_server *http_server = NULL;

O2err o2_http_initialize(int port, const char *root)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    o2ws_protocol = new O2ws_protocol();
    http_server = new Http_server(port, root);
    if (!http_server) return O2_FAIL;
    o2_method_new_internal("/_o2/ws/dy", "s", &o2ws_dy_handler,
                           NULL, false, true);
    o2_method_new_internal("/_o2/ws/sv", "siisi", &o2_bridge_sv_handler,
                           NULL, false, true);
    o2_method_new_internal("/_o2/ws/cs/get", "iis", &o2ws_csget_handler,
                           NULL, false, true);
    o2_method_new_internal("/_o2/ws/cs/cs", "", &o2_bridge_cscs_handler,
                           NULL, false, true);
    o2_method_new_internal("/_o2/ws/st", "s",
                           &o2_bridge_st_handler, NULL, false, true);
    o2_method_new_internal("/_o2/ws/ls", "",
                           &o2_bridge_ls_handler, NULL, false, true);
    return O2_SUCCESS;
}


Http_server::Http_server(int port, const char *root_) :
        Proxy_info(NULL, O2TAG_HTTP_SERVER)
{
    O2_DBw(printf("%s new Http_server %p\n", o2_debug_prefix, this));
    fds_info = Fds_info::create_tcp_server(&port, this);
    root = root_;
    // caller must check for fds_info and delete this object if NULL
    if (!root || root[0] == 0) {  // null or empty -- replace with "index.htm"
        root = "index.htm";
    } 
    root = o2_heapify(root);
    // remove "/" from end of root, if any
    size_t root_len = strlen(root);
    if (root[root_len - 1] == '/') {
        ((char *) root)[root_len - 1] = 0;
    }
    O2_DBw(printf("%s     server port %d root %s\n", o2_debug_prefix,
                  port, root));
    // register with zeroconf
#ifndef O2_NO_ZEROCONF
    o2_zc_register_record(port);
#endif
}


Http_server::~Http_server()
{
    O2_DBw(printf("%s: delete Http_server %p\n", o2_debug_prefix, this));
    // close all the client connections
    int n = o2n_fds_info.size();
    for (int i = 0; i < n; i++) {
        Http_conn *http = (Http_conn *) o2n_fds_info[i]->owner;
        if (http && ISA_O2WS(http)) {  // only remove an http client
            delete http;
        }
    }
    O2_FREE((char *) root);
}


O2err Http_server::accepted(Fds_info *conn)
{
    assert(tag == O2TAG_HTTP_SERVER);
    Http_conn *info = new Http_conn(conn, root, fds_info->port);
    conn->owner = info;
    return O2_SUCCESS;
}


Http_conn::Http_conn(Fds_info *conn, const char *root_, int port_) :
        Bridge_info(o2ws_protocol)
{
    O2_DBw(printf("%s: new Http_conn %p\n", o2_debug_prefix, this));
    root = root_;
    port = port_;
    reader = NULL;
    next_pending = NULL;
    is_web_socket = false;
    sent_close_command = false;
    confirmed_ensemble = false;
    fds_info = conn;
    fds_info->read_type = READ_RAW;
    outgoing = NULL;
}


// close a connection
O2err Http_conn::close()
{
    if (is_web_socket && !sent_close_command) { // send WSOP_CLOSE message
        O2netmsg_ptr o2netmsg = O2N_MESSAGE_ALLOC(32);  // the most we need
        if (!o2netmsg) {
            return O2_NO_MEMORY;
        }
        int heading_len = 2;
        int CLOSE_STATUS = htons(1001);
        o2netmsg->payload[0] = (char) (WSBIT_FIN | WSOP_CLOSE);
        o2netmsg->payload[1] = 19;
        memcpy(o2netmsg->payload + 2, &CLOSE_STATUS, 2);
        memcpy(o2netmsg->payload + 4, "O2 server shutdown", 19);
        o2netmsg->length = 4 + 19;
        fds_info->send_tcp(false, o2netmsg);
    }
    return O2_SUCCESS;
}


Http_conn::~Http_conn()
{
    if (!this) return;
    O2_DBw(printf("%s: delete Http_conn %p, is_web_socket %d sent_close_"
                  "command %d\n", o2_debug_prefix, this, is_web_socket,
                  sent_close_command));
    // even though we may have sent a CLOSE command, we do not wait for it
    // to be sent in cases where sends are pending. If the CLOSE was sent,
    // we DO wait for the asynchronous command to complete.
    
    // if we have a pending send, remove it:
    ((O2ws_protocol *) proto)->remove_pending_ws_sender(this);
    // remove all pending send
    o2_message_list_free(&outgoing);
    // unlink and free data we have not processed
    delete_fds_info();
    if (reader) {
        delete reader;
        reader = NULL;
    }
}


// find a sequence matching name: *value*\r in msg
// If value is a string, return pointer to value if found
// If value is NULL, return pointer to where value begins if name is found
//    and value is present (non empty).
// Otherwise, return NULL
//
// Limitations: find_field does not check that value matches a whole item
// in a comma-separated list, e.g. "e, U" will match " keep-alive, Upgrade"
// or maybe worse, "Upgrade" will match " keep-alive, DoNotUpgrade" but we
// only look for "Upgrade" and "websocket" on under their respective
// attributes, and we check for a complete match to attribute names, e.g.
// "\r\nConnection: ", so it seems unlikely this will cause any problem.
//
const char *Http_conn::find_field(const char *name, const char *value, 
                                  int length)
{
    // since strnstr is not always defined, we'll temporarily put in an EOS
    // to mark the end of the request. But the request might not be followed
    // by any bytes, so we'll pad with an EOS (possibly forcing reallocation)
    // to avoid the edge case where there is no space for EOS
    const char *result = NULL;
    inbuf.push_back(0);
    char save_byte_after_end = inbuf[length];
    inbuf[length] = 0;  // temporary EOS
    const char *request = &inbuf[0];
    const char *start = strstr(request, name);
    if (start) {
        start = start + strlen(name);  // beginning of value string
        if (*start != '\r') {  // must be non-empty in case value is NULL
            if (!value || strstr(start, value)) {
                result = start;
            }
        }            
    }
    // restore saved byte
    inbuf[length] = save_byte_after_end;
    inbuf.pop_back();  // remove added EOS
    return result;
}


O2err Http_conn::websocket_upgrade(const char *key, int msg_len)
{
    char sha1[32];
    sha1_with_magic(sha1, key);
    O2netmsg_ptr msg = O2N_MESSAGE_ALLOC(512);
    snprintf(msg->payload, 512, "HTTP/1.1 101 Switching Protocols\r\n"
             "Upgrade: websocket\r\nConnection: Upgrade\r\n"
             "Sec-WebSocket-Accept: %s\r\n\r\n", sha1);
    msg->length = (int) strlen(msg->payload);
    fds_info->send_tcp(false, msg);
    ws_msg_len = -1;  // unknown
    inbuf.drop_front(msg_len);
    is_web_socket = true;
    confirmed_ensemble = false;  // connection must send correct ensemble name
    outgoing = NULL;
    return O2_SUCCESS;
}

// Check if we have a complete websocket message starting at msg->payload
// Returns: O2_SUCCESS if true, O2_FAIL if not yet, O2_INVALID_MSG if 
// something goes wrong and connection should be closed (the only case is
// that the websocket message is too long -- we have a 512 byte limit.
//
O2err Http_conn::ws_msg_is_complete(const char **error)
{
    // printf("CALLED ws_msg_is_complete with %d\n", inbuf.size());
    // we are upgraded. inbuf is all the data so far.
    int inbuf_size = inbuf.size();
    
    if (ws_msg_len >= 0 && ws_msg_len <= inbuf_size) {
        return O2_SUCCESS;  // previously we computed length, now we have it
    }
    // see if we can compute the number of bytes we need from data so far
    if (inbuf_size < 6) {  // need at least 2 bytes + masking key
        return O2_FAIL;
    }
    const char *p = &inbuf[0];
    int mask_load = p[1];  // MASK and Payload len in byte 1
    // get the payload length
    int payload_length = (mask_load & 127);
    maskx = 2;  // index of masking key, saved as instance variable for later
    if (payload_length == 126) {  // next 2 bytes are 16-bit payload length
        if (inbuf_size < 134) {  // must have at least 8 + 126 bytes in message
            return O2_FAIL;
        }
        payload_length = (p[2] << 8) + p[3];
        maskx = 4;
    } else if (payload_length == 127) {
        // code for getting long lengths is commented here; instead, we
        // just report an error so caller will close the connection
        *error = "Websocket message exceeds server's length limitation.";
        return O2_INVALID_MSG;
    }
    ws_msg_len = payload_length + maskx + 4;
    /*
    if (ws_msg_len <= inbuf_size)
        printf("GOT WS MSG LEN %d, payload %d, OF %d\n",
               ws_msg_len, payload_length, inbuf_size);
    else printf("   WAITING FOR WS MSG, %d SO FAR\n", inbuf_size);
     */
    return (ws_msg_len <= inbuf_size ? O2_SUCCESS : O2_FAIL);
}


static int ws_msg_parse(char *payload, int plen,
                        const char *fields[], int flen)
{
    if (payload[plen - 1] != ETX) {
        return O2_INVALID_MSG;
    }
    int px = 0;
    int fx = 0;
    while (px < plen) {
        fields[fx++] = payload + px;
        if (fx > flen) return O2_INVALID_MSG;
        while (payload[px] != ETX) px++;  // skip to next ETX
        payload[px++] = 0;  // separate fields with EOS
    }
    return fx;
}


O2err Http_conn::handle_websocket_msg(const char **error)
{
    // preconditions:
    //    ws_msg_is_complete() returned O2_SUCCESS
    //    the message data is all in msg, which may have extra bytes
    //    ws_msg_len is the length of the websocket message
    // postconditions:
    //    first message is parsed and delivered
    //    message data is removed from inbuf
    //    inbuf is either full or inbuf->next is NULL
    char *msg = &inbuf[0];
    int header = msg[0];
    
    const char *address;
    const char *types;
    O2time time;
    bool tcp_flag;
    int fx;
    O2message_ptr to_send;

    // FIN should be 1 indicating the entire message is in this fragment
    if (!(header & WSBIT_FIN)) {
        *error = "Websocket message fragments not implemented.";
        return O2_INVALID_MSG;
    }
    // Opcode should indicate a text frame or close
    int opcode = (header & 15);
    if (opcode != WSOP_TEXT && opcode != WSOP_PING && opcode != WSOP_CLOSE) {
        *error = "Websocket opcode was neither CLOSE, TEXT, nor PING.";
        return O2_INVALID_MSG;
    }
    int mask_load = msg[1];  // MASK and Payload len in byte 1
    if ((mask_load & WSBIT_MASK) == 0) {
        *error = "Websocket MASK must be 1.";
        return O2_INVALID_MSG;
    }
    // printf("    OPCODE %s\n", (opcode == WSOP_TEXT ? "TEXT" : "PING"));

    // Already got the payload length and maskx from ws_msg_is_complete()
    // Make the unmasked payload.
    char *mask = msg + maskx;
    int payloadx = maskx + 4;
    char *payload = msg + payloadx;
    int payload_len = ws_msg_len - payloadx;
    assert(payload_len + payload - msg <= inbuf.size());

    /*
    printf("BEFORE UNMASK %d bytes: ", payload_len);
    for (int i = 0; i < payload_len; i++) {
        printf("%02x ", (uint8_t) payload[i]);
    }
    printf("\n");
     */
    
    for (int i = 0; i < payload_len; i++) {
        payload[i] ^= mask[i & 3];  // mask[i % 4]
    }
    
    /*
    printf("UNMASKED %d bytes: ", payload_len);
    for (int i = 0; i < payload_len; i++) {
        putchar(payload[i]);
    }
    printf("\n");
     */
    
    // now payload is an unencoded payload as a string of length payload_len
    // with no EOS
    /*
    printf("    Websocket payload: ");
    for (int i = 0; i < payload_len; i++)
        if (payload[i] == ETX) putchar('|'); else putchar(payload[i]);
    putchar('\n');
     */
    if (opcode == WSOP_PING || (opcode == WSOP_CLOSE && !sent_close_command)) {
        // send payload back
        // we can only handle a PING or CLOSE if payload length less than 126
        if (payload_len < 126) {
            O2netmsg *reply = O2N_MESSAGE_ALLOC(payload_len + 2);
            reply->payload[0] = WSBIT_FIN |
                                (opcode == WSOP_PING ? WSOP_PONG : WSOP_CLOSE);
            reply->payload[1] = payload_len;
            memcpy(reply->payload + 2, payload, payload_len);
            reply->length = payload_len + 2;
            fds_info->send_tcp(false, reply);
            O2_DBw(printf("%s: Sent %s back to client\n", o2_debug_prefix,
                          (opcode == WSOP_PING ? "PONG" : "CLOSE")));
            inbuf.drop_front((int) (payload + payload_len - msg));
            ws_msg_len = -1;
            if (opcode == WSOP_CLOSE) {
                sent_close_command = true;
                return O2_FAIL;  // this will close the socket
            } else {
                return O2_SUCCESS;
            }
        } else {
            // otherwise we skip it -- maybe client will hang up, hope not
            // if so, the fix is probably to support longer Pong messages
            O2_DBw(printf("%s: websocket got opcode %d but payload_len %d "
                          "is too long.\n", o2_debug_prefix, opcode,
                          payload_len));
            return O2_SUCCESS;
        }
    } else if (opcode == WSOP_CLOSE) { // already sent close command
        return O2_FAIL; // this will close the socket
    }

    const char *fields[32];
    int flen = ws_msg_parse(payload, payload_len, fields, 32);
    if (flen < 4) {        // need 4 fields: addr, types, time, TCP flag.
        goto bad_message;  // Exit if less than 4 or O2_INVALID_MSG or O2_FAIL.
    }
    o2_send_start();
    address = fields[0];
    types = fields[1];
    time = atof(fields[2]);
    tcp_flag = (fields[3][0] == 'T');
    fx = 4;
    while (*types) {
        if (fx >= flen) goto bad_message;
        const char *field = fields[fx];
        switch (*types) {
            case 'i': o2_add_int32(atoi(field)); break;
            case 'f': o2_add_float((float) atof(field)); break;
            case 'd': o2_add_double(atof(field)); break;
            case 't': o2_add_time(atof(field)); break;
            case 's': 
                // anything that's not allowed is already a string. We could
                // refuse to send anything, but that would make messages
                // disappear without explanation, so instead we send the
                // field as a string -- maybe receiver or a debugging tool 
                // will enable the user to figure out what happened.
            default: o2_add_string(field); break;
        }
        types++;
        fx++;
    }
    if (!confirmed_ensemble) {  // first message can only be /_o2/ws/dy:
        if (!streql(address + 1, "_o2/ws/dy")) {  // refuse connection
            fprintf(stderr, "Warning: Refusing Websocket message forwarding "
                    "until /_o2/ws/dy <ensemble> is received.\n");
            return O2_FAIL;
        }
    }
    O2_DBw(printf("%s websocket bridge incoming %s @ %g (%c): ",
                  o2_debug_prefix, address, time, tcp_flag ? 'T' : 'F');
           for (int i = 4; i < flen; i++) printf(" %s", fields[i]);
           putchar('\n');)
    o2_message_source = this;
    // finish building message before we destroy the source in inbuf
    to_send = o2_message_finish(time, address, tcp_flag);

    /*
    printf("BEFORE drop_front, %d left:", inbuf.size());
    for (int i = 0; i < inbuf.size(); i++) printf(" %02x", (uint8_t) inbuf[i]);
    printf("\n");
     */
    
    inbuf.drop_front((int) (payload + payload_len - msg));  // shift the input stream
    
    /*
    printf("AFTER drop_front, %d left:", inbuf.size());
    for (int i = 0; i < inbuf.size(); i++) printf(" %02x", (uint8_t) inbuf[i]);
    printf("\n");
    */

    ws_msg_len = -1;
    o2_message_send(to_send);  // this may return an error such as O2_NO_SERVICE
    return O2_SUCCESS;  // but we still report success to avoid closing websocket
  bad_message:
    O2_DBw(printf("%s websocket bridge bad_message\n", o2_debug_prefix));
    // now we need to remove the message from inbuf
    inbuf.drop_front((int) (payload + payload_len - msg));
    ws_msg_len = -1;
    return O2_INVALID_MSG;
}


O2err Http_conn::send_msg_later(O2message_ptr msg)
{
    // insert message at end of queue; normally queue is very short
    O2message_ptr *pending = &outgoing;
    while (*pending) pending = &(*pending)->next;
    // now *pending is where to put the new message
    *pending = o2_postpone_delivery();
    o2ws_protocol->insert_pending_ws_sender(this);
    return O2_SUCCESS;
}

static void print_websocket_data(const char *wsmsg)
{
    printf("SENDING ");
    while (*wsmsg) {
        if (*wsmsg == ETX) printf(" | ");
        else printf("%c", *wsmsg);
        wsmsg++;
    }
    printf("\n");
}

// As a proxy, deliver a message to the remote host. This can only be a
// websocket message. Ordinary HTTP GET responses are generated and sent
// by HTTP_reader.
//
O2err Http_conn::send(bool block)
{
    // normally, a proxy calls pre_send, but presend does byte swapping
    O2message_ptr msg = o2_current_message();
    // TODO: I think this is a mistake: if (!msg) return O2_NO_SERVICE;
    // create websocket message using O2 message parsing/construction areas
    if (o2_do_not_reenter) {
        return send_msg_later(msg);
    }
    // handle tap sending while we have msg
    /* O2err taperr = */ send_to_taps(msg);

    O2_DBw(o2_dbg_msg("websock bridge outgoing", msg, &msg->data, NULL, NULL));
    o2_extract_start(&msg->data);  // prepare to extract parameters
    assert(!o2_ctx->building_message_lock);
    o2_send_start();        // prepare space to build websocket message
    // <address> ETX <types> ETX <time> ETX <T/F> ETX [<value>ETX]*
    o2_ctx->msg_data.append(msg->data.address, (int) strlen(msg->data.address));
    o2_ctx->msg_data.push_back(ETX);
    O2_DBw(printf("just the address field: ");
           o2_ctx->msg_data.push_back(0);
           print_websocket_data(&o2_ctx->msg_data[0]);
           o2_ctx->msg_data.pop_back());
    const char *types = o2_msg_types(msg);
    o2_ctx->msg_data.append(types, (int) strlen(types));
    o2_ctx->msg_data.push_back(ETX);
    char timestr[32];
    sprintf((char *) timestr, "%.3f", msg->data.timestamp);
    // Remove extra zeros. Is there a better way to do this?
    int len = (int) strlen(timestr);
    while (timestr[len - 1]  == '0') len--;  // remove trailing zeros
    if (timestr[len - 1] == '.') len--;      // remove trailing decimal point
    o2_ctx->msg_data.append(timestr, len);
    o2_ctx->msg_data.push_back(ETX);
    o2_ctx->msg_data.push_back((msg->data.misc & O2_TCP_FLAG) ? 'T' : 'F');
    o2_ctx->msg_data.push_back(ETX);
    // append all the parameters encoded to ASCII (strings are unicode)
    O2type typecode;
    while ((typecode = (O2type) *types++)) {
        switch (typecode) {
          case O2_INT32:
            sprintf(timestr, "%d\003", o2_get_next(typecode)->i);
            break;
          case O2_INT64:
            sprintf(timestr, "%" PRId64 "\003", o2_get_next(typecode)->h);
            break;
          case O2_FLOAT:
            sprintf(timestr, "%g\003", o2_get_next(typecode)->f);
            break;
          case O2_DOUBLE:
            sprintf(timestr, "%g\003", o2_get_next(typecode)->d);
            break;
          case O2_TIME:
            sprintf(timestr, "%.3f\003", o2_get_next(typecode)->t);
            break;
          case O2_SYMBOL:
          case O2_STRING: {
            const char *str = o2_get_next(typecode)->s;
            // directly copy str to msg_data because str might be long
            o2_ctx->msg_data.append(str, (int) strlen(str));
            timestr[0] = 3; timestr[1] = 0;
            break;
          }
          default:  // send a field with ? -- maybe it will be easier to debug
            sprintf(timestr, "?\003");  // than just dropping the message
            break;
        }
        o2_ctx->msg_data.append(timestr, (int) strlen(timestr));
    }
    o2_complete_delivery();  // we're done with msg now
    const char *wsmsg = &o2_ctx->msg_data[0];
    len = o2_ctx->msg_data.size();
    if (len >= 0xffff) {
        return O2_FAIL;  // too big
    }
    O2_DBw(o2_ctx->msg_data.push_back(0);
           print_websocket_data(wsmsg);
           o2_ctx->msg_data.pop_back(););
    O2netmsg_ptr o2netmsg = O2N_MESSAGE_ALLOC(len + 4);  // the most we need
    if (!o2netmsg) {
        o2_ctx->building_message_lock = false;
        return O2_FAIL;  // failed to allocate message
    }
    int heading_len = 2;
    o2netmsg->payload[0] = (char) (WSBIT_FIN | WSOP_TEXT);
    if (len < 126) {
        // to deliver, we need to copy to an O2netmsg_ptr
        o2netmsg->payload[1] = len;
    } else if (len < 0xffff) {  // length is 126 to 16-bits
        heading_len = 4;
        o2netmsg->payload[0] = (char) (WSBIT_FIN | WSOP_TEXT);
        o2netmsg->payload[1] = 126;
        o2netmsg->payload[2] = len >> 8;
        o2netmsg->payload[3] = len;
    }
    o2netmsg->length = len + heading_len;
    memcpy(o2netmsg->payload + heading_len, wsmsg, len);
    o2_ctx->building_message_lock = false;
    return fds_info->send_tcp(false, o2netmsg);
}


O2err Http_conn::deliver(O2netmsg_ptr msg)
{
    const char *response = "400 Bad Request";
    const char *text = "The URL path was too long or malformed.";
    const char *text2 = "";
    char *msg_end;
    int msg_len;
    const char *sec_web_key;
    Vec<char> path;
    int backup;

    O2_DBw(o2_print_bytes("Http_conn::deliver bytes:",
                          msg->payload, msg->length));
    
    // get the incoming request in a contiguous array we can search.
    // keep the old size to speed up searching for \r\n\r\n:
    int prev_index = inbuf.size();
    inbuf.append(msg->payload, msg->length);
    O2_FREE(msg);

    if (is_web_socket) {
        // since maximum length of accepted websocket messages fits in one
        // 512-byte msg payload, we now have at most 2 messages in a list
        // (inbuf), and first is the first message we received (last on 
        // the list). first is where we look for a websocket message.
        O2err rslt;
        while ((rslt = ws_msg_is_complete(&text)) == O2_SUCCESS) {
            if (rslt == O2_FAIL) return O2_SUCCESS;  // wait for another msg
            else if (rslt == O2_INVALID_MSG) {
                // TODO: we really should send a close frame with code 1009 here
                fds_info->close_socket(true);
                return O2_SUCCESS;
            } else {
                O2err err = handle_websocket_msg(&text);
                if (err) {
                    /* When this code jumps to report_error, it outputs HTML,
                     * but this seems wrong because we upgraded to a websocket.
                     * I think it is better to simply close the connection.
                    text = "Websocket or protocol error: ";
                    text2 = o2_error_to_string(err);
                    goto report_error;
                     */
                    // here, we can either return an error and have o2network
                    // close the connection abruptly, or we can close it
                    // gently ourselves (ie send pending messages which might
                    // send a CLOSE command) and return O2_SUCCESS so that
                    // o2network does not attempt another close() operation.
                    fds_info->close_socket(false); // allow CLOSE msg to send
                    return O2_SUCCESS; // return O2_ERROR would close socket
                }
            }                    
        }
        return O2_SUCCESS;  // wait for a complete message
    }

    // is there \r\n\r\n somewhere now? Worst case is we just received
    // the last \n, so start searching 3 from the end if possible:
    backup = prev_index;
    if (backup > 3) backup = 3;
    prev_index -= backup;
    // search length is current end - prev_end
    msg_end = strnstr(&inbuf[prev_index], "\r\n\r\n",
                      inbuf.size() - prev_index);
    if (!msg_end) {  // we do not have a complete request
        return O2_SUCCESS; // wait for the rest of the get request
    }
    msg_end += 4;  // the real end is after \r\n\r\n
    msg_len = (int) (msg_end - &inbuf[0]);
    
    O2_DBw(printf("Got %d-byte header: <<", msg_len);
           for (int i = 0; i < msg_len; i++) {
               putchar(inbuf[i]);
               if (inbuf[i] == '\n') printf("    "); // indent
           }
           printf(">>\n"));
    
    // parse the request:
    sec_web_key = NULL;
    if (find_field("\r\nConnection: ", "Upgrade", msg_len) &&
        find_field("\r\nUpgrade: ", "websocket", msg_len) &&
        (sec_web_key = find_field("\r\nSec-WebSocket-Key: ", NULL, msg_len))) {
        // "\r" must exist because find_field found it already
        const char *end = strchr(sec_web_key, '\r');
        // terminate the string:
        *((char *) end) = 0;  // (modifies the request header!)
        return websocket_upgrade(sec_web_key, msg_len);
    } else if (strncmp(&inbuf[0], "GET /", 5) == 0) {
        // find full path to open
        // make sure there is a terminating space
        inbuf.push_back(' ');
        const char *path_end = strchr(&inbuf[4], ' ');
        inbuf.pop_back();
        int root_len = (int) strlen(root);
        path.append(root, root_len);
        int path_len = (int) (path_end - &inbuf[4]);
        path.append(&inbuf[4], path_len);
        if (path.last() == '/') {
            path.append("index.htm", 9);
        }
        path.push_back(0);
        const char *c_path = &path[0];  // c-string (address) of path
        O2_DBw(printf("%s: HTTP GET, path=%s obj %p", o2_debug_prefix,
                      c_path, this));
        // prevent requests from looking outside of the root directory
        if (strstr(c_path, "..") == NULL) {
            // TODO: inf should be in Http_reader, but it is in Http_conn
            reader = new Http_reader(c_path, this, port);
            if (inf < 0) {
                response = "404 Not Found";
                text = "The requested URL was not found: ";
                text2 = c_path + root_len + 1;
                delete reader;
                reader = NULL;
            } else {
                O2_DBw(printf("\n"));
                inbuf.drop_front(msg_len);
                return O2_SUCCESS;
            }
        } else {
            O2_DBw(printf(" - rejected, path contains \"..\"\n"));
        }
    }
    inbuf.drop_front(msg_len);
  // report_error:
    char content[300];
    // content len is < 150 + path, so path < 150 
    snprintf(content, 256, "<html><head><title>%s</title></head>"
             "<body><h1>Error</h1><p>%s%s</p></body></html>\r\n", 
             response, text, text2);
    int content_len = int(strlen(content));
    msg = O2N_MESSAGE_ALLOC(content_len + 150);
    // 512 is plenty big, so we reuse msg->payload to write the message
    // length is 119 + response + content_len < 200, so content < 312
    sprintf(msg->payload, "<HTTP/1.1 %s \r\nServer: O2 Http_server\r\n"
            "Content-Length: %ld\r\nContent-Type: text/html\r\n"
            "Connection: Closed\r\n\r\n%s",
            response, (long) strlen(content), content);
    int payload_len = (int) strlen(msg->payload);
    assert(payload_len <= content_len + 150);  // confirm we allocated enough
    msg->length = payload_len;
    fds_info->send_tcp(false, msg);
    O2_DBw(printf("%s: closing web socket: %s%s\n", o2_debug_prefix,
                  text, text2));
    fds_info->close_socket(false);
    return O2_SUCCESS;
}

#define HTTP_FILE_READ_SIZE 512
#ifdef WIN32
Http_reader::Http_reader(const char* c_path, Http_conn* connection, int port_)
#else
Http_reader::Http_reader(const char *c_path, Http_conn *connection,
                         int port_) : Proxy_info(NULL, O2TAG_HTTP_READER)
#endif
{
    conn = NULL; // needed by delete if file open fails
    printf("HTTP GET %s\n", c_path);
    data = NULL;
    last_ref = &data;
    port = port_;
    data_len = 0;
#ifdef WIN32
    ready_for_read = false;  // disable reads until file is open
    connection->inf = -1;
    next = NULL;
    inf = CreateFileA(c_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                      OPEN_EXISTING, FILE_FLAG_OVERLAPPED | 
                      FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (inf == INVALID_HANDLE_VALUE) {
        printf("    -> file not found\n");
    } else {
        connection->inf = 0;  // means read is in progress
        memset(&overlapped, 0, sizeof(overlapped));
        ready_for_read = true;  // tells poll to call ReadFile
        // rely on poll() to read at most one block per polling period
        o2ws_protocol->add_reader(this);
#else
    // open the file
    connection->inf = open(c_path, O_RDONLY | O_NONBLOCK, 0);
    if (connection->inf < 0) {
        printf("    -> file not found\n");
    } else {
#endif
        conn = connection;
#ifndef WIN32
        fds_info = new Fds_info(connection->inf, NET_INFILE, 0, NULL);
        fds_info->read_type = READ_CUSTOM;
        fds_info->owner = this;
#endif
    }
}

#ifdef WIN32
void Http_reader::poll()
{
    bool ro_completed = false;
    DWORD len = 0;
    assert(inf != INVALID_HANDLE_VALUE);
    if (ready_for_read) {
        (*last_ref)->next = prepare_new_read();
        BOOL rslt = ReadFile(inf, &(*last_ref)->payload, HTTP_FILE_READ_SIZE, &len, &overlapped);
        if (rslt) {
            ro_completed = true;
        } else {
            ready_for_read = false;
            DWORD err = GetLastError();
            if (err != ERROR_IO_PENDING) {
                O2_DBw(printf("%s: ReadFile error %d, *last_ref %p\n",
                              o2_debug_prefix, err, *last_ref));
                read_eof();  // EOF and error are both treated as eof
                return;
            }
        }
    }
    // read operation can complete immediately, in which case ro_completed is 
    // set, or read can complete later, detected by GetOverlappedResult
    if (ro_completed || GetOverlappedResult(inf, &overlapped, &len, false)) {
        // need to update overlapped to maintain read pointer
        overlapped.Offset += len;
        read_operation_completed(len);
        ready_for_read = true;
    } else {  // could be end of file or else file read error
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            O2_DBw(printf("%s: GetOverlappedResult result %d, *last_ref %p\n",
                          o2_debug_prefix, err, *last_ref));
            read_eof();  // EOF and error are both treated as eof
            return;
        }
    }
}
#endif  


// called when async read completes (common code for Windows, macOS & Linux)
// n is the number of bytes read into (*last_ref)->payload
void Http_reader::read_operation_completed(int n)
{
    O2netmsg_ptr msg = *last_ref;
    msg->length = n;
    O2_DBw(o2_print_bytes("Http_reader read complete:", msg->payload, n));
    data_len += n;

    last_ref = &(msg->next);
}


Http_reader::~Http_reader()
{
    if (conn) {
        if (conn->inf != -1) {
#ifdef WIN32
            // Windows has no file to close; however, this is on the
            // list of readers to poll, so remove to avoid dangling pointer.
            o2ws_protocol->remove_reader(this);
#else
            closesocket(conn->inf);
#endif
            conn->inf = -1;
        }
        conn->reader = NULL;
        conn = NULL;
    }
}


// Special feature to send ws://IP:port/o2ws to client
// Replace "ws:/THE.LOC.ALH.OST:PORTNO/o2ws" (including the quotes)
// with actual IP and port address, padded with spaces as needed.
// This is tricky because string (web page) is broken into chunks.
void substitute_ip_port(O2netmsg_ptr msg, int port)
{
    const char *key = "\"ws://THE.LOC.ALH.OST:PORTNO/o2ws\"";
    const int keylen = 34;
    char *start = msg->payload;
    O2netmsg_ptr curmsg;
    char *curchr;
    int found = 0;
    while (found < keylen) {
        found = 0;
        curmsg = msg;
        curchr = start;
        while (found < keylen) {
            if (curchr >= curmsg->payload + curmsg->length) {
                // continue searching in next msg
                curmsg = curmsg->next;
                if (!curmsg) return; // < keylen chars left to search
                curchr = curmsg->payload;
            }
            if (*curchr++ != key[found++]) {
                start++;
                if (start >= msg->payload + msg->length) {
                    msg = msg->next;
                    if (!msg) return;  // no more chars to search
                    start = msg->payload;
                }
                break;  // search from new start location
            }
        }
    }
    // found it! Build replacement string
    char replacement[keylen + 1];
    strcpy(replacement, "\"ws://");
    o2_hex_to_dot(o2n_internal_ip, replacement + 6);
    int next = (int) strlen(replacement);
    replacement[next++] = ':';
    sprintf(replacement + next, "%d", port);
    next += (int) strlen(replacement + next); // string increased by length of port
    strcpy(replacement + next, "/o2ws\"");
    next += 6;
    while (next < keylen) replacement[next++] = ' ';  // pad with spaces
    replacement[next] = 0;  // not needed, but conventional to add EOS

    // Replace characters starting at start
    found = 0;
    curchr = start;
    curmsg = msg;
    while (found < keylen) {
        *curchr++ = replacement[found++];
        if (curchr >= curmsg->payload + curmsg->length) {
            curmsg = curmsg->next;
            curchr = curmsg->payload;
        }
    }
}

O2netmsg_ptr Http_reader::prepare_new_read()
{
    O2netmsg_ptr msg = O2N_MESSAGE_ALLOC(HTTP_FILE_READ_SIZE);
    msg->next = NULL;
    msg->length = 0; // until next read completes
    *last_ref = msg;  // note that this sets data with the first message
    return msg;
}



// for macOS and Linux, we use aio and o2network poll code to do
// asynchronous file read.

// Handle file read event; unlike deliver() for sockets, msg is NULL
O2err Http_reader::deliver(O2netmsg_ptr msg)
{
#ifdef WIN32
    return O2_FAIL;  // deliver should not be called in Windows
#else
    int n = 0;
    if (data) { // not the first read after open, so get result
        if (aio_error(&cb) == EINPROGRESS) {
            return O2_SUCCESS;
        }
        n = aio_return(&cb);
        if (n <= 0) {  // if error condition, pretend it is just EOF
            return read_eof();     // *last_ref was the first message
        }
        read_operation_completed(n);
    }
    // start a new read
    msg = prepare_new_read();

    memset(&cb, 0, sizeof(aiocb));
    cb.aio_nbytes = HTTP_FILE_READ_SIZE;
    cb.aio_fildes = fds_info->get_socket();
    cb.aio_offset = data_len;
    cb.aio_buf = msg->payload;

    if (aio_read(&cb) != -1) {
        return O2_SUCCESS;
    }
    return read_eof();
#endif
}


// when entire file has been read, call this to deliver the HTTP reply
O2err Http_reader::read_eof()
{
    // if we got a read error, we'll deliver what we got so far
    // all content is in the list of messages at data
    // we have an empty message linked at the end of the message list
    // unlink the message and use it for the reply header
    O2netmsg_ptr msg = *last_ref;
    assert(msg);
    *last_ref = NULL;       // note that this does data=NULL if
    // data_len is the total length of data
    // make a prefix with reply info
#ifdef WIN32
    CloseHandle(inf);
    inf = INVALID_HANDLE_VALUE;
    ready_for_read = false;
#else
    close(fds_info->get_socket());
#endif
    snprintf(msg->payload, 150,
             "HTTP/1.1 200 OK\r\nServer: O2 Http_server\r\n"
             "Content-Length: %ld\r\nContent-Type: text/html\r\n"
             "Connection: Closed\r\n\r\n", data_len);
    msg->length = (int) strlen(msg->payload);
    msg->next = data; data = msg;  // push
    assert(conn->fds_info->out_message == NULL);  // no output should be pending

    if (data_len > 36) {
        substitute_ip_port(data->next, port);
    }
    
    conn->fds_info->out_message = data;  // transfer to output queue
    /*
    while (data) {
        printf("SENDING %d bytes ||", data->length);
        // for (int i = 0; i < data->length; i++) putchar(data->payload[i]);
        printf("||\n");
        data = data->next;
    }
     */
    data = NULL;  // remove other references
    last_ref = &data;
    conn->fds_info->send(false);  // send all messages in list, non-blocking
#ifdef WIN32
    delete this;
#else
    fds_info->close_socket(false);  // removes fd from fd and fds lists
#endif
    // now this is deleted!
    return O2_SUCCESS;
}


#endif
#endif
