//
//  o2_interoperation.c
//  o2
//
//  Created by ĺźĺź  on 3/31/16.
//
/* Design notes:
 *    We handle incoming OSC ports using 
 * o2_create_osc_port(service_name, port_num), which puts an entry in
 * the fds_info table that says incoming OSC messages are handled by
 * the service_name (which may or may not be local). Thus, when an OSC
 * message arrives, we use the incoming data to construct a full O2
 * message. We can use the OSC message length plus the service name
 * length plus a timestamp length (plus some padding) to determine
 * how much message space to allocate. Then, we can receive the data
 * with some offset allowing us to prepend the timestamp and 
 * service name. Finally, we just send the message, resulting in
 * either a local dispatch or forwarding to an O2 service.
 *
 *   We handle outgoing OSC messages using
 * o2_delegate_to_osc(service_name, ip, port_num), which puts an
 * entry in the top-level hash table with the OSC socket.
 */

#include "o2.h"
#include "o2_internal.h"
#include "o2_message.h"
#include "o2_sched.h"
#include "o2_send.h"
#include "o2_interoperation.h"


static o2_message_ptr osc_to_o2(int32_t len, char *oscmsg, char *service);

static int64_t osc_time_offset = 0;

int64_t set_osc_time_offset(int64_t offset)
{
    int64_t old = osc_time_offset;
    osc_time_offset = offset;
    return old;
}


o2_time o2_time_from_osc(int64_t osctime)
{
    osctime -= osc_time_offset;
    return osctime / 4294967296.0;
}


int64_t o2_time_to_osc(o2_time o2time)
{
    int64_t osctime = (int64_t) (o2time * 4294967296.0);
    return osctime + osc_time_offset;
}


/* create a port to receive OSC messages.
 * Messages are directed to service_name.
 *
 * Algorithm: Add a socket, put service name in info
 */
int o2_create_osc_port(const char *service_name, int port_num, int udp_flag)
{
    //osc_entry_ptr osc_entry = O2_MALLOC(sizeof(osc_entry));
    //osc_entry->tag = OSC_LOCAL_SERVICE;
    //osc_entry->key = o2_heapify(service_name);
    //osc_entry->port = port_num;
    //osc_entry->fds_index = -1;
    fds_info_ptr info;
    if (udp_flag) {
        RETURN_IF_ERROR(o2_make_tcp_recv_socket(OSC_TCP_SERVER_SOCKET,
                                                &o2_osc_tcp_accept_handler, &info));
    } else {
        RETURN_IF_ERROR(o2_make_udp_recv_socket(OSC_SOCKET, &port_num, &info));
    }
    info->osc_service_name = o2_heapify(service_name);
    return O2_SUCCESS;
}


/** send an OSC message directly. The service_name is the O2 equivalent
 * of an address. path is a normal OSC address string and is not prefixed
 * with an O2 service name.
 */
int o2_send_osc_message_marker(char *service_name, const char *path,
                               const char *typestring, ...)
{
    va_list ap;
    va_start(ap, typestring);
    
    o2_message_ptr msg;
    int rslt = o2_message_build(&msg, 0.0, service_name, path, typestring, FALSE, ap);
    if (rslt == O2_SUCCESS) {
        // TODO: send the message, set tcp_flag
    }
    return rslt;
}


// messages to this service are forwarded as OSC messages
int o2_delegate_to_osc(char *service_name, char *ip, int port_num, int tcp_flag)
{
    int ret = O2_SUCCESS;
    if (streql(ip, "")) ip = "localhost";
    char port[24]; // can't overrun even with 64-bit int
    sprintf(port, "%d", port_num);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = PF_INET;
    struct addrinfo *aiptr = NULL;
    struct sockaddr_in remote_addr;

    // make a description for fds_info
    osc_entry_ptr entry = O2_MALLOC(sizeof(osc_entry));
    entry->tag = OSC_REMOTE_SERVICE;
    char *key = o2_heapify(service_name);
    entry->key = key;
    entry->port = port_num;

    if (tcp_flag) {
        fds_info_ptr info;
        RETURN_IF_ERROR(o2_make_tcp_recv_socket(
                OSC_TCP_SOCKET, o2_osc_delegate_handler, &info));
        // make the connection
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        if (getaddrinfo(ip, port, &hints, &aiptr)) {;
            goto hostname_to_netaddr_fail;
        }
        memcpy(&remote_addr, aiptr->ai_addr, sizeof(remote_addr));
        remote_addr.sin_port = htons((short) port_num);
        SOCKET sock = DA_LAST(o2_fds, struct pollfd)->fd;
        entry->fds_index = o2_fds_info.length - 1; // info is in last entry
        if (connect(sock, (struct sockaddr *) &remote_addr,
                    sizeof(remote_addr)) == -1) {
            perror("OSC Server connect error!");
            o2_fds_info.length--;
            o2_fds.length--;
            ret = O2_TCP_CONNECT;
            goto fail_and_exit;
        }
    } else {
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
        if (getaddrinfo(ip, port, &hints, &aiptr)) {
            goto hostname_to_netaddr_fail;
        }
        memcpy(&remote_addr, aiptr->ai_addr, sizeof(remote_addr));
        if (remote_addr.sin_port == 0) {
            remote_addr.sin_port = htons((short) port_num);
        }
        memcpy(&entry->udp_sa, &remote_addr, sizeof(entry->udp_sa));
        entry->fds_index = -1; // UDP
    }
    // put the entry in the master table
    o2_add_entry(&path_tree_table, (generic_entry_ptr) entry);
    ret = O2_SUCCESS;
    goto just_exit;
  hostname_to_netaddr_fail:
    ret = O2_HOSTNAME_TO_NETADDR;
  fail_and_exit:
    O2_FREE(entry);
  just_exit:
    if (aiptr) freeaddrinfo(aiptr);
    return ret;
}

