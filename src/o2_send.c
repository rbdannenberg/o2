//
//  o2_send.c
//  O2
//
//  Created by 弛张 on 2/4/16.
//  Copyright © 2016 弛张. All rights reserved.
//

#include "ctype.h"
#include "o2.h"
#include "o2_dynamic.h"
#include "o2_socket.h"
#include "o2_search.h"
#include "o2_internal.h"
#include "o2_send.h"
#include "o2_sched.h"
#include "o2_message.h"
#include <errno.h>


#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#if defined(WIN32) || defined(_MSC_VER)
int initWSock();
#endif


generic_entry_ptr o2_find_service(const char *service_name)
{
    // all callers are passing in (possibly) unaligned strings, so we
    // need to copy the service_name to aligned storage and pad it
    int i;
    char padded[MAX_SERVICE_LEN + 8]; // 8 allows 4 extra (I think)
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

    generic_entry_ptr *entry = lookup(&path_tree_table, padded, &i);
    if (!entry) {
        return NULL;
    }
    // TODO: can anything else be in the path_tree_table? I guess /o2_ and IP addresses
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
    int rslt = o2_build_message(&msg, time, NULL, path, typestring, ap);
#ifndef O2_NO_DEBUGGING
    if (o2_debug > 2 || // non-o2-system messages only if o2_debug <= 2
        (o2_debug > 1 && msg->data.address[1] != '_' &&
         !isdigit(msg->data.address[1]))) {
            printf("O2: sending%s ", (tcp_flag ? " cmd" : ""));
            o2_print_msg(msg);
            printf("\n");
        }
#endif
    if (rslt != O2_SUCCESS) {
        return rslt; // could not allocate a message!
    }
    return o2_send_message(msg, tcp_flag);
}


/*
BUG: the next two functions are the same except for the unused port parameter:

osc_entry_ptr o2_find_local_osc(const char *service_name, int port)
{
    int i;
    entry_ptr *e = lookup(path_tree_table, service_name, &i);
    if (!e) {
        return FALSE;
    }
    if (((*e)->generic.tag == OSC_SERVICE) && (!strcmp((*e)->osc_entry.ip, "0"))) {
        return &(*e)->osc_entry;
    }
    return FALSE;
}

osc_entry_ptr o2_find_remote_osc(const char *service_name)
{
    int i;
    entry_ptr *e = lookup(path_tree_table, service_name, &i);
    if (!e) {
        return FALSE;
    }
    if (((*e)->generic.tag == OSC_SERVICE) && strcmp((*e)->osc_entry.ip, "0")) {
        return &(*e)->osc_entry;
    }
    return FALSE;
}
*/

size_t o2_arg_size(o2_type type, void *data);


int send_by_tcp_to_process(process_info_ptr proc, o2_message_ptr msg)
{
    // printf("+    %s send by tcp %s\n", debug_prefix, msg->data.address);
    // Send the length of the message
    int32_t len = htonl(msg->length);
    SOCKET fd = DA_GET(o2_fds, struct pollfd, proc->tcp_fd_index)->fd;
    if (send(fd, &len, sizeof(int32_t), 0) < 0) {
        perror("o2_send_message writing length");
        goto send_error;
    }
    // Send the message body
    if (send(fd, &(msg->data), msg->length, 0) < 0) {
        perror("o2_send_message writing data");
        goto send_error;
    }
    return O2_SUCCESS;
  send_error:
    if (errno != EAGAIN && errno != EINTR) {
        o2_remove_remote_process(proc);
    }
    return O2_FAIL;
}    


int o2_send_message(o2_message_ptr msg, int tcp_flag)
{
    // Find the remote service, note that we skip over the leading '/':
    generic_entry_ptr service = o2_find_service(msg->data.address + 1);
    if (!service) {
        o2_free_message(msg);
        return O2_FAIL;
    }
    // Local delivery?
    if (service->tag <= PATTERN_HANDLER) {
        // TODO: test if o2_get_time() is operational?
        // future?
        if (msg->data.timestamp > o2_get_time()) {
            o2_schedule_on(&o2_ltsched, msg, service);
        } else { // send it now
            find_and_call_handlers(msg, service);
        }
        return O2_SUCCESS;
    } else if (service->tag == O2_REMOTE_SERVICE) { // send the message to remote process
        remote_service_entry_ptr rse = (remote_service_entry_ptr) service;
        process_info_ptr proc = rse->parent;
        if (tcp_flag) {
            send_by_tcp_to_process(proc, msg);
        } else { // send via UDP
            // printf(" +    %s normal udp msg to %s, port %d, ip %x\n", debug_prefix, msg->data.address, ntohs(proc->udp_sa.sin_port), ntohl(proc->udp_sa.sin_addr.s_addr));
            if (sendto(local_send_sock, &(msg->data), msg->length,
                       0, (struct sockaddr *) &(proc->udp_sa),
                       sizeof(proc->udp_sa)) < 0) {
                perror("o2_send_message");
                return O2_FAIL;
            }
            O2_DB4(printf("sent UDP, local_send_sock %d, length %d, proc %p\n",
                          local_send_sock, msg->length, proc));
        }
    } else if (service->tag == OSC_REMOTE_SERVICE) {
        send_osc(service, msg);
    } else {
        assert(FALSE);
    }
    return O2_SUCCESS;
}


#ifdef UNUSED
int o2_send_bundle(o2_service_ptr a, o2_bundle_ptr b)
{
    return o2_send_bundle_from(a, NULL, b);
}

int o2_send_bundle_from(struct remote_process *p, o2_service_ptr from, o2_bundle_ptr b)
{
    size_t data_len;
    char *data = o2_bundle_serialise(b, NULL, &data_len);
    
    // Send the bundle
    int ret = send_data(p, from, data, data_len);
    
    // Free the memory allocated by o2_bundle_serialise
    if (data)
        O2_FREE(data);
    
    return ret;
}

// From http://tools.ietf.org/html/rfc1055
#define SLIP_END        0300    /* indicates end of packet */
#define SLIP_ESC        0333    /* indicates byte stuffing */
#define SLIP_ESC_END    0334    /* ESC ESC_END means END data byte */
#define SLIP_ESC_ESC    0335    /* ESC ESC_ESC means ESC data byte */

static unsigned char *slip_encode(const unsigned char *data,
                                  size_t *data_len)
{
    size_t i, j = 0, len=*data_len;
    unsigned char *slipdata = O2_MALLOC(len*2);
    for (i=0; i<len; i++) {
        switch (data[i])
        {
            case SLIP_ESC:
                slipdata[j++] = SLIP_ESC;
                slipdata[j++] = SLIP_ESC_ESC;
                break;
            case SLIP_END:
                slipdata[j++] = SLIP_ESC;
                slipdata[j++] = SLIP_ESC_END;
                break;
            default:
                slipdata[j++] = data[i];
        }
    }
    slipdata[j++] = SLIP_END;
    slipdata[j] = 0;
    *data_len = j;
    return slipdata;
}


#endif

void send_osc(generic_entry_ptr service, o2_message_ptr msg)
{
    // TODO: finish
    printf("ERROR! send_osc\n");
}
