// msgsend.c -- implementation of message construction
//
// Roger B. Dannenberg, 2019
//

// delivery is recursive due to bundles. Here's an overview of the structure:
//
// o2_send() builds message from arguments and calls o2_message_send_sched()
//         (Defined in msgsend.c)
// o2_send_finish() finishes building message and calls o2_message_send_sched()
//         (Defined in message.c)
// o2_message_send_sched(o2_message_ptr msg, int schedulable)
//         (Defined in msgsend.c)
//         Determines local or remote O2 or OSC
//         If remote, calls o2_send_remote()
//         If local and future and schedulable, calls o2_schedule()
//         Otherwise, calls o2_send_local() for local delivery
//         Caller gives msg to callee. msg is freed eventually.
// o2_send_remote(o2_msg_data_ptr msg, int tcp_flag, fds_info_ptr info)
//         Byte-swaps msg data to network order,
//         Sends msg_data to remote service by TCP or UDP
// o2_schedule(o2_sched_ptr sched, o2_message_ptr msg)
//         (Defined in o2_sched.c)
//         msg could be local or for delivery to an OSC server
//         Caller gives msg to callee. msg is freed eventually.
//         Calls o2_message_send_sched(msg, FALSE) to send message
//         (FALSE prevents a locally scheduled message you are trying
//         to dispatch from being considered for scheduling using the
//         o2_gtsched, which may not be sync'ed yet.)
// o2_send_local()
//         (Defined in msgsend.c)
//         Either sends a message locally or queues it to avoid
//         reentrant message delivery. Calls o2_msg_deliver() directly,
//         or o2_msg_deliver() is called by o2_deliver_pending().
// o2_msg_deliver(o2_msg_data_ptr msg, int tcp_flag,
//                o2_node_ptr service)
//         delivers a message or bundle locally. Calls
//         o2_embedded_msgs_deliver() if this is a bundle.
//         Otherwise, uses find_and_call_handlers_rec().
// o2_embedded_msgs_deliver(o2_msg_data_ptr msg, int tcp_flag)
//         Deliver or schedule messages in a bundle (recursively).
//         Calls o2_message_send_sched() to deliver each embedded
//         message, which is copied into an o2_message.
// o2_find_handlers_rec()
//         does lookup of path locally and calls handler
//
// Message parsing and forming o2_argv with message parameters is not
// reentrant since there is a global buffer used to store coerced
// parameters. Therefore, if you call a handler and the handler sends a
// message, we cannot deliver it immediately, at least not if it has a
// local destination. Therefore, messages sent from handlers may be
// saved on a list and dispatched later. 


#include "ctype.h"
#include "o2internal.h"
#include "services.h"
#include "msgsend.h"
#include "message.h"
#include "pathtree.h"
#include "o2osc.h"
//#include "discovery.h"
#include "bridge.h"

#include <errno.h>


// to prevent deep recursion, messages go into a queue if we are already
// delivering a message via o2_msg_deliver:
int o2_do_not_reenter = 0; // counter to allow nesting
// we have two pending queues: one for normal messages and one for
// local delivery (needed for taps)
typedef struct pending {
    o2_message_ptr head;
    o2_message_ptr tail;
} pending, *pending_ptr;

#define PENDING_EMPTY {NULL, NULL}
static pending pending_anywhere = PENDING_EMPTY;
static pending pending_local = PENDING_EMPTY;

// o2_send_local delivers a message immediately and locally to a service,
// but it is safe to call because it is reentrant by deferring delivery
// if needed. Ownership of msg is transferred to this function.
void o2_send_local(o2_message_ptr msg,
                   o2_node_ptr service, services_entry_ptr ss)
{
    if (o2_do_not_reenter) {
        pending_ptr p = ((msg->data.flags | O2_TAP_FLAG) ? &pending_local : &pending_anywhere);
        if (p->tail) {
            p->tail->next = msg;
            p->tail = msg;
        } else {
            p->head = p->tail = msg;
        }
    } else {
        o2_do_not_reenter++;
        o2_msg_deliver(msg, service, ss);
        o2_do_not_reenter--;
    }
}

