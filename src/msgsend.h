//
//  msgsend.h - send messages
//
//  Roger B. Dannenberg
//  April 2020

#ifndef MSGSEND_H
#define MSGSEND_H

// if MSG_NOSIGNAL is an option for send(), then use it;
// otherwise, give it a legal innocuous value as a flag:
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

extern int o2_do_not_reenter; // counter to allow nesting

void o2_deliver_pending(void);

/**
 * Deliver a message immediately and locally. If service is given,
 * the caller must check to see if this is a bundle and call 
 * deliver_embedded_msgs() if so. Not reentrant, but see o2_send_local().
 * Called from o2_send_local(), o2_schedule(), and o2_deliver_pending()
 *
 * @param msg the message  to deliver
 * @param service is the service to which the message is addressed.
 *                If the service is unknown, pass NULL.
 */
void o2_msg_deliver(o2_message_ptr msg,
                    o2_node_ptr service, services_entry_ptr services);

// callback from o2n (network abstraction) layer
int o2_message_deliver(o2n_info_ptr info);

void o2_call_handler(handler_entry_ptr handler, o2_msg_data_ptr msg,
                     const char *types);

services_entry_ptr *o2_services_find(const char *service_name);

o2_node_ptr o2_msg_service(o2_msg_data_ptr msg, services_entry_ptr *services);


int o2_message_send_sched(o2_message_ptr msg, int schedulable);

// int o2_send_message(o2n_info_ptr proc, int blocking);

void o2_send_local(o2_message_ptr msg,
                   o2_node_ptr service, services_entry_ptr ss);

int o2_send_remote(o2_message_ptr msg, proc_info_ptr proc);

// void o2_queue_message(o2_message_ptr msg, o2n_info_ptr proc);

#endif /* o2_send_h */
