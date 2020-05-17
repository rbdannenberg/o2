/* o2osc.c -- open sound control compatibility */

/* Roger B. Dannenberg
 * April 2020
 */

/* Design notes:
 *    We set up to receive incoming OSC ports using 
 * o2_osc_port_new(service_name, port_num, tcp_flag), which puts an
 * osc_info as the application for an fds_info. The osc_info has the
 * service_name (which may or may not be local) indicating where to
 * send the incoming message. Thus, when an OSC message arrives,
 * we use the incoming data to construct 
 * a full O2 message. We can use the OSC message length plus the 
 * service name length plus a timestamp length (plus some padding) to 
 * determine how much message space to allocate.
 *
 *   We set up to send outgoing OSC messages using
 * o2_osc_delegate(service_name, ip, port_num), which puts a
 * entry in the top-level hash table with the OSC socket to which we
 * forward messages (after converting from O2 to OSC format).

There are 4 tags and functions for osc_info. All tags satisfy ISA_OSC(tag).
The 4 result from UDP vs TCP and incoming (server) vs outgoing (client).

As services, 
osc_info for incoming (server) (UDP or TCP) do not appear as a service.
    They do have a service_name which tells where to forward messages
    that arrive by network.
osc_info for outgoing (client) (UDP or TCP) *do* appear as a service,
    so they are referenced by both the fds_info->application pointer and
    a pointer from the services array.

OSC clients are created by o2_osc_delegate(). 
---------------------------------------------
There are UDP and TCP flavors:
    OSC Over UDP Client Socket (tag = OSC_UDP_CLIENT)
        This sends OSC messages via UDP. This is the only OSC tag that does not
        have a corresponding socket, so net_info field is NULL.
    OSC Over TCP Client Socket (tag = OSC_TCP_CLIENT)
       tag                     net_tag             notes
       OSC_TCP_CLIENT     NET_TCP_CONNECTING  waiting for connection
       OSC_TCP_CLIENT     NET_TCP_CLIENT      connected, ready to send
Creation of clients:
    o2_osc_delegate() calls
        creates osc_info object and
        installs service and makes osc->service_name = services key
        either: o2n_connect(ip, portnum, osc_info) for TCP or
                o2n_address_init(&osc->udp_address, ip, portnum, ...) for UDP
Destruction of clients:
   In response to a TCP_HUP (for OSC_TCP_CLIENT)
       o2n_close_socket(info) causes eventually a call to:
       o2n_socket_remove(info) calls
           *o2n_close_callout == o2_net_info_remove()
               (osc->service_name is services_entry key, do not free)
               calls o2_osc_info_free(), which calls
                   o2_service_remove() to remove service entry
                       frees OSC_UDP_CLIENT -or-
                       removes service_name and (re)closes OSC_TCP_CLIENT socket
           removes the entry in o2_fds and o2_fds_info arrays
           frees info
  (Note that OSC_UDP_CLIENT has no connection and never gets a "hang up")
  In response to o2_service_free():
      o2_service_remove() calls
          either (for UDP) frees osc directly -- no special cleanup needed
              or (for TCP) o2n_close_socket(osc->net_info)
          osc->service_name is owned by services and not freed


OSC servers are created by o2_osc_port_new().
----------------------------------------------------------------------
this makes incoming UDP or TCP providing OSC service:
    OSC UDP Server Port (tag = OSC_UDP_SERVER, net_tag = NET_UDP_SERVER)
        This receives OSC messages via UDP.
        Destruction: 
    OSC TCP Server Port (tag = OSC_TCP_SERVER, net_tag = NET_TCP_SERVER)
        This TCP server port is created to offer an OSC service over TCP; it
        only receives connection requests; when a request is accepted, a new
        port is created with net_tag NET_TCP_CONNECTION.
    OSC TCP Socket (tag = OSC_TCP_SERVER, net_tag = NET_TCP_CONNECTION)
        This receives OSC messages via TCP. Accepted from OSC TCP Server Port.
        The fds_info application points to the same osc_info as the 
        fds_info with net_tag NET_TCP_SERVER.
Creation of servers:
   o2_osc_port_new()
       allocates an osc_info struct
       sets osc->net_info to a new tcp or udp server port
       copies service name and stores on osc->service_name
   when a connection is accepted, o2n makes an info_node and calls
       o2_net_accepted(info, conn) which calls
           o2_osc_accepted(server, conn) which
               sets conn->application to server (note that now
                   both the TCP server port AND the accepted TCP
                   connection point to the same osc_info thorugh
                   their o2n_info->application pointers).
Destruction of servers:
   In response to a connection giving a TCP_HUP:
       do nothing: the osc_info is owned by the NET_TCP_SERVER
       o2n layer will remove the o2n_info structure
   The server stays open until the user calls o2_osc_port_free():
       find NET_TCP_SERVER with matching port and call
           o2n_close_socket(info), which eventually will call 
               o2_osc_info_free()
                   for each o2n_info that points to this same osc_info,
                       o2n_close_socket(info)
                       clear the info->application to remove dangling pointer
                           and avoid another callback. The NET_TCP_SERVER socket
                           will be closed here as well, which is a no-op.
                   free server name
                   free osc_info object
   Or the user can call o2_finish() which calls the following:
       o2n_close_socket()
       o2n_free_deleted_sockets(), which calls
           o2n_socket_remove(info) calls
               *o2n_close_callout == o2_net_info_remove()
                   o2_osc_info_free(), which functions as described above

 */

