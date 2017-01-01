//
//  o2_send.c
//  O2
//
//  Created by 弛张 on 2/4/16.
//  Copyright © 2016 弛张. All rights reserved.
//

#include "ctype.h"
#include "o2_internal.h"
#include "o2_send.h"
#include "o2_message.h"
#include "o2_interoperation.h"
#include "o2_discovery.h"


#include <errno.h>


#if defined(WIN32) || defined(_MSC_VER)
int initWSock();
#endif


// to prevent deep recursion, messages go into a queue if we are already
// delivering a message via o2_msg_data_deliver:
static int in_find_and_call_handlers = FALSE;
static o2_message_ptr pending_head = NULL;
static o2_message_ptr pending_tail = NULL;


void o2_deliver_pending()
{
    while (pending_head) {
        o2_message_ptr msg = pending_head;
        if (pending_head == pending_tail) {
            pending_head = pending_tail = NULL;
        } else {
            pending_head = pending_head->next;
        }
        o2_message_send(msg);
        o2_messge_free(msg);
    }
}


generic_entry_ptr o2_find_service(const char *service_name)
{
    // all callers are passing in (possibly) unaligned strings, so we
    // need to copy the service_name to aligned storage and pad it
    int i;
    char padded[MAX_SERVICE_LEN + 8]; // 8 allows for padding
    for (i = 0; i < MAX_SERVICE_LEN; i++) {
        char c = (padded[i] = service_name[i]);
        if (c == '/') {
            padded[i] = 0;
            break;
        } else if (c == 0) {
            break;
        }
    }
    // make sure final word is padded to word boundary (or else hash
    // computation might be corrupted by stray bytes
    padded[i++] = 0;
    padded[i++] = 0;
    padded[i++] = 0;
    padded[i++] = 0;

    generic_entry_ptr *entry = o2_lookup(&path_tree_table, padded, &i);
    if (!entry) {
        return NULL;
    }
    // TODO: can anything else be in the path_tree_table? At least /o2_ and IP addresses
    if ((*entry)->tag == PATTERN_NODE ||
        (*entry)->tag == PATTERN_HANDLER ||
        (*entry)->tag == O2_REMOTE_SERVICE ||
        (*entry)->tag == OSC_REMOTE_SERVICE) {
        return *entry;
    }
    return NULL;
}


// The macro form of o2_sends
int o2_send_marker(char *path, double time, int tcp_flag, char *typestring, ...)
{
    va_list ap;
    va_start(ap, typestring);

    o2_message_ptr msg;
    int rslt = o2_message_build(&msg, time, NULL, path, typestring, tcp_flag,
                                ap);
#ifndef O2_NO_DEBUGGING
    if (o2_debug > 2 || // non-o2-system messages only if o2_debug <= 2
        (o2_debug > 1 && msg->data.address[1] != '_' &&
         !isdigit(msg->data.address[1]))) {
            printf("O2: sending%s ", (tcp_flag ? " cmd" : ""));
            o2_msg_data_print(&(msg->data));
            printf("\n");
        }
#endif
    if (rslt != O2_SUCCESS) {
        return rslt; // could not allocate a message!
    }
    return o2_message_send(msg);
}


int o2_message_send(o2_message_ptr msg)
{

    // Find the remote service, note that we skip over the leading '/':
    generic_entry_ptr service = o2_find_service(msg->data.address + 1);
    if (!service) {
        o2_messge_free(msg);
        return O2_FAIL;
    } else if (service->tag > PATTERN_HANDLER) { // remote delivery?
        o2_send_remote(&msg->data, msg->tcp_flag, service);
        o2_messge_free(msg);
    } else if (msg->data.timestamp > 0.0 &&
               msg->data.timestamp > o2_gtsched.last_time) { // local delivery
        return o2_schedule(&o2_gtsched, msg); // local delivery later
    } else if (in_find_and_call_handlers) {
        if (pending_tail) {
            pending_tail->next = msg;
            pending_tail = msg;
        } else {
            pending_head = pending_tail = msg;
        }
        return O2_SUCCESS;
    } else {
        in_find_and_call_handlers = TRUE;
        o2_msg_data_deliver(&msg->data, msg->tcp_flag, service);
        o2_messge_free(msg);
        in_find_and_call_handlers = FALSE;
    }
    return O2_SUCCESS;
}


