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

void o2_drop_msg_data(const char *warn, o2_msg_data_ptr data);

void o2_prepare_to_deliver(o2_message_ptr msg);

extern int o2_do_not_reenter; // counter to allow nesting

void o2_deliver_pending(void);

// free any remaining messages after O2 has been shut down
void o2_free_pending_msgs(void);

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
void o2_msg_deliver(o2_node_ptr service, services_entry_ptr services);

// callback from o2n (network abstraction) layer
o2_err_t o2_message_deliver(o2n_info_ptr info);

void o2_call_handler(handler_entry_ptr handler, o2_msg_data_ptr msg,
                     const char *types);

services_entry_ptr *o2_services_find(const char *service_name);

o2_node_ptr o2_msg_service(o2_msg_data_ptr msg, services_entry_ptr *services);


o2_err_t o2_message_send_sched(int schedulable);

// int o2_send_message(o2n_info_ptr proc, int blocking);

void o2_send_local(o2_node_ptr service, services_entry_ptr ss);

o2_err_t o2_send_remote(proc_info_ptr proc, int block);

// void o2_queue_message(o2_message_ptr msg, o2n_info_ptr proc);

#endif /* o2_send_h */
