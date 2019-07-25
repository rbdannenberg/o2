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

extern int o2_do_not_reenter;

void o2_deliver_pending(void);

services_entry_ptr *o2_services_find(const char *service_name);

o2_node_ptr o2_msg_service(o2_msg_data_ptr msg, services_entry_ptr *services);

/**
 *  \brief Use initial part of an O2 address to find an o2_service using
 *  a hash table lookup.
 *
 *  @param name points to the service name (do not include the
 *              initial '!' or '/' from the O2 address).
 *
 *  @return The pointer to the service, tag may be INFO_TCP_SOCKET 
 *          (remote process), NODE_HASH (local service), 
 *          NODE_HANDLER (local service with single handler),
 *          or NODE_OSC_REMOTE_SERVICE (redirect to OSC server),
 *          or NULL if name is not found.
 */
o2_node_ptr o2_service_find(const char *name, services_entry_ptr *services);

int o2_message_send_sched(o2_message_ptr msg, int schedulable);

int o2_msg_data_send(o2_msg_data_ptr msg, int tcp_flag);

// int o2_send_message(o2n_info_ptr proc, int blocking);

int o2_send_remote(o2_message_ptr msg, o2n_info_ptr info);

// void o2_queue_message(o2_message_ptr msg, o2n_info_ptr proc);

int o2_send_by_tcp(o2n_info_ptr proc, int block, o2_message_ptr msg);

#endif /* o2_send_h */