#include <ctype.h>
#include "o2internal.h"
#include "services.h"
#include "message.h"
//#include "o2_sched.h"
#include "msgsend.h"
#include "o2osc.h"

#include "errno.h"

static o2_message_ptr osc_to_o2(int32_t len, char *oscmsg, o2string service);

static uint64_t osc_time_offset = 0;

uint64_t o2_osc_time_offset(uint64_t offset)
{
    uint64_t old = osc_time_offset;
    osc_time_offset = offset;
    return old;
}

#define TWO32 4294967296.0


o2_time o2_time_from_osc(uint64_t osctime)
{
#if IS_LITTLE_ENDIAN
    osctime = swap64(osctime); // message is byte swapped
#endif
    osctime -= osc_time_offset;
    return osctime / TWO32;
}


uint64_t o2_time_to_osc(o2_time o2time)
{
    uint64_t osctime = (uint64_t) (o2time * TWO32);
    return osctime + osc_time_offset;
}


/* create a port to receive OSC messages.
 * Messages are directed to service_name.
 * The service is not created by this call, but if the service
 * does not exist when an OSC message arrives, the message will be
 * dropped.
 *
 * Algorithm: Add a socket, put service name in info
 */
int o2_osc_port_new(const char *service_name, int port, int tcp_flag)
{
    // create osc_info to pass to network layer
    osc_info_ptr osc = (osc_info_ptr) O2_CALLOC(1, sizeof(osc_info));
    if (tcp_flag) {
        osc->net_info = o2n_tcp_server_new(port, (void *) osc);
        osc->tag = OSC_TCP_SERVER;
    } else {
        osc->net_info = o2n_udp_server_new(&port, (void *) osc);
        osc->tag = OSC_UDP_SERVER;
    }
    if (!osc->net_info) { // failure, remove osc
        O2_FREE(osc);
        osc = NULL;
    }
    osc->service_name = o2_heapify(service_name);
    return O2_SUCCESS;
}


// callback when a new connection is made to an OSC_TCP_SERVER socket
//
int o2_osc_accepted(osc_info_ptr server, o2n_info_ptr conn)
{
    assert(server && server->tag == OSC_TCP_SERVER);
    // create an osc_info for the connection
    conn->application = (void *) server;
    return O2_SUCCESS;
}


int o2_osc_connected(osc_info_ptr client)
{
    assert(client && client->tag == OSC_TCP_CLIENT);
    // nothing further to do for osc connections
    return O2_SUCCESS;
}