static o2_message_ptr pending_dequeue(pending_ptr p)
{
    o2_message_ptr msg = p->head;
    if (p->head == p->tail) {
        p->head = p->tail = NULL;
    } else {
        p->head = p->head->next;
    }
    return msg;
}

void o2_deliver_pending()
{
    while (pending_anywhere.head) {
        o2_message_ptr msg = pending_dequeue(&pending_anywhere);
        o2_message_send_sched(msg, TRUE);
    }
    while (pending_local.head) {
        o2_message_ptr msg = pending_dequeue(&pending_local);
        services_entry_ptr services = *o2_services_from_msg(msg);
        if (!services) goto give_up;
        o2_node_ptr service = *o2_proc_service_find(o2_context->proc, services);
        if (!service) goto give_up;
        o2_msg_deliver(msg, service, services);
    give_up:
        O2_FREE(msg);
    }
}


// call handler for message. Does type coercion, argument vector
// construction, and type checking. types points to the type string
// after the initial ','
//
// Design note: We could find types by scanning over the address in
// msg, but since address pattern matching already scans over most
// of the address, it's faster (I think) for the caller to compute
// types and pass it in. An exception is the case where we do a hash
// lookup of the full address. In that case the caller has to scan
// over the whole address (4 bytes at a time) to find types in order
// to pass it in.
//
void o2_call_handler(handler_entry_ptr handler, o2_msg_data_ptr msg,
                     const char *types)
{
    // coerce to avoid compiler warning -- even 2^31 is absurdly big
    //     for the type string length
    int types_len = (int) strlen(types);

    // type checking
    if (handler->type_string && // mismatch detection needs type_string
        ((handler->types_len != types_len) || // first check if counts are equal
         !(handler->coerce_flag ||  // need coercion or exact match
           (streql(handler->type_string, types))))) {
        // printf("!!! %s: call_handler skipping %s due to type mismatch\n",
        //        o2_debug_prefix, msg->address);
        return; // type mismatch
    }

    if (handler->parse_args) {
        o2_extract_start(msg);
        o2string typ = handler->type_string;
        if (!typ) { // if handler type_string is NULL, use message types
            typ = types;
        }
        while (*typ) {
            o2_arg_ptr next = o2_get_next(*typ++);
            if (!next) {
                return; // type mismatch, do not deliver the message
            }
        }
        if (handler->type_string) {
            types = handler->type_string; // so that handler gets coerced types
        }
        assert(o2_context->arg_data.allocated >= o2_context->arg_data.length);
        assert(o2_context->argv_data.allocated >= o2_context->argv_data.length);
    } else {
        o2_context->argv = NULL;
        o2_context->argc = 0;
    }
    (*(handler->handler))(msg, types, o2_context->argv, o2_context->argc, handler->user_data);
}


int o2_embedded_msgs_deliver(o2_msg_data_ptr msg)
{
    char *end_of_msg = PTR(msg) + msg->length;
    o2_msg_data_ptr embedded = (o2_msg_data_ptr)
            (msg->address + o2_strsize(msg->address) + sizeof(int32_t));
    while (PTR(embedded) < end_of_msg) {
        // need to copy each embedded message before sending
        int len = embedded->length;
        o2_message_ptr message = o2_alloc_message(len);
        memcpy((char *) &(message->data), (char *) embedded, len);
        message->next = NULL;
        message->data.length = len;
        message->data.flags |= O2_TCP_FLAG;
        o2_message_send_sched(message, TRUE);
        embedded = (o2_msg_data_ptr)
                (PTR(embedded) + len + sizeof(int32_t));
    }
    return O2_SUCCESS;
}


