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
#include "o2_dynamic.h"
#include "o2_socket.h"
#include "o2_search.h"
#include "o2_internal.h"
#include "o2_message.h"
#include "o2_sched.h"
#include "o2_send.h"
#include "o2_search.h"
#include "o2_interoperation.h"

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
        RETURN_IF_ERROR(make_tcp_recv_socket(OSC_TCP_SERVER_SOCKET,
                                             &o2_osc_tcp_accept_handler, &info));
    } else {
        RETURN_IF_ERROR(make_udp_recv_socket(OSC_SOCKET, &port_num, &info));
    }
    info->osc_service_name = o2_heapify(service_name);
    return O2_SUCCESS;
}


size_t o2_arg_size(o2_type type, void *data);

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
    int rslt = o2_build_message(&msg, 0.0, service_name, path, typestring, ap);
    if (rslt == O2_SUCCESS) {
        // TODO: send the message
    }
    return rslt;
}


// messages to this service are forwarded as OSC messages
int o2_delegate_to_osc(char *service_name, char *ip, int port_num, int tcp_flag)
{
    int ret = O2_SUCCESS;
    if (streql(ip, "")) ip = "localhost";
    char port[24]; // can't overflow even with 64-bit int
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
        RETURN_IF_ERROR(make_tcp_recv_socket(OSC_TCP_SOCKET,
                                             o2_osc_delegate_handler, &info));
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


int o2_deliver_osc(fds_info_ptr info)
{
    // osc message has the form: address, types, data
    // o2 message has the form: timestamp, address, types, data
    // o2 address must have a info->u.osc_service_name prefix
    // since we need more space, allocate a new message for o2 and
    // copy the data to it
    
    int service_len = (int) strlen(info->osc_service_name);
    // length in data part will be timestamp + slash (1) + service name +
    //    o2 data; add another 7 bytes for padding after address
    int o2len = sizeof(double) + 8 + service_len + info->message->length;
    o2_message_ptr o2msg = o2_alloc_size_message(o2len);
    o2msg->data.timestamp = 0.0;  // deliver immediately
    o2msg->data.address[0] = '/'; // slash before service name
    strcpy(o2msg->data.address + 1, info->osc_service_name);
    // how many bytes in OSC address?
    char *msg_data = (char *) &(info->message->data); // OSC address starts here
    int addr_len = (int) strlen(msg_data);
    // compute address of byte after the O2 address string
    char *o2_ptr = o2msg->data.address + 1 + service_len;
    // zero fill to word boundary
    int32_t *fill_ptr = (int32_t *) WORD_ALIGN_PTR(o2_ptr + addr_len);
    *fill_ptr = 0;
    // copy in OSC address string, possibly overwriting some of the fill
    memcpy(o2_ptr, msg_data, addr_len);
    o2_ptr = (char *) (fill_ptr + 1); // get location after O2 address
    // copy type string and OSC message data
    char *osc_ptr = WORD_ALIGN_PTR(msg_data + addr_len + 4);
    o2len = msg_data + info->message->length - osc_ptr; // how much payload to copy
    memcpy(o2_ptr, osc_ptr, o2len);
    o2msg->length = o2_ptr + o2len - (char *) &(o2msg->data);
    // now we have an O2 message to send
    o2_free_message(info->message);
    if (o2_process->proc.little_endian) {
        o2_msg_swap_endian(o2msg, FALSE);
    }
    
    return o2_send_message(o2msg, FALSE);
}


// forward an O2 message to an OSC server
void o2_send_osc(osc_entry_ptr service, o2_message_ptr msg)
{
    // we need to strip off the service from the address,
    // ignore the timestamp
    // and send the msg to OSC server.
    // The message will start at msg->data.address (note that when
    // we receive OSC messages, the OSC message starts at
    // msg->data.timestamp even though there is no timestamp
    // Unlike o2_deliver_osc(), we do not allocate new message.

    // Begin by converting to network byte order:
    if (o2_process->proc.little_endian) {
        o2_msg_swap_endian(msg, TRUE);
    }

    // Move address to eliminate service name prefix
    int addr_len = (int) strlen(msg->data.address) + 1;  // include EOS
    int service_len = (int) strlen(service->key) + 1; // include slash
    memmove(msg->data.address, msg->data.address + service_len,
            addr_len - service_len);

    // now we probably have to move the rest of the message since we
    // removed the service name from the address
    // First, let's get the address of the rest of the message:
    char *ptr = msg->data.address + addr_len;
    char *types_ptr = WORD_ALIGN_PTR(ptr + 3);
    // now ptr is address after EOS after original address
    //     types_ptr points to beginning of type string in original msg
    ptr -= service_len;
    // now ptr is address after EOS after removing service name
    // fill zeros after address that we just moved:
    while (((size_t) ptr) & 3) *ptr++ = 0;
    // now ptr is address where type string should start
    if (ptr != types_ptr) {
        memmove(ptr, types_ptr, (((char *) &(msg->data)) + msg->length) - types_ptr);
    }
    // shorten length by the number of bytes by which content was shifted
    msg->length -= (types_ptr - ptr);

    // Now we have an OSC message at msg->data.address. Send it.
    if (service->fds_index < 0) { // must be UDP
        if (sendto(local_send_sock, msg->data.address,
                   msg->length - sizeof(double), // we're not sending timestamp
                   0, (struct sockaddr *) &(service->udp_sa),
                   sizeof(service->udp_sa)) < 0) {
            perror("o2_send_osc");
        }
    } else { // send by TCP
        // if (send(service->tcp_socket, you are here
        assert(FALSE); // fix this
    }
}