// deliver msg_data; similar to o2_message_send but local future
//     delivery requires the creation of an o2_message
int o2_msg_data_send(o2_msg_data_ptr msg, int tcp_flag)
{
    generic_entry_ptr service = o2_find_service(msg->address + 1);
    if (service->tag > PATTERN_HANDLER) {
        return o2_send_remote(msg, tcp_flag, service);
    } else if (msg->timestamp > 0.0 &&
               msg->timestamp > o2_gtsched.last_time) {
        // need to schedule o2_msg_data, so we need to copy to an o2_message
        int len = MSG_DATA_LENGTH(msg);
        o2_message_ptr message = o2_alloc_size_message(len);
        memcpy((char *) &(message->data), &msg, len);
        message->length = len;
        return o2_schedule(&o2_gtsched, message);
    } else {
        o2_msg_data_deliver(msg, tcp_flag, service);
        return O2_SUCCESS;
    }
}


int o2_send_remote(o2_msg_data_ptr msg, int tcp_flag, generic_entry_ptr service)
{
    if (service->tag == O2_REMOTE_SERVICE) {
        // send the message to remote process
        fds_info_ptr info = DA_GET(o2_fds_info, fds_info,
                ((remote_service_entry_ptr) service)->process_index);
        if (tcp_flag) {
            return send_by_tcp_to_process(info, msg);
        } else { // send via UDP
            // printf(" +    %s normal udp msg to %s, port %d, ip %x\n",
            //        debug_prefix, msg->data.address,
            //        ntohs(proc->udp_sa.sin_port),
            //        ntohl(proc->udp_sa.sin_addr.s_addr));
#if IS_LITTLE_ENDIAN
            o2_msg_swap_endian(msg, TRUE);
#endif
            if (sendto(local_send_sock, (char *) msg, MSG_DATA_LENGTH(msg),
                       0, (struct sockaddr *) &(info->proc.udp_sa),
                       sizeof(info->proc.udp_sa)) < 0) {
                perror("o2_message_send");
                return O2_FAIL;
            }
            O2_DB4(printf("sent UDP, local_send_sock %d, length %d, proc %p\n",
                          local_send_sock, MSG_DATA_LENGTH(msg), info));
        }
    } else if (service->tag == OSC_REMOTE_SERVICE) {
        o2_send_osc((osc_entry_ptr) service, msg);
    } else {
        assert(FALSE);
    }
    return O2_SUCCESS;
}


int send_by_tcp_to_process(fds_info_ptr info, o2_msg_data_ptr msg)
{
    // printf("+    %s send by tcp %s\n", debug_prefix, msg->data.address);
    // Send the length of the message
    int32_t len = htonl(MSG_DATA_LENGTH(msg));
    SOCKET fd = INFO_TO_FD(info);
    if (send(fd, (char *) &len, sizeof(int32_t), MSG_NOSIGNAL) < 0) {
        perror("o2_message_send writing length");
        goto send_error;
    }
#if IS_LITTLE_ENDIAN
    o2_msg_swap_endian(msg, TRUE);
#endif
    // Send the message body
    if (send(fd, (char *) msg, MSG_DATA_LENGTH(msg), MSG_NOSIGNAL) < 0) {
        perror("o2_message_send writing data");
        goto send_error;
    }
    return O2_SUCCESS;
  send_error:
    if (errno != EAGAIN && errno != EINTR) {
        o2_remove_remote_process(info);
    }
    return O2_FAIL;
}    