void o2_osc_info_free(osc_info_ptr osc)
{
    if (osc->service_name &&
        (osc->tag == OSC_TCP_CLIENT || osc->tag == OSC_UDP_CLIENT)) {
        // if we are a client, we offer a service that's going away:
        o2_service_remove(osc->service_name, NULL, NULL, -1);
    } else if (osc->tag == OSC_TCP_SERVER && osc->net_info &&
        osc->net_info->net_tag == NET_TCP_SERVER) {
        // delete all the sockets that have been accepted and delete server
        // socket:
        int i = 0;
        o2n_info_ptr info;
        while ((info = o2n_get_info(i++))) {
            osc_info_ptr conn = (osc_info_ptr) (info->application);
            if (conn == osc) { // must be an open connection for this service
                info->application = NULL; // remove to-be dangling pointer
                o2n_close_socket(info); // close the accepted connection
            }
        }
        O2_FREE(osc->service_name); // forward-to name is owned by osc
    } else if (osc->tag == OSC_UDP_SERVER) {
//        if (osc->net_info) {
//            osc->net_info->application = NULL; // don't call back to delete
//            o2n_close_socket(osc->net_info);
//        }
        O2_FREE(osc->service_name); // forward-to name is owned by osc
    }
    // free osc unless this is just a connection that closed:
    if (!osc->net_info || osc->net_info->net_tag != NET_TCP_CONNECTION) {
        O2_FREE(osc);
    }
}


/* free the port (if UDP) or free the server port and all accepted 
 * connections (if TCP).
 * Design notes: We need to free the NET_TCP_SERVER port and also
 * all accepted sockets (NET_TCP_CONNECTION) that point to the
 * same osc_info object. I.e. we're terminating the whole service,
 * meaning all connections, not just the server port. We have two
 * reasonable algorithms for this:
 * 1) agressively close all the ports here
 * 2) just close the NET_TCP_SERVER port and then when there's a
 *    callback to free the osc_info, search and free all
 *    NET_TCP_CONNECTIONS. If we do this, then the same code can
 *    handle a full shut-down where the NET_TCP_SERVER port is
 *    closed, but o2_osc_port_free() is not called.
 */
int o2_osc_port_free(int port_num)
{
    int i = 0;
    o2n_info_ptr info;
    while ((info = o2n_get_info(i++))) {
        osc_info_ptr osc = (osc_info_ptr) (info->application);
        if (osc && ISA_OSC(osc) && osc->port == port_num &&
            info->net_tag == NET_TCP_SERVER) {
            o2n_close_socket(info);
            return O2_SUCCESS;
        }
    }
    return O2_FAIL; // server not found
}


// messages to this service are forwarded as OSC messages
// does the service exist as a local service? If so, fail.
// make an osc_info record for this delegation of service.
// If tcp_flag, make a tcp connection with tag OSC_TCP_CLIENT
// and set the tcp_socket_info to the o2n_info_ptr you get
// for the tcp connection.
// if udp, then set the udp address info in the osc_info
// add osc_info as the service
int o2_osc_delegate(const char *service_name, const char *ip,
                    int port_num, int tcp_flag)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    if (!service_name || !isalpha(service_name[0]) ||
        strchr(service_name, '/')) {
        return O2_BAD_SERVICE_NAME;
    }
    char padded_name[NAME_BUF_LEN];
    o2_string_pad(padded_name, service_name);
    osc_info_ptr osc = (osc_info_ptr) O2_CALLOC(1, sizeof(osc_info));
    osc->tag = (tcp_flag ? OSC_TCP_CLIENT : OSC_UDP_CLIENT);

    int rslt = o2_service_provider_new(padded_name, NULL,
                                       (o2_node_ptr) osc, o2_context->proc);
    if (rslt != O2_SUCCESS) {
        O2_FREE(osc);
        return rslt;
    }
    // osc->service_name is the same as the services_entry key formed from padded_name
    if (tcp_flag) {
        osc->net_info = o2n_connect(ip, port_num, (void *) osc);
        if (!osc->net_info) {
            rslt = O2_FAIL;
        }
    } else {
        rslt = o2n_address_init(&(osc->udp_address), ip, port_num, FALSE);
    }
    if (rslt != O2_SUCCESS) {
        O2_FREE(osc);
    }
    return rslt;
}


