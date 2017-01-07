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

void o2_deliver_pending();

/**
 *  \brief Use initial part of an O2 address to find an o2_service using
 *  a hash table lookup.
 *
 *  @param name points to the service name (do not include the
 *              initial '!' or '/' from the O2 address).
 *
 *  @return The pointer to the table entry pointing to service,
 *          or NULL if none found.
 * 
 *  Note: The table entry is not dereferenced so that the result
 *  can be used to remove the entry from the table. (See entry_remove()).
 */
generic_entry_ptr *o2_service_find(const char *name);

int o2_message_send2(o2_message_ptr msg, int schedulable);

int o2_msg_data_send(o2_msg_data_ptr msg, int tcp_flag);

int o2_send_remote(o2_msg_data_ptr msg, int tcp_flag,
                     generic_entry_ptr service);

int send_by_tcp_to_process(fds_info_ptr proc, o2_msg_data_ptr msg);

#endif /* o2_send_h */
