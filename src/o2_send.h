//
//  o2_send.h
//  O2
//
//  Created by 弛张 on 2/4/16.
//  Copyright © 2016 弛张. All rights reserved.
//

#ifndef o2_send_h
#define o2_send_h

// if MSG_NOSIGNAL is an option for send(), then use it;
// otherwise, give it a legal innocuous value as a flag:
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

extern int o2_in_find_and_call_handlers;

void o2_deliver_pending(void);

services_entry_ptr *o2_services_find(const char *service_name);

o2_info_ptr o2_msg_service(o2_msg_data_ptr msg, services_entry_ptr *services);

/**
 *  \brief Use initial part of an O2 address to find an o2_service using
 *  a hash table lookup.
 *
 *  @param name points to the service name (do not include the
 *              initial '!' or '/' from the O2 address).
 *
 *  @return The pointer to the service, tag may be TCP_SOCKET 
 *          (remote process), PATTERN_NODE (local service), 
 *          PATTERN_HANDLER (local service with single handler),
 *          or OSC_REMOTE_SERVICE (redirect to OSC server),
 *          or NULL if name is not found.
 */
o2_info_ptr o2_service_find(const char *name, services_entry_ptr *services);

int o2_message_send_sched(o2_message_ptr msg, int schedulable);

int o2_msg_data_send(o2_msg_data_ptr msg, int tcp_flag);

int o2_send_message(struct pollfd *pfd, o2_message_ptr msg,
                    int blocking, process_info_ptr proc);

int o2_send_remote(o2_message_ptr msg, process_info_ptr info);

int send_by_tcp_to_process(process_info_ptr proc, o2_message_ptr msg);

#endif /* o2_send_h */
