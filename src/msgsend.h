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

void o2_prepare_to_deliver(O2message_ptr msg);

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
void o2_msg_deliver(O2node *service, Services_entry *services);

O2err o2_msg_send_now();

void o2_call_handler(Handler_entry *handler, o2_msg_data_ptr msg,
                     const char *types);

O2node *o2_msg_service(o2_msg_data_ptr msg, Services_entry **services);

// int o2_send_message(Fds_info *proc, int blocking);

void o2_send_local(O2node *service, Services_entry *ss);

// void o2_queue_message(O2message_ptr msg, Fds_info *proc);

#endif /* o2_send_h */