// convert network byte order bundle to host order O2 message
static o2_message_ptr osc_bundle_to_o2(int32_t len, char *oscmsg,
                                       o2string service)
{
    // osc bundle has the form #bundle, timestamp, messages
    // It is assumed that all embedded messages in an OSC bundle
    // are destined for the same service (info->osc_service_name).
    // Bundle translation is going to unpack and repack the embedded
    // messages: not the most efficient, but simpler.
    
    o2_time ts = o2_time_from_osc(*((uint64_t *) (oscmsg + 8)));
    char *end_of_msg = oscmsg + len;
    char *embedded = oscmsg + 20; // skip #bundle & timestamp & length
    o2_message_ptr o2msg = NULL;
    o2_message_ptr msg_list = NULL;
    o2_message_ptr last = NULL;
    while (embedded < end_of_msg) {
        int embedded_len = ((int32_t *) embedded)[-1];
#if IS_LITTLE_ENDIAN
        embedded_len = swap32(embedded_len);
#endif
        if (PTR(embedded) + embedded_len <= end_of_msg) {
            o2msg = osc_to_o2(embedded_len, embedded, service);
        }
        if (!o2msg) {
            o2_message_list_free(msg_list);
            return NULL;
        }
        o2msg->next = NULL; // make sure link is initialized
        // remember embedded messages on list
        if (last == NULL) { // first element goes at head of list
            msg_list = o2msg;
        } else {
            last->next = o2msg;
        }
        last = o2msg;

        embedded += embedded_len + sizeof(int32_t);
    }
    // add each element to a message
    o2_send_start();
    while (msg_list) {
        o2_message_ptr next = msg_list->next;
        o2_add_message(msg_list);
        O2_FREE(msg_list);
        msg_list = next;
    }
    return o2_service_message_finish(ts, service, "", O2_TCP_FLAG);
}


// convert an osc message in network byte order to o2 message in host order
static o2_message_ptr osc_to_o2(int32_t len, char *oscmsg, o2string service)
{
    // osc message has the form: address, types, data
    // o2 message has the form: timestamp, address, types, data
    // o2 address must have a info->u.osc_service_name prefix
    // since we need more space, allocate a new message for o2 and
    // copy the data to it


    if (strcmp(oscmsg, "#bundle") == 0) { // it's a bundle
        return osc_bundle_to_o2(len, oscmsg, service);
    } else { // normal message
        int service_len = (int) strlen(service);
        // length in data part will be timestamp + slash (1) + service name +
        //    o2 data; add another 7 bytes for padding after address
        int o2len = sizeof(double) + 8 + service_len + len;
        o2_message_ptr o2msg = o2_alloc_message(o2len);
        o2msg->data.timestamp = 0.0;  // deliver immediately
        o2msg->data.address[0] = '/'; // slash before service name
        strcpy(o2msg->data.address + 1, service);
        // how many bytes in OSC address?
        int addr_len = (int) strlen(oscmsg);
        // compute address of byte after the O2 address string
        char *o2_ptr = o2msg->data.address + 1 + service_len;
        // zero fill to word boundary
        int32_t *fill_ptr = (int32_t *) O2MEM_BIT32_ALIGN_PTR(o2_ptr + addr_len);
        *fill_ptr = 0;
        // copy in OSC address string, possibly overwriting some of the fill
        memcpy(o2_ptr, oscmsg, addr_len);
        o2_ptr = PTR(fill_ptr + 1); // get location after O2 address
        // copy type string and OSC message data
        char *osc_ptr = O2MEM_BIT32_ALIGN_PTR(oscmsg + addr_len + 4);
        o2len = (int) (oscmsg + len - osc_ptr); // how much payload to copy
        memcpy(o2_ptr, osc_ptr, o2len);
        o2msg->data.length = (int32_t) (o2_ptr + o2len - PTR(&(o2msg->data)));
#if IS_LITTLE_ENDIAN
        o2_msg_swap_endian(&(o2msg->data), FALSE);
#endif
        return o2msg;
    }
}


