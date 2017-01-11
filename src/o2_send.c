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
        o2_message_send2(msg, TRUE);
    }
}


generic_entry_ptr *o2_service_find(const char *service_name)
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

    return o2_lookup(&path_tree_table, padded, &i);
}


// This function is invoked by macros o2_send and o2_send_cmd.
// It expects arguments to end with O2_MARKER_A and O2_MARKER_B
int o2_send_marker(const char *path, double time, int tcp_flag, const char *typestring, ...)
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
    return o2_message_send2(msg, TRUE);
}

// This is the externally visible message send function.
//
int o2_message_send(o2_message_ptr msg)
{
    return o2_message_send2(msg, TRUE);
}

// Internal message send function.
// schedulable is normally TRUE meaning we can schedule messages
// according to their timestamps. If this message was dispatched
// by o2_ltsched, schedulable will be FALSE and we should ignore
// the timestamp, which has already been observed by o2_ltsched.
//
int o2_message_send2(o2_message_ptr msg, int schedulable)
{
    // Find the remote service, note that we skip over the leading '/':
    generic_entry_ptr service = *o2_service_find(msg->data.address + 1);
    if (!service) {
        o2_message_free(msg);
        return O2_FAIL;
    } else if (service->tag == O2_REMOTE_SERVICE) { // remote delivery?
        o2_send_remote(&msg->data, msg->tcp_flag, service);
        o2_message_free(msg);
    } else if (service->tag == OSC_REMOTE_SERVICE) {
        // this is a bit complicated: send immediately if it is a bundle
        // or is not scheduled in the future. Otherwise use O2 scheduling.
        if (!schedulable || IS_BUNDLE(&msg->data) ||
             msg->data.timestamp == 0.0 ||
             msg->data.timestamp <= o2_gtsched.last_time) {
            o2_send_osc((osc_entry_ptr) service, &msg->data);
            o2_message_free(msg);
        } else {
            return o2_schedule(&o2_gtsched, msg); // delivery on time
        }
    } else if (schedulable && msg->data.timestamp > 0.0 &&
               msg->data.timestamp > o2_gtsched.last_time) { // local delivery
        return o2_schedule(&o2_gtsched, msg); // local delivery later
    } else if (in_find_and_call_handlers) {
        if (pending_tail) {
            pending_tail->next = msg;
            pending_tail = msg;
        } else {
            pending_head = pending_tail = msg;
        }
    } else {
        in_find_and_call_handlers = TRUE;
        o2_msg_data_deliver(&msg->data, msg->tcp_flag, service);
        o2_message_free(msg);
        in_find_and_call_handlers = FALSE;
    }
    return O2_SUCCESS;
}


// deliver msg_data; similar to o2_message_send but local future
//     delivery requires the creation of an o2_message
int o2_msg_data_send(o2_msg_data_ptr msg, int tcp_flag)
{
    generic_entry_ptr service = *o2_service_find(msg->address + 1);
    if (!service) return O2_FAIL;
    if (service->tag == O2_REMOTE_SERVICE) {
        return o2_send_remote(msg, tcp_flag, service);
    } else if (service->tag == OSC_REMOTE_SERVICE) {
        if (IS_BUNDLE(msg) || (msg->timestamp == 0.0 ||
                               msg->timestamp <= o2_gtsched.last_time)) {
            return o2_send_osc((osc_entry_ptr) service, msg);
        }
    } else if (msg->timestamp == 0.0 ||
               msg->timestamp <= o2_gtsched.last_time) {
        o2_msg_data_deliver(msg, tcp_flag, service);
        return O2_SUCCESS;
    }
    // need to schedule o2_msg_data, so we need to copy to an o2_message
    int len = MSG_DATA_LENGTH(msg);
    o2_message_ptr message = o2_alloc_size_message(len);
    memcpy((char *) &(message->data), msg, len);
    message->length = len;
    return o2_schedule(&o2_gtsched, message);
}


int o2_send_remote(o2_msg_data_ptr msg, int tcp_flag, generic_entry_ptr service)
{
    // send the message to remote process
    fds_info_ptr info = DA_GET(o2_fds_info, fds_info,
            ((remote_service_entry_ptr) service)->process_index);
    if (tcp_flag) {
        return send_by_tcp_to_process(info, msg);
    } else { // send via UDP
        O2_DBs(if (msg->address[1] != '_' && !isdigit(msg->address[1]))
                   o2_dbg_msg("sent UDP", msg, "to", info->proc.name));
        O2_DBS(if (msg->address[1] == '_' || isdigit(msg->address[1]))
                   o2_dbg_msg("sent UDP", msg, "to", info->proc.name));
#if IS_LITTLE_ENDIAN
        o2_msg_swap_endian(msg, TRUE);
#endif
        if (sendto(local_send_sock, (char *) msg, MSG_DATA_LENGTH(msg),
                   0, (struct sockaddr *) &(info->proc.udp_sa),
                   sizeof(info->proc.udp_sa)) < 0) {
            perror("o2_send_remote");
            return O2_FAIL;
        }
    }
    return O2_SUCCESS;
}


// Note: the message is converted to network byte order. Free the
// message after calling this.
int send_by_tcp_to_process(fds_info_ptr info, o2_msg_data_ptr msg)
{
    O2_DBs(if (msg->address[1] != '_' && !isdigit(msg->address[1]))
           o2_dbg_msg("sending TCP", msg, "to", info->proc.name));
    O2_DBS(if (msg->address[1] == '_' || isdigit(msg->address[1]))
           o2_dbg_msg("sending TCP", msg, "to", info->proc.name));
#if IS_LITTLE_ENDIAN
    o2_msg_swap_endian(msg, TRUE);
#endif
    // Send the length of the message followed by the message.
    // We want to do this in one send; otherwise, we'll send 2 
    // network packets due to the NODELAY socket option.
    int32_t len = MSG_DATA_LENGTH(msg);
    MSG_DATA_LENGTH(msg) = htonl(len);
    SOCKET fd = INFO_TO_FD(info);
    if (send(fd, (char *) &MSG_DATA_LENGTH(msg), len + sizeof(int32_t),
             MSG_NOSIGNAL) < 0) {
        if (errno != EAGAIN && errno != EINTR) {
            o2_remove_remote_process(info);
        } else {
            perror("send_by_tcp_to_process");
        }
        return O2_FAIL;
    }
    // restore len just in case caller needs it to skip over the
    // message, which has now been byte-swapped and should not be read
    MSG_DATA_LENGTH(msg) = len;
    return O2_SUCCESS;
}    
