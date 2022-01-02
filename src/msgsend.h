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

class Pending_msgs_queue {
    O2message_ptr head;
    O2message_ptr tail;
public:
    Pending_msgs_queue();
    void enqueue(O2message_ptr msg);
    O2message_ptr dequeue();
    bool empty();
};

extern Pending_msgs_queue o2_pending_local;
extern Pending_msgs_queue o2_pending_anywhere;


void o2_drop_msg_data(const char *warn, O2msg_data_ptr data);

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


/**
 * Send a message to any tappers. tap messages will be queued
 * if another message delivery is in progress.
 * @param msg the message  to deliver
 * @param ss is the services entry with the tapper list
 */
void o2_send_to_taps(O2message_ptr msg, Services_entry *ss);


O2err o2_msg_send_now();

void o2_call_handler(Handler_entry *handler, O2msg_data_ptr msg,
                     const char *types);

O2node *o2_msg_service(O2msg_data_ptr msg, Services_entry **services);

// int o2_send_message(Fds_info *proc, int blocking);

void o2_send_local(O2node *service, Services_entry *ss);

// void o2_queue_message(O2message_ptr msg, Fds_info *proc);

#endif /* o2_send_h */
