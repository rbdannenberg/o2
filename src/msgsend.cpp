// msgsend.c -- implementation of message construction
//
// Roger B. Dannenberg, 2019
//

// delivery is recursive due to bundles. Here's an overview of the structure:
// 
// o2_send() builds message from arguments, transfers ownership to
//         o2_ctx->msgs and calls o2_message_send_sched() which transfers
//         ownership somewhere else (including the freelist).
//         (Defined in msgsend.c)
// o2_send_finish() finishes building message, transfers ownership to
//         o2_ctx->msgs and calls o2_message_send_sched()
//         (Defined in message.c)
// o2_message_send_sched(o2_message_ptr msg, int schedulable)
//         (Defined in msgsend.c)
//         Determines local or remote O2 or OSC
//         If remote, calls o2_send_remote()
//         If local and future and schedulable, calls o2_schedule()
//         Otherwise, calls o2_send_local() for local delivery
//         When called, msg owned by o2_ctx->msgs. The msg is removed
//         from o2_ctx->msgs and either handed off or freed.
// o2_send_remote(proc_info_ptr proc, int block)
//         Byte-swaps msg data to network order,
//         Sends msg_data to remote service by TCP or UDP. Ownership is
//         transferred from o2_ctx->msgs to TCP or UDP, which may
//         queue msg for later delivery, otherwise they free the msg.
// o2_schedule(o2_sched_ptr sched)
//         (Defined in sched.c)
//         Schedule message if time is in the future, or directly call
//         o2_msg_deliver() if it is time to send the message. Three
//         cases are: (1) Scheduled message ownership is transferred
//         from o2_ctx->msgs to the scheduling queue, (2) Undeliverable
//         message is freed, (3) Immediate delivery uses o2_msg_deliver
//         which frees the message.
// sched_dispatch(o2_sched_ptr s, o2_time run_until_time)
//         (Defined in sched.c)
//         Dispatches messages by transferring ownership from queue to
//         o2_ctx->msgs and calling o2_message_send_sched(). This is
//         called rather than o2_send_local() in case this is a timed
//         OSC message.
// o2_send_local(node_ptr service, services_entry_ptr ss)
//         (Defined in msgsend.c)
//         Either sends a message locally or queues it to avoid
//         reentrant message delivery. Calls o2_msg_deliver() directly,
//         or o2_msg_deliver() is called by o2_deliver_pending().
//         Initially, message is owned by o2_ctx->msgs. Upon return,
//         msg is either freed or transferred to a queue of some kind.
// o2_send_osc(osc_info_ptr service,  services_entry_ptr ss)
//         (Defined in o2osc.c)
//         takes ownership from o2_ctx->msgs or frees the message
// o2_msg_deliver(int tcp_flag, o2_node_ptr service)
//         (Defined in msgsend.c)
//         delivers a message or bundle locally. Calls
//         o2_embedded_msgs_deliver() if this is a bundle. Otherwise,
//         uses find_handlers_rec(). Initially, message ownership on
//         o2_ctx->msgs. Upon return, the message is freed. This
//         function also calls send_message_to_tap() for each tapper
//         of the service.
// send_message_to_tap(service_tap_ptr tap)
//         (Defined in msgsend.c)
//         Copies message content, changing the address to that of
//         the tapper. Calls either o2_send_remote() or o2_send_local().
//         (Can't simply call o2_message_send_sched() because tap
//         messages must be sent to a specific process even if some
//         other process offers the service and has a higher
//         public:internal:port
//         address (priority). The copied message is transferred to
//         o2_ctx->msgs (which is why we need this to be a list and
//         not just remember a single message). Either o2_send_local()
//         or o2_send_remote() take ownership from o2_ctx->msgs.
// o2_embedded_msgs_deliver(o2_msg_data_ptr msg, int tcp_flag)
//         Deliver or schedule messages in a bundle (recursively).
//         Calls o2_message_send_sched() to deliver each embedded
//         message, which is copied into an o2_message and transferred
//         to o2_ctx->msgs.
// o2_find_handlers_rec()
//         does lookup of path locally and calls handler. msg is just the
//         data part and the full message is held somewhere in the call
//         stack.
//
// Message parsing and forming o2_argv with message parameters is not
// reentrant since there is a global buffer used to store coerced
// parameters. Therefore, if you call a handler and the handler sends a
// message, we cannot deliver it immediately, at least not if it has a
// local destination. Therefore, messages sent from handlers may be
// saved on a list and dispatched later. 


