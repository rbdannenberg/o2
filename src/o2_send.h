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
 *  Use initial part of an O2 address to find an o2_service using
 *  a hash table lookup.
 *
 *  @param name points to the service name (do not include the
 *              initial '!' or '/' from the O2 address).
 *
 *  @return The pointer to the service or NULL if none found. 
 */
generic_entry_ptr o2_find_service(const char *name);

int o2_msg_data_send(o2_msg_data_ptr msg, int tcp_flag);

int o2_send_remote(o2_msg_data_ptr msg, int tcp_flag,
                     generic_entry_ptr service);

int send_by_tcp_to_process(fds_info_ptr proc, o2_msg_data_ptr msg);

#endif /* o2_send_h */