static o2_message_ptr osc_bundle_to_o2(int32_t len, char *oscmsg, char *service)
{
    // osc bundle has the form #bundle, timestamp, messages
    // It is assumed that all embedded messages in an OSC bundle
    // are destined for the same service (info->osc_service_name).
    // Bundle translation is going to unpack and repack the embedded
    // messages: not the most efficient, but simpler.
    
    o2_time ts = o2_time_from_osc(*((int64_t *) (oscmsg + 8)));
    char *end_of_msg = oscmsg + len;
    char *embedded = oscmsg + 20; // skip #bundle & timestamp & length
    o2_message_ptr o2msg;
    o2_message_ptr msg_list = NULL;
    o2_message_ptr last = NULL;
    while (embedded < end_of_msg) {
        int embedded_len = ((int32_t *) embedded)[-1];
#if IS_LITTLE_ENDIAN
        embedded_len = swap32(embedded_len);
#endif
        o2msg = osc_to_o2(embedded_len, embedded, service);
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
        o2_messge_free(msg_list);
        msg_list = next;
    }
    return o2_service_message_finish(ts, service, "", TRUE);
}


// convert an osc message in network byte order to o2 message in host order
static o2_message_ptr osc_to_o2(int32_t len, char *oscmsg, char *service)
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
        o2_message_ptr o2msg = o2_alloc_size_message(o2len);
        o2msg->data.timestamp = 0.0;  // deliver immediately
        o2msg->data.address[0] = '/'; // slash before service name
        strcpy(o2msg->data.address + 1, service);
        // how many bytes in OSC address?
        int addr_len = (int) strlen(oscmsg);
        // compute address of byte after the O2 address string
        char *o2_ptr = o2msg->data.address + 1 + service_len;
        // zero fill to word boundary
        int32_t *fill_ptr = (int32_t *) WORD_ALIGN_PTR(o2_ptr + addr_len);
        *fill_ptr = 0;
        // copy in OSC address string, possibly overwriting some of the fill
        memcpy(o2_ptr, oscmsg, addr_len);
        o2_ptr = PTR(fill_ptr + 1); // get location after O2 address
        // copy type string and OSC message data
        char *osc_ptr = WORD_ALIGN_PTR(oscmsg + addr_len + 4);
        o2len = oscmsg + len - osc_ptr; // how much payload to copy
        memcpy(o2_ptr, osc_ptr, o2len);
        o2msg->length = o2_ptr + o2len - PTR(&(o2msg->data));
#if IS_LITTLE_ENDIAN
        o2_msg_swap_endian(&(o2msg->data), FALSE);
#endif
        return o2msg;
    }
}


// forward an OSC message to an O2 service
int o2_deliver_osc(fds_info_ptr info)
{
    char *msg_data = (char *) &(info->message->data); // OSC address starts here
    o2_message_ptr o2msg = osc_to_o2(info->message->length, msg_data,
                                     info->osc_service_name);
    // now we have an O2 message in host byte order to send
    o2_messge_free(info->message);

    // if this came by UDP, tag is OSC_SOCKET, and tcp_flag should be false
    o2msg->tcp_flag = (info->tag != OSC_SOCKET);
    return o2_message_send(o2msg);
}


static void msg_data_to_osc_data(osc_entry_ptr service, o2_msg_data_ptr msg)
{
    // build new message in msg_data
    if (IS_BUNDLE(msg)) {
        o2_add_bundle_head(o2_time_to_osc(msg->timestamp));
        FOR_EACH_EMBEDDED(msg,
                          int32_t *len_ptr = o2_msg_len_ptr();
                          msg_data_to_osc_data(service, embedded);
                          o2_set_msg_length(len_ptr);
                          len = MSG_DATA_LENGTH(embedded))
    } else {
        // Begin by converting to network byte order:
#if IS_LITTLE_ENDIAN
        o2_msg_swap_endian(msg, TRUE);
#endif
        // Copy address, eliminating service name prefix
        int service_len = (int) strlen(service->key) + 1; // include slash
        o2_add_string_or_symbol('s', msg->address + service_len);
        // Get the address of the rest of the message:
        char *types_ptr = msg->address + 4;
        while (types_ptr[-1]) types_ptr += 4;
        o2_add_raw_bytes(PTR(msg) + MSG_DATA_LENGTH(msg) -
                         types_ptr, types_ptr);
    }
}


// forward an O2 message to an OSC server
void o2_send_osc(osc_entry_ptr service, o2_msg_data_ptr msg)
{
    o2_send_start();
    msg_data_to_osc_data(service, msg);
    int32_t osc_len;
    char *osc_msg = o2_msg_data_get(&osc_len);
    // Now we have an OSC message at msg->address. Send it.
    if (service->fds_index < 0) { // must be UDP
        if (sendto(local_send_sock, osc_msg, osc_len,
                   0, (struct sockaddr *) &(service->udp_sa),
                   sizeof(service->udp_sa)) < 0) {
            perror("o2_send_osc");
        }
    } else { // send by TCP
        SOCKET fd = DA_GET(o2_fds, struct pollfd, service->fds_index)->fd;
        // send length
        int32_t len = htonl(osc_len);
        if (send(fd, (char *) &len, sizeof(int32_t), MSG_NOSIGNAL) < 0) {
            perror("o2_send_osc writing length");
            return;
        }
        // send message body
        if (send(fd, osc_msg, len, MSG_NOSIGNAL) < 0) {
            perror("o2_send_osc writing data");
        }
    }
}