#include "ctype.h"
#include "o2.h"
#include "o2internal.h"
#include "services.h"
#include "msgsend.h"
#include "message.h"
#include "pathtree.h"
#include "o2osc.h"
//#include "discovery.h"
#include "bridge.h"
#include "o2sched.h"

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

// When a message is pulled from pending to be delivered, it is saved
// here in case the user calls exit() before the message can be freed.
// exit() will call o2_finish() which will free the message. This is
// not actually a memory leak since the message will have come from a
// chunk that will be freed in any case, but this will remove the
// *appearance* of a leak so we can focus on actual problems (if any).
o2_message_ptr o2_active_message = NULL;


void o2_drop_msg_data(const char *warn, o2_msg_data_ptr data)
{
    char fullmsg[100];
    if (streql(data->address, "!_o2/si")) {
        // status info messages are internally generated and we do not
        // warn if there is no user-provided handler
        return;
    }
    snprintf(fullmsg, 100, "%s%s", "dropping message because ", warn);
    (*o2_ctx->warning)(fullmsg, data);
}


void o2_drop_message(const char *warn, o2_message_ptr msg)
{
    o2_drop_msg_data(warn, &msg->data);
}


#define PENDING_EMPTY {NULL, NULL}
static pending pending_anywhere = PENDING_EMPTY;
static pending pending_local = PENDING_EMPTY;

// push a message onto the o2_ctx->msgs list
//
void o2_prepare_to_deliver(o2_message_ptr msg)
{
    msg->next = o2_ctx->msgs;
    o2_ctx->msgs = msg;
}


o2_message_ptr o2_current_message(void)
{
    return o2_ctx->msgs;
}


// free the current message from o2_ctx->msgs
// 
void o2_complete_delivery(void)
{
    O2_FREE(o2_postpone_delivery());
}


// remove the current message from o2_ctx->msgs and return
// it so we can hand it off to another owner
// 
o2_message_ptr o2_postpone_delivery(void)
{
    assert(o2_ctx->msgs);
    o2_message_ptr msg = o2_ctx->msgs;
    o2_ctx->msgs = msg->next;
    msg->next = NULL; // debugging aid, remove needless pointer
    return msg;
}