// forward an OSC message to an O2 service
// precondition: proc->in_message is in NETWORK byte order
//
int o2_deliver_osc(osc_info_ptr info, o2_message_ptr msg)
{
    char *msg_data = msg->data.address;
    O2_DBO(
     printf("%s os_deliver_osc got OSC message %s length %d for service %s\n",
     o2_debug_prefix, msg_data, msg->data.length, info->service_name));
    o2_message_ptr o2msg = osc_to_o2(msg->data.length, msg_data,
                                     info->service_name);
    O2_FREE(msg);
    if (!o2msg) {
        return O2_FAIL;
    }
    if (o2_message_send_sched(o2msg, TRUE)) { // failure to deliver message
            // will NOT cause the connection to be closed; only the current
            // message will be dropped
        O2_DBO(printf("%s os_deliver_osc: message %s forward to %s failed\n",
                      o2_debug_prefix, msg_data, info->service_name));
    }
    return O2_SUCCESS;
}


// convert O2 message to OSC message which is appended to msg_data.array
// for liblo compatibility, timestamps of embedded bundles are at least
// as late as the containing, or parent, bundle's timestamp.
//
static int msg_data_to_osc_data(osc_info_ptr osc, o2_msg_data_ptr msg,
                                o2_time min_time)
{
    // build new message in msg_data
    if (IS_BUNDLE(msg)) {
        if (msg->timestamp > min_time) min_time = msg->timestamp;
        o2_add_bundle_head(o2_time_to_osc(min_time));
        char *end_of_msg = PTR(msg) + msg->length;
        o2_msg_data_ptr embedded = (o2_msg_data_ptr)
            ((msg)->address + o2_strsize((msg)->address) + sizeof(int32_t));
        while (PTR(embedded) < end_of_msg) { int32_t len;
            int32_t *len_ptr = o2_msg_len_ptr();
            len = embedded->length;
            if ((PTR(embedded) + len > end_of_msg) ||
                msg_data_to_osc_data(osc, embedded, min_time) != O2_SUCCESS) {
                return O2_FAIL;
            }
            o2_set_msg_length(len_ptr);
            embedded = (o2_msg_data_ptr)
                       (PTR(embedded) + len + sizeof(int32_t));
        }
    } else {
        // Begin by converting to network byte order:
#if IS_LITTLE_ENDIAN
        RETURN_IF_ERROR(o2_msg_swap_endian(msg, TRUE));
#endif
        // Copy address, eliminating service name prefix, include slash
        int service_len = (int) strlen(osc->service_name) + 1;
        o2_add_string_or_symbol('s', msg->address + service_len);
        // Get the address of the rest of the message:
        char *types_ptr = msg->address + 4;
        while (types_ptr[-1]) types_ptr += 4;
        o2_add_raw_bytes((int32_t) (PTR(msg) + msg->length -
                                    types_ptr), types_ptr);
    }
    return O2_SUCCESS;
}


// forward an O2 message to an OSC server, caller owns msg
int o2_send_osc(osc_info_ptr service, o2_message_ptr msg,
                services_entry_ptr ss)
{
    int rslt = O2_SUCCESS;
    o2_send_start();
    RETURN_IF_ERROR(msg_data_to_osc_data(service, &(msg->data), 0.0));
    int32_t osc_len;
    char *osc_msg = o2_msg_data_get(&osc_len);
    // copy the osc message into the msg container to pass to o2n layer
    assert(msg->data.length <= osc_len); // sanity check, don't overrun message
    msg->data.length = osc_len;
    memcpy(&(msg->data.flags), osc_msg, osc_len); // dst is just after length
    O2_DBO(printf("%s o2_send_osc sending OSC message %s length %d as "
                  "service %s\n",
                  o2_debug_prefix, msg->data.address, msg->data.length,
                  service->service_name));
    // Now we have an OSC length and message at msg->data. Send it.
    if (service->net_info == NULL) { // must be UDP
        rslt = o2n_send_udp(&service->udp_address, (o2n_message_ptr) msg);
    } else { // send by TCP as if this is an O2 message
        o2n_send_tcp(service->net_info, FALSE, (o2n_message_ptr) msg);
        // msg is owned by o2n now
    }
    return rslt;
}


#ifndef O2_NO_DEBUG
void o2_osc_info_show(osc_info_ptr oi)
{
    printf(" service=%s port=%d\n", oi->service_name, oi->udp_address.port);
}
#endif