void send_message_to_tap(o2_message_ptr msg, service_tap_ptr tap)
{
    // construct a new message to send to tapper by replacing service name
    // how big is the existing service name?
    // I think coerce to char * will remove bounds checking, which might
    // limit the search to 4 characters since msg->address is declared to
    // be char [4]
    // Skip first character which might be a slash; we want the slash after the
    // service name.
    char *slash = strchr((char *) (msg->data.address) + 1, '/');
    if (!slash) {
        return;  // this is not a valid address, stop now
    }
    int curlen = (int) (slash - msg->data.address);

    // how much space will tapper take?
    int newlen = (int) strlen(tap->tapper) + 1; // add 1 for initial '/' or '!'

    // how long is current address?
    int curaddrlen = (int) strlen((char *) (msg->data.address));

    // how long is new address?
    int newaddrlen = curaddrlen + (newlen - curlen);

    // what is the difference in space needed for address (and message)?
    // "+ 4" accounts for end-of-string byte and padding in each case
    int curaddrall = ROUNDUP_TO_32BIT(curaddrlen + 1); // address + padding
    int newaddrall = ROUNDUP_TO_32BIT(newaddrlen + 1);
    int extra = newaddrall - curaddrall;

    // allocate a new message
    o2_message_ptr newmsg = o2_alloc_message(msg->data.length + extra);
    newmsg->data.length = msg->data.length + extra;
    newmsg->data.timestamp = msg->data.timestamp;
    // fill end of address with zeros before creating address string
    int32_t *end = (int32_t *) (newmsg->data.address + ROUNDUP_TO_32BIT(newaddrlen));
    end[-1] = 0;
    // first character is either / or ! copied from original message
    newmsg->data.address[0] = msg->data.address[0];
    // copies name and EOS:
    memcpy((char *) (newmsg->data.address + 1), tap->tapper, newlen);
    memcpy((char *) (newmsg->data.address + newlen), msg->data.address + curlen,
           curaddrlen - curlen);
    // copy the rest of the message
    memcpy((char *) (newmsg->data.address + newaddrall),
           msg->data.address + curaddrall, msg->data.length - curaddrall);
    msg->data.flags = O2_TCP_FLAG | O2_TAP_FLAG;
    // must send message to tap->proc
    if (PROC_IS_REMOTE(tap->proc)) {
        o2_send_remote(newmsg, tap->proc);
    } else {
        services_entry_ptr services = *((services_entry_ptr *)
                o2_lookup(&o2_context->path_tree, tap->tapper));
        if (services) {
            o2_node_ptr service = *o2_proc_service_find(o2_context->proc, services);
            if (service) { // newmsg ownership transfers to o2_send_local()
                return o2_send_local(newmsg, service, services);
            }
        }
        O2_FREE(newmsg);
    }
}


// deliver msg locally and immediately. If service is not null,
//    assume it is correct, saving the cost of looking it up
//    ownership of msg is transferred to this function
void o2_msg_deliver(o2_message_ptr msg,
                    o2_node_ptr service, services_entry_ptr ss)
{
    if (IS_BUNDLE(&(msg->data))) {
        o2_embedded_msgs_deliver(&(msg->data));
        O2_FREE(msg);
        return;
    }

    char *address = msg->data.address;
    if (!service) {
        service = o2_msg_service(&(msg->data), &ss);
        if (!service) return; // service must have been removed
    }

    // we are going to deliver a non-bundle message, so we'll need
    // to find the type string...
    char *types = address;
    while (types[3]) types += 4; // find end of address string
    // types[3] is the zero marking the end of the address,
    // types[4] is the "," that starts the type string, so
    // types + 5 is the first type char
    types += 5; // now types points to first type char

    // if you o2_add_message("/service", ...) then the service entry is a
    // pattern handler used for ALL messages to the service
    if (service->tag == NODE_HANDLER) {
        o2_call_handler((handler_entry_ptr) service, &(msg->data), types);
    } else if ((address[0]) == '!') { // do full path lookup
        address[0] = '/'; // must start with '/' to get consistent hash value
        o2_node_ptr handler = *o2_lookup(&o2_context->full_path_table, address);
        address[0] = '!'; // restore address for no particular reason
        if (handler && handler->tag == NODE_HANDLER) {
            o2_call_handler((handler_entry_ptr) handler, &(msg->data), types);
        }
    } else if (service->tag == NODE_HASH) {
        char name[NAME_BUF_LEN];
        address = strchr(address + 1, '/'); // search for end of service name
        if (!address) {
            // address is "/service", but "/service" is not a NODE_HANDLER
            ;
        } else {
            o2_find_handlers_rec(address + 1, name, (o2_node_ptr) service,
                                 &msg->data, types);
        }
    } // else the assumption that the service is local fails, drop the message

    // if there are tappers, send the message to them as well
    // (can't tap a tap msg)
    if (!(msg->data.flags & O2_TAP_FLAG)) {
        for (int i = 0; i < ss->taps.length; i++) {
            send_message_to_tap(msg, GET_TAP_PTR(ss->taps, i));
        }
    }
    O2_FREE(msg);
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
#ifndef O2_NO_DEBUG
    if (o2_debug & // either non-system (s) or system (S) mask
        (msg->data.address[1] != '_' && !isdigit(msg->data.address[1]) ?
         O2_DBs_FLAG : O2_DBS_FLAG)) {
        printf("O2: sending%s ", (tcp_flag ? " cmd" : ""));
        o2_msg_data_print(&(msg->data));
        printf("\n");
    }
#endif
    if (rslt != O2_SUCCESS) {
        return rslt; // could not allocate a message!
    }
    return o2_message_send_sched(msg, TRUE);
}