// o2_send_local delivers a message immediately and locally to a service,
// but it is safe to call because it is reentrant by deferring delivery
// if needed. Ownership of msg is initially with o2_ctx. Ownership is
// transferred from o2_ctx before returning to caller.
void o2_send_local(o2_node_ptr service, services_entry_ptr ss)
{
    if (o2_do_not_reenter) {
        o2_message_ptr msg = o2_postpone_delivery();
        pending_ptr p = ((msg->data.flags | O2_TAP_FLAG) ? &pending_local :
                                                           &pending_anywhere);
        if (p->tail) {
            p->tail->next = msg;
            p->tail = msg;
        } else {
            p->head = p->tail = msg;
        }
    } else {
        o2_do_not_reenter++;
        o2_msg_deliver(service, ss);
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
#ifndef O2_NO_DEBUG
    extern void *o2_mem_watch;
    if (msg == o2_mem_watch) {
        printf("pending_dequeue has dequeued %p == o2_mem_watch\n", msg);
    }
#endif
    o2_prepare_to_deliver(msg);
    return msg;
}

void o2_deliver_pending()
{
    while (pending_anywhere.head) {
        pending_dequeue(&pending_anywhere);
        o2_message_send_sched(true);
    }
    while (pending_local.head) {
        o2_message_ptr msg = pending_dequeue(&pending_local);
        services_entry_ptr services = *o2_services_from_msg(msg);
        service_provider_ptr spp;
        if (services &&
            (spp = o2_proc_service_find(o2_ctx->proc, services))) {
            o2_msg_deliver(spp->service, services);
        } else {
            o2_complete_delivery(); // remove msg and free it
        }
    }
}

void o2_free_pending_msgs()
{
    while (o2_ctx->msgs) {
        o2_complete_delivery();
    }
    while (pending_anywhere.head) {
        pending_dequeue(&pending_anywhere);
        o2_complete_delivery();
    }
    while (pending_local.head) {
        pending_dequeue(&pending_local);
        o2_complete_delivery();
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
        o2_drop_msg_data("of type mismatch", msg);
        return;
    }

    if (handler->parse_args) {
        o2_extract_start(msg);
        o2string typ = handler->type_string;
        if (!typ) { // if handler type_string is NULL, use message types
            typ = types;
        }
        while (*typ) {
            o2_arg_ptr next = o2_get_next((o2_type) (*typ++));
            if (!next) {
                o2_drop_msg_data("of type coercion failure", msg);
                return;
            }
        }
        if (handler->type_string) {
            types = handler->type_string; // so that handler gets coerced types
        }
        assert(o2_ctx->arg_data.allocated >= o2_ctx->arg_data.length);
        assert(o2_ctx->argv_data.allocated >= o2_ctx->argv_data.length);
    } else {
        o2_ctx->argv = NULL;
        o2_ctx->argc = 0;
    }
    (*(handler->handler))(msg, types, o2_ctx->argv, o2_ctx->argc,
                          handler->user_data);
}


static o2_err_t o2_embedded_msgs_deliver(o2_msg_data_ptr msg)
{
    char *end_of_msg = O2_MSG_DATA_END(msg);
    // embedded message starts where ',' of type string should be:
    o2_msg_data_ptr embedded = (o2_msg_data_ptr) (o2_msg_data_types(msg) - 1);
    while (PTR(embedded) < end_of_msg) {
        // need to copy each embedded message before sending
        int len = embedded->length;
        o2_message_ptr message = o2_message_new(len);
        memcpy((char *) &message->data, (char *) embedded,
               len + sizeof(embedded->length));
        message->next = NULL;
        message->data.flags |= O2_TCP_FLAG;
        o2_message_send(message);
        embedded = (o2_msg_data_ptr) O2_MSG_DATA_END(embedded);
    }
    return O2_SUCCESS;
}


void send_message_to_tap(service_tap_ptr tap)
{
    o2_message_ptr msg = o2_ctx->msgs; // we do not own or free this message
    // construct a new message to send to tapper by replacing service name
    // how big is the existing service name?
    // I think coerce to char * will remove bounds checking, which might
    // limit the search to 4 characters since msg->address is declared to
    // be char [4]
    // Skip first character which might be a slash; we want the slash after the
    // service name.
    char *slash = strchr((char *) (msg->data.address) + 1, '/');
    int curlen; // length of "/servicename" without EOS
    if (slash) {
        curlen = (int) (slash - msg->data.address);
    } else {
        curlen = strlen((char *) (msg->data.address));
    }
    // how much space will tapper take?
    int newlen = (int) strlen(tap->tapper) + 1; // add 1 for initial '/' or '!'

    // how long is current address, not including eos?
    int curaddrlen = (int) strlen((char *) (msg->data.address));

    // how long is new address, not including eos?
    int newaddrlen = curaddrlen + (newlen - curlen);

    // what is the difference in space needed for address (and message)?
    // "+ 4" accounts for end-of-string byte and padding in each case
    int curaddrall = ROUNDUP_TO_32BIT(curaddrlen + 1); // address + padding
    int newaddrall = ROUNDUP_TO_32BIT(newaddrlen + 1);
    int extra = newaddrall - curaddrall;

    // allocate a new message
    o2_message_ptr newmsg = o2_message_new(msg->data.length + extra);
    newmsg->data.length = msg->data.length + extra;
    newmsg->data.timestamp = msg->data.timestamp;
    // fill end of address with zeros before creating address string
    int32_t *end = (int32_t *) (newmsg->data.address + newaddrall);
    end[-1] = 0;
    // first character is either / or ! copied from original message
    newmsg->data.address[0] = msg->data.address[0];
    // copies name and EOS:
    memcpy((char *) (newmsg->data.address + 1), tap->tapper, newlen);
    memcpy((char *) (newmsg->data.address + newlen), msg->data.address + curlen,
           curaddrlen - curlen);
    // copy the rest of the message
    int len = ((char *) &msg->data) + msg->data.length +
                    sizeof(msg->data.length) - &msg->data.address[curaddrall];
    memcpy((char *) (newmsg->data.address + newaddrall),
           msg->data.address + curaddrall, len);
    newmsg->data.flags = O2_TCP_FLAG | O2_TAP_FLAG;
    o2_prepare_to_deliver(newmsg); // transfer ownership to o2_ctx->msgs
    // must send message to tap->proc
    if (IS_REMOTE_PROC(tap->proc)) {
        o2_send_remote(tap->proc, true);
    } else {
        services_entry_ptr services = *((services_entry_ptr *)
                o2_lookup(&o2_ctx->path_tree, tap->tapper));
        if (services) {
            service_provider_ptr spp = o2_proc_service_find(o2_ctx->proc,
                                                            services);
            if (spp && spp->service) {
                // newmsg ownership transfers to o2_send_local():
                return o2_send_local(spp->service, services);
            }
        }
    }
}


// deliver msg locally and immediately. If service is not null,
//    assume it is correct, saving the cost of looking it up
//    ownership of msg is transferred to this function
void o2_msg_deliver(o2_node_ptr service, services_entry_ptr ss)
{
    bool delivered = false;
    char *address;
    const char *types;
    // STEP 0: If message is a bundle, send each embedded message separately
    o2_message_ptr msg = o2_ctx->msgs;
#ifndef O2_NO_BUNDLES
    if (IS_BUNDLE(&msg->data)) {
        o2_embedded_msgs_deliver(&msg->data);
        delivered = true;
        goto done;
    }
#endif
    // STEP 1: Check for a service to handle the message
    address = msg->data.address;
    if (!service) {
        service = o2_msg_service(&msg->data, &ss);
        if (!service) goto done; // service must have been removed
    }
    
    // STEP 2: Isolate the type string, which is after the address
    types = o2_msg_types(msg);

    O2_DBl(printf("%s o2_msg_deliver msg %p addr %s\n", o2_debug_prefix,
                  msg, address));

    // STEP 3: If service is a Handler, call the handler directly
    if (service->tag == NODE_HANDLER) {
        o2_call_handler((handler_entry_ptr) service, &msg->data, types);
        delivered = true; // either delivered or warning issued

    // STEP 4: If path begins with '!', or O2_NO_PATTERNS, do a full path lookup
    } else if (service->tag == NODE_HASH
#ifndef O2_NO_PATTERNS
               && (address[0] == '!')
#endif
              ) {
        o2_node_ptr handler;
        // temporary address if service is our @public:internal:port :
        char tmp_addr[O2_MAX_PROCNAME_LEN];
        char *handler_address;
        // '!' allows for direct lookup, but if the service name is
        // @public:internal:port, a straightforward lookup will not find the
        // handler because the the key uses /_o2/....  So translate the local
        // @pip:iip:port service to _o2.
        if (address[1] == '@') {
            // build an address with /_o2/ for lookup. The maximum address
            // length is small because all /_o2/ nodes are built-in and short:
            *((int32_t *) tmp_addr) = *(int32_t *) "/_o2"; // copy "/_o2"
            char *slash_ptr = strchr(address + 4, '/');
            if (!slash_ptr) goto done; // not deliverable because "/_o2" does
                                       // not have a handler
            // leave 4 bytes of extra room at the end to fill with zeros by
            // setting n to size - 8 instead of size - 4:
            o2strcpy(tmp_addr + 4, slash_ptr, O2_MAX_PROCNAME_LEN - 8);
            // make sure address is padded to 32-bit word to make o2string:
            // first, find pointer to the byte AFTER the EOS character:
            char *endptr = tmp_addr + 6 + strlen(tmp_addr + 5);
            *endptr++ = 0; *endptr++ = 0; *endptr++ = 0;  // extra fill
            handler_address = tmp_addr;
        } else {
            address[0] = '/'; // must start with '/' to get consistent hash
            handler_address = address; // use the message address normally
        }
        handler = *o2_lookup(&o2_ctx->full_path_table, handler_address);
        address[0] = '!'; // restore address (if changed) [maybe not needed]
        if (handler && handler->tag == NODE_HANDLER) {
            // Even though we might have done a lookup on /_o2/..., the message
            // passed to the handler will have the original address, which might
            // be something like /_7f00001:7f00001:4321/...
            o2_call_handler((handler_entry_ptr) handler,
                            &msg->data, types);
            delivered = true; // either delivered or warning issued
        }
    }
#ifndef O2_NO_PATTERNS
    // STEP 5: Use path tree to find handler
    else if (service->tag == NODE_HASH) {
        char name[NAME_BUF_LEN];
        address = strchr(address + 1, '/'); // search for end of service name
        if (address) {
            delivered = o2_find_handlers_rec(address + 1, name,
                                (o2_node_ptr) service, &msg->data, types);
        } else { // address is "/service", but "/service" is not a NODE_HANDLER
            o2_drop_message("there is no handler for this address", msg);
        }
    }
#endif
    else { // the assumption that the service is local fails
        o2_drop_message("service is not local", msg);
        delivered = true;
    }

    // STEP 6: if there are tappers, send the message to them as well
    //         (can't tap a tap msg)
    if (!(msg->data.flags & O2_TAP_FLAG)) {
        for (int i = 0; i < ss->taps.length; i++) {
            send_message_to_tap(GET_TAP_PTR(ss->taps, i));
        }
    }

    // STEP 7: remove the message from the stack and free it
  done:
    if (!delivered) {
        o2_drop_message("no handler was found", msg);
        O2_DBsS(o2_node_show((o2_node_ptr) (&o2_ctx->path_tree), 2));
    }
    o2_complete_delivery();
}


// This function is invoked by macros o2_send and o2_send_cmd.
// It expects arguments to end with O2_MARKER_A and O2_MARKER_B
o2_err_t o2_send_marker(const char *path, double time, int tcp_flag,
                          const char *typestring, ...)
{
    va_list ap;
    va_start(ap, typestring);

    o2_message_ptr msg;
    o2_err_t rslt = o2_message_build(&msg, time, NULL, path,
                                     typestring, tcp_flag, ap);
    if (rslt != O2_SUCCESS) {
        return rslt; // could not allocate a message!
    }
    O2_DB((msg->data.address[1] == '_' || msg->data.address[1] == '@') ?
          O2_DBS_FLAG : O2_DBs_FLAG,  // either non-system (s) or system (S)
          printf("%s sending%s (%p) ", o2_debug_prefix,
                 (tcp_flag ? " cmd" : ""), msg);
          o2_msg_data_print(&msg->data);
          printf("\n"));
    return o2_message_send(msg);
}


// This is the externally visible message send function.
// 
// Ownership of message is transferred to o2 system.
o2_err_t o2_message_send(o2_message_ptr msg)
{
    o2_prepare_to_deliver(msg); // transfer msg to o2_ctx->msgs
    return o2_message_send_sched(true);
}


// Internal message send function.
// schedulable is normally true meaning we can schedule messages
// according to their timestamps. If this message was dispatched
// by o2_ltsched, schedulable will be false and we should ignore
// the timestamp, which has already been observed by o2_ltsched.
//
// Before returning, msg is transferred from o2_ctx->msgs.
//
o2_err_t o2_message_send_sched(int schedulable)
{
    o2_message_ptr msg = o2_ctx->msgs; // get the "active" message
    // Find the remote service, note that we skip over the leading '/':
    services_entry_ptr services;
    o2_node_ptr service = o2_msg_service(&msg->data, &services);
    if (!service) {
        o2_complete_delivery(); // remove from o2_ctx->msgs and freej
        o2_drop_message("service was not found", msg);
        return O2_NO_SERVICE;
    } else if (IS_REMOTE_PROC(service)
#ifndef O2_NO_MQTT
               || IS_MQTT_PROC(service)
#endif
              ) { // remote delivery, UDP or TCP or MQTT
        o2_err_t rslt = o2_send_remote(TO_PROC_INFO(service), true);
        return rslt;
#ifndef O2_NO_BRIDGES
    } else if (ISA_BRIDGE(service)) {
        bridge_inst_ptr inst = (bridge_inst_ptr) service;
        if (!schedulable || inst->tag == BRIDGE_SYNCED ||
            msg->data.timestamp == 0.0 ||
            msg->data.timestamp <= o2_gtsched.last_time) {
            // bridge_send must take ownership or free the message
            return (*(inst->proto->bridge_send))(inst);
        } else {
            return o2_schedule(&o2_gtsched); // delivery on time
        }
#endif
#ifndef O2_NO_OSC
    } else if (ISA_OSC(service)) {
        // this is a bit complicated: send immediately if it is a bundle
        // or is not scheduled in the future. Otherwise use O2 scheduling.
        if (!schedulable ||
#ifndef O2_NO_BUNDLES
                IS_BUNDLE(&msg->data) ||
#endif
                msg->data.timestamp == 0.0 ||
                msg->data.timestamp <= o2_gtsched.last_time) {
            // o2_send_osc must take ownership or free the message
            return o2_send_osc((osc_info_ptr) service, services);
        } else {
            return o2_schedule(&o2_gtsched); // delivery on time
        }
#endif
    } else if (schedulable && msg->data.timestamp > 0.0 &&
               msg->data.timestamp > o2_gtsched.last_time) { // local delivery
        return o2_schedule(&o2_gtsched); // local delivery later
    } // else
    o2_send_local(service, services);
    return O2_SUCCESS;
}


// send a message via TCP or UDP to proc, frees the current message
o2_err_t o2_send_remote(proc_info_ptr proc, int block)
{
    if (proc == NULL) {
        o2_drop_message("proc is NULL in o2_send_remote", o2_ctx->msgs);
        o2_complete_delivery(); // remove from o2_ctx->msgs and free
        return O2_NO_SERVICE;
    }
    o2_message_ptr msg = o2_postpone_delivery(); // own the "active" message
#ifndef O2_NO_DEBUG
#ifndef O2_NO_MQTT
    if (IS_MQTT_PROC(proc)) {
        O2_DB(O2_DBs_FLAG | O2_DBS_FLAG | O2_DBq_FLAG,
              o2_msg_data_ptr mdp = &msg->data;
              int sysmsg = mdp->address[1] == '_' || mdp->address[1] == '@';
              int db = (o2_debug & O2_DBq_FLAG) ||
                       (sysmsg ? (o2_debug && O2_DBS_FLAG) :
                                 (o2_debug && O2_DBs_FLAG));
              if (db) {
                  o2_dbg_msg("sending via mqtt", msg, &msg->data,
                             "to", proc->name);
              });
    } else
#endif
    {
        o2_msg_data_ptr mdp = &msg->data;
        bool sysmsg = mdp->address[1] == '_' || mdp->address[1] == '@';
        O2_DB(sysmsg ? O2_DBS_FLAG : O2_DBs_FLAG,
              bool blocking = proc->net_info->out_message && !block;
              const char *desc = (mdp->flags & O2_TCP_FLAG ?
                                  (blocking ? "queueing TCP" : "sending TCP") :
                                  "sending UDP");
              o2_dbg_msg(desc, msg, mdp, "to", proc->name));
    }
#endif
    int tcp_flag = msg->data.flags & O2_TCP_FLAG; // before byte swap
#if IS_LITTLE_ENDIAN
    o2_msg_swap_endian(&msg->data, true);
#endif
    // send the message to remote process
#ifndef O2_NO_MQTT
    if (IS_MQTT_PROC(proc)) {
        o2_mqtt_send(proc, msg);
    } else
#endif
    if (tcp_flag) {
        return o2n_send_tcp(proc->net_info, block, (o2n_message_ptr) msg);
    } else { // send via UDP
        o2_err_t rslt = o2n_send_udp(&proc->udp_address,
                                       (o2n_message_ptr) msg);
        if (rslt != O2_SUCCESS) {
            O2_DBn(printf("o2_send_remote error, port %d\n",
                          o2n_address_get_port(&proc->udp_address)));
            return O2_FAIL;
        }
    }
    return O2_SUCCESS;
}