// This is the externally visible message send function.
//
int o2_message_send(o2_message_ptr msg)
{
    return o2_message_send_sched(msg, TRUE);
}

// Internal message send function.
// schedulable is normally TRUE meaning we can schedule messages
// according to their timestamps. If this message was dispatched
// by o2_ltsched, schedulable will be FALSE and we should ignore
// the timestamp, which has already been observed by o2_ltsched.
//
// msg is freed by this function
//
int o2_message_send_sched(o2_message_ptr msg, int schedulable)
{
    // Find the remote service, note that we skip over the leading '/':
    services_entry_ptr services;
    o2_node_ptr service = o2_msg_service(&msg->data, &services);
    if (!service) {
        O2_FREE(msg);
        return O2_FAIL;
    } else if (PROC_IS_REMOTE(service)) { // remote delivery, UDP or TCP
        return o2_send_remote(msg, TO_PROC_INFO(service));
    } else if (service->tag == NODE_BRIDGE_SERVICE) {
        bridge_entry_ptr info = (bridge_entry_ptr) service;
        (*(info->bridge_send))(&msg->data, info);
        O2_FREE(msg);
    } else if (ISA_OSC(service)) {
        // this is a bit complicated: send immediately if it is a bundle
        // or is not scheduled in the future. Otherwise use O2 scheduling.
        if (!schedulable || IS_BUNDLE(&msg->data) ||
             msg->data.timestamp == 0.0 ||
             msg->data.timestamp <= o2_gtsched.last_time) {
            o2_send_osc((osc_info_ptr) service, msg, services);
            O2_FREE(msg);
        } else {
            return o2_schedule(&o2_gtsched, msg); // delivery on time
        }
    } else if (schedulable && msg->data.timestamp > 0.0 &&
               msg->data.timestamp > o2_gtsched.last_time) { // local delivery
        return o2_schedule(&o2_gtsched, msg); // local delivery later
    } else {
        o2_send_local(msg, service, services);
    }
    return O2_SUCCESS;
}


// send a message via TCP or UDP to proc, frees the message
int o2_send_remote(o2_message_ptr msg, proc_info_ptr proc)
{
    if (proc == NULL) {
        return O2_FAIL;
    }
#if IS_LITTLE_ENDIAN
    o2_msg_swap_endian(&(msg->data), TRUE);
#endif
    // send the message to remote process
    if (msg->data.flags & O2_TCP_FLAG) {
        return o2n_send_tcp(proc->net_info, TRUE, (o2n_message_ptr) msg);
    } else { // send via UDP
        O2_DBs(if (msg->data.address[1] != '_' && !isdigit(msg->data.address[1]))
                   o2_dbg_msg("sending UDP", &(msg->data), "to", proc->name));
        O2_DBS(if (msg->data.address[1] == '_' || isdigit(msg->data.address[1]))
                   o2_dbg_msg("sending UDP", &(msg->data), "to", proc->name));
        int rslt = o2n_send_udp(&proc->udp_address, (o2n_message_ptr) msg);
        if (rslt) {
            O2_DBn(printf("o2_send_remote error, port %d\n", ntohs(proc->udp_address.port)));
            return O2_FAIL;
        }
    }
    return O2_SUCCESS;
}
