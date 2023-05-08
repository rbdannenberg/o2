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
// o2_message_send(O2message_ptr msg)
//         (Defined in msgsend.c)
//         is the general "bottleneck" for sending messages -- every
//         complete schedulablemessage is delivered by calling this.
//         Basically decides to schedule by calling o2_schedule() or
//         send now by calling o2_service_msg_send()
// o2_service_msg_send(O2node *service, Services_entry *services)
//         (Defined in msgsend.cpp)
//         Decides to use a proxy's send method for special cases, or
//         makes a standard local delivery with o2_send_local()
// o2_send_local(O2node *service, Services_entry *ss)
//         (Defined in msgsend.c)
//         Either sends a message locally or queues it to avoid
//         reentrant message delivery. Calls o2_msg_deliver() directly,
//         or o2_msg_deliver() is called by o2_deliver_pending().
//         Initially, message is owned by o2_ctx->msgs. Upon return,
//         msg is either freed or transferred to a queue of some kind.
// o2_schedule(O2sched_ptr sched)
//         (Defined in sched.c)
//         Schedule message if time is in the future, or directly call
//         o2_msg_deliver() if it is time to send the message. Three
//         cases are: (1) Scheduled message ownership is transferred
//         from o2_ctx->msgs to the scheduling queue, (2) Undeliverable
//         message is freed, (3) Immediate delivery uses o2_msg_deliver
//         which frees the message.
// sched_dispatch(O2sched_ptr s, O2time run_until_time)
//         (Defined in sched.c)
//         Dispatches messages by transferring ownership from queue to
//         o2_ctx->msgs and calling o2_msg_send_now().
// o2_msg_deliver(bool tcp_flag, O2node *service)
//         (Defined in msgsend.c)
//         delivers a message or bundle locally. Calls
//         o2_embedded_msgs_deliver() if this is a bundle. Or, calls 
//         service's invoke() method if service is a Handler or Handler's
//         invoke() method if path is in full path table, or uses 
//         o2_find_handlers_rec() to find handler(s). Initially, message
//         ownership on o2_ctx->msgs. Upon return, the message is freed.
//         This function also calls msg_send_to_tap() for each tapper
//         of the service.
// msg_send_to_tap(Service_tap *tap)
//         (Defined in msgsend.c)
//         Copies message content, changing the address to that of
//         the tapper. Calls either Proxy_info::send() method or
//         o2_send_local().
//         (Can't simply call o2_message_send_sched() because tap
//         messages must be sent to a specific process even if some
//         other process offers the service and has a higher
//         public:internal:port
//         address (priority). The copied message is transferred to
//         o2_ctx->msgs (which is why we need this to be a list and
//         not just remember a single message). Either o2_send_local()
//         or Proxy_info::send() take ownership from o2_ctx->msgs.
// o2_embedded_msgs_deliver(O2msg_data_ptr msg, bool tcp_flag)
//         Deliver or schedule messages in a bundle (recursively).
//         Calls o2_message_send_sched() to deliver each embedded
//         message, which is copied into an O2message and transferred
//         to o2_ctx->msgs.
// o2_find_handlers_rec()
//         does lookup of path locally and calls handler. msg is just the
//         data part and the full message is held somewhere in the call
//         stack.
// sched_dispatch()
//         (Defined in o2sched.cpp)
//         Calls o2_msg_send_now() to find service and Services_entry
// o2_msg_send_now()
//         (Defined in msgsend.cpp)
//         calls o2_service_msg_send()
//
// SUMMARY:
//
// o2_message_send()
//   CALLS o2_schedule()
//   OR o2_service_msg_send
//        CALLS send method of a proxy
//        OR o2_send_local()
//          CALLS o2_msg_deliver()
//            CALLS o2_embedded_msgs_deliver()
//              CALLS o2_message_send()  ------> recursion
//            OR service's invoke() method
//            OR Handler node's invoke() method
//            OR o2_find_handlers_rec()
//              CALLS self recursively
//              AND/OR invoke() method on handler(s)
//            AND/OR o2_msg_send_to_tap()
//              CALLS proxy send() method
//              OR o2_send_local() -------> recursion
//          OR queues message and o2_msg_deliver() is called later
// o2_sched_poll()
//   CALLS sched_dispatch()
//     CALLS o2_msg_send_now()
//       CALLS o2_service_msg_send() ------> see above
//
// Message parsing and forming O2argv with message parameters is not
// reentrant since there is a global buffer used to store coerced
// parameters. Therefore, if you call a handler and the handler sends a
// message, we cannot deliver it immediately, at least not if it has a
// local destination. Therefore, messages sent from handlers may be
// saved on a list and dispatched later. 


#include "ctype.h"
#include "o2.h"
#include "o2internal.h"
#include "debug.h"
#include "services.h"
#include "msgsend.h"
#include "message.h"
#include "pathtree.h"
#include "o2osc.h"
//#include "discovery.h"
#include "o2sched.h"

#include <errno.h>


// to prevent deep recursion, messages go into a queue if we are already
// delivering a message via o2_msg_deliver:
int o2_do_not_reenter = 0; // counter to allow nesting

// we have two pending queues: one for normal messages and one for
// local delivery (needed for taps)
Pending_msgs_queue o2_pending_local;
Pending_msgs_queue o2_pending_anywhere;


#ifndef O2_NO_DEBUG
const char *pending_msgs_name(Pending_msgs_queue *p)
{
    return ((p == &o2_pending_anywhere) ? "anywhere" : "local");
}
#endif

Pending_msgs_queue::Pending_msgs_queue()
{
    head = NULL; tail = NULL;
}

void Pending_msgs_queue::enqueue(O2message_ptr msg)
{
    if (tail) {
        tail->next = msg;
        tail = msg;
    } else {
        head = tail = msg;
    }
}

O2message_ptr Pending_msgs_queue::dequeue() {
    O2message_ptr msg = head;
    assert(msg);
    if (head == tail) {
        head = tail = NULL;
    } else {
        head = head->next;
    }
    #ifndef O2_NO_DEBUG
    extern void *o2_mem_watch;
    if (msg == o2_mem_watch) {
        printf("Pending_msgs_queue::dequeue %p == o2_mem_watch\n", msg);
    }
    #endif
    O2_DBl(o2_dbg_msg("Pending_msgs_queue::dequeue", msg, &msg->data, "from",
                      pending_msgs_name(this)));
    return msg;
}

bool Pending_msgs_queue::empty()
{
    return head == NULL;
}


// When a message is pulled from pending to be delivered, it is saved
// here in case the user calls exit() before the message can be freed.
// exit() will call o2_finish() which will free the message. This is
// not actually a memory leak since the message will have come from a
// chunk that will be freed in any case, but this will remove the
// *appearance* of a leak so we can focus on actual problems (if any).
O2message_ptr o2_active_message = NULL;


void o2_drop_msg_data(const char *warn, O2msg_data_ptr data)
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


void o2_drop_message(const char *warn, bool free_the_msg)
{
    O2message_ptr msg = o2_current_message();
    o2_drop_msg_data(warn, &msg->data);
    if (free_the_msg) {
        o2_complete_delivery();
    }
}


// push a message onto the o2_ctx->msgs list
//
void o2_prepare_to_deliver(O2message_ptr msg)
{
    msg->next = o2_ctx->msgs;
    o2_ctx->msgs = msg;
}


O2message_ptr o2_current_message()
{
    return o2_ctx->msgs;
}


// free the current message from o2_ctx->msgs
// 
void o2_complete_delivery()
{
    O2_FREE(o2_postpone_delivery());
}


// remove the current message from o2_ctx->msgs and return
// it so we can hand it off to another owner
// 
O2message_ptr o2_postpone_delivery()
{
    assert(o2_ctx->msgs);
    O2message_ptr msg = o2_ctx->msgs;
    o2_ctx->msgs = msg->next;
    msg->next = NULL; // debugging aid, remove needless pointer
    return msg;
}


// o2_send_local delivers a message immediately and locally to a service,
// but it is safe to call because it is reentrant by deferring delivery
// if needed. Ownership of msg is initially with o2_ctx. Ownership is
// transferred from o2_ctx before returning to caller.
void o2_send_local(O2node *service, Services_entry *ss)
{
    if (o2_do_not_reenter) {
        O2message_ptr msg = o2_postpone_delivery();
        Pending_msgs_queue *p = ((msg->data.misc & O2_TAP_FLAG) ?
                                 &o2_pending_local : &o2_pending_anywhere);
        O2_DBl(o2_dbg_msg("o2_send_local defer", msg, &msg->data, "from",
                             (p == &o2_pending_anywhere) ? "anywhere" : "local"));
        p->enqueue(msg);
    } else {
        o2_do_not_reenter++;
        o2_msg_deliver(service, ss);
        o2_do_not_reenter--;
    }
}

void o2_deliver_pending()
{
    while (!o2_pending_anywhere.empty()) {
        o2_message_send(o2_pending_anywhere.dequeue());
    }
    while (!o2_pending_local.empty()) {
        O2message_ptr msg = o2_pending_local.dequeue();
        Services_entry *services = *Services_entry::find_from_msg(msg);
        Service_provider *spp;
        if (services && (spp = services->proc_service_find(o2_ctx->proc)) &&
            HANDLER_IS_LOCAL(spp->service)) {
            o2_prepare_to_deliver(msg);
            o2_msg_deliver(spp->service, services);
        } else {  // something strange happened: we deferred a message for a
            // local handler, but now the service is not found or is not local
            O2_FREE(msg);
        }
    }
}

void o2_free_pending_msgs()
{
    while (o2_ctx->msgs) {
        o2_complete_delivery();
    }
    while (!o2_pending_anywhere.empty()) {
        O2_FREE(o2_pending_anywhere.dequeue());
    }
    while (!o2_pending_local.empty()) {
        O2_FREE(o2_pending_local.dequeue());
    }

}

#ifndef O2_NO_BUNDLES
static O2err o2_embedded_msgs_deliver(O2msg_data_ptr msg)
{
    char *end_of_msg = O2_MSG_DATA_END(msg);
    // embedded message starts where ',' of type string should be:
    O2msg_data_ptr embedded = (O2msg_data_ptr) (o2_msg_data_types(msg) - 1);
    while (PTR(embedded) < end_of_msg) {
        // need to copy each embedded message before sending
        int len = embedded->length;
        O2message_ptr message = o2_message_new(len);
        memcpy((char *) &message->data, (char *) embedded,
               len + sizeof(embedded->length));
        message->next = NULL;
        message->data.misc |= O2_TCP_FLAG;
        o2_message_send(message);
        embedded = (O2msg_data_ptr) O2_MSG_DATA_END(embedded);
    }
    return O2_SUCCESS;
}
#endif


void msg_send_to_tap(Service_tap *tap)
{
    O2message_ptr msg = o2_ctx->msgs; // we do not own or free this message
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
        curlen = (int) strlen((char *) (msg->data.address));
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
    O2message_ptr newmsg = o2_message_new(msg->data.length + extra);
    newmsg->data.length = msg->data.length + extra;
    // determine whether to send by TCP or UDP, and retain TAP flag and ttl:
    newmsg->data.misc = ((tap->send_mode == TAP_KEEP ? msg->data.misc :
                           (tap->send_mode == TAP_RELIABLE ? O2_TCP_FLAG :
                            O2_UDP_FLAG)) |
                          O2_TAP_FLAG);
    newmsg->data.misc |= (msg->data.misc & 0xFF00); // copy TTL field
    newmsg->data.timestamp = msg->data.timestamp;
    // fill end of address with zeros before creating address string
    int32_t *end = (int32_t *) (newmsg->data.address + newaddrall);
    end[-1] = 0;
    // first character is either / or ! copied from original message
    newmsg->data.address[0] = msg->data.address[0];
    // copies name and EOS:
    memcpy((char *) (newmsg->data.address + 1), tap->tapper, newlen);
    memcpy((char *) (newmsg->data.address + newlen), 
           msg->data.address + curlen, curaddrlen - curlen);
    // copy the rest of the message
    int len = (int) (((char *) &msg->data) + msg->data.length +
                  sizeof(msg->data.length) - &msg->data.address[curaddrall]);
    memcpy((char *) (newmsg->data.address + newaddrall),
           msg->data.address + curaddrall, len);
    o2_prepare_to_deliver(newmsg); // transfer ownership to o2_ctx->msgs
    O2_DBp(printf("%s tap send from %s to %s at %s\n", o2_debug_prefix, 
                  msg->data.address, newmsg->data.address, tap->proc->key));
    // must send message to tap->proc
    if (ISA_REMOTE_PROC(tap->proc)) {  // send to remote process
        tap->proc->send(true);
        return;
    } else {
        Services_entry *services = *((Services_entry **)
                  o2_ctx->path_tree.lookup(tap->tapper));
        if (services) {
            Service_provider *spp = services->proc_service_find(o2_ctx->proc);
            if (spp && spp->service) {
                if (HANDLER_IS_LOCAL(spp->service)) {  // send to local service
                    // newmsg ownership transfers to o2_send_local():
                    o2_send_local(spp->service, services);
                    return;
                } else if (ISA_PROXY(spp->service)) {  // send to OSC or BRIDGE
                    Proxy_info *proxy = (Proxy_info *) spp->service;
                    proxy->send(true);
                    return;
                }
            }
        }
    }
    // tap is not a remote proc, a local handler or a proxy, so maybe
    // the tap is no longer valid. I don't know how this could happen.
    o2_drop_message("Tapper not found", true);
}


// deliver msg locally and immediately. If service is not null,
//    assume it is correct, saving the cost of looking it up
//    ownership of msg is transferred to this function
void o2_msg_deliver(O2node *service, Services_entry *ss)
{
    bool delivered = false;
    char *address;
    const char *types;
    // STEP 0: If message is a bundle, send each embedded message separately
    O2message_ptr msg = o2_current_message();
#ifndef O2_NO_BUNDLES
    if (IS_BUNDLE(&msg->data)) {
        o2_embedded_msgs_deliver(&msg->data);
        delivered = true;
        goto done;
    }
#endif
    // STEP 1: Check for a service to handle the message
    address = msg->data.address;
    assert(*address == '/' || *address == '!');
    if (!service) {
        service = o2_msg_service(&msg->data, &ss);
        if (!service) goto done; // service must have been removed
    }
    
    // STEP 2: Isolate the type string, which is after the address
    types = o2_msg_types(msg);

    O2_DBl(printf("%s o2_msg_deliver msg %p addr %s\n", o2_debug_prefix,
                  msg, address));

    // STEP 3: If service is a Handler, call the handler directly
    if (ISA_HANDLER(service)) {
        ((Handler_entry *) service)->invoke(&msg->data, types);
        delivered = true; // either delivered or warning issued

    // STEP 4: If path begins with '!', or O2_NO_PATTERNS, do a full path lookup
    } else if (ISA_HASH(service)
#ifndef O2_NO_PATTERNS
               && (address[0] == '!')
#endif
              ) {
        O2node *handler;
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
            o2_strcpy(tmp_addr + 4, slash_ptr, O2_MAX_PROCNAME_LEN - 8);
            // make sure address is padded to 32-bit word to make O2string:
            // first, find pointer to the byte AFTER the EOS character:
            char *endptr = tmp_addr + 6 + strlen(tmp_addr + 5);
            *endptr++ = 0; *endptr++ = 0; *endptr++ = 0;  // extra fill
            handler_address = tmp_addr;
        } else {
            address[0] = '/'; // must start with '/' to get consistent hash
            handler_address = address; // use the message address normally
        }
        handler = *o2_ctx->full_path_table.lookup(handler_address);
        address[0] = '!'; // restore address (if changed) [maybe not needed]
        if (handler && ISA_HANDLER(handler)) {
            // Even though we might have done a lookup on /_o2/..., the message
            // passed to the handler will have the original address, which might
            // be something like /_7f00001:7f00001:4321/...
            ((Handler_entry *) handler)->invoke(&msg->data, types);
            delivered = true; // either delivered or warning issued
        }
    }
#ifndef O2_NO_PATTERNS
    // STEP 5: Use path tree to find handler
    else if (ISA_HASH(service)) {
        char name[NAME_BUF_LEN];
        address = strchr(address + 1, '/'); // search for end of service name
        if (address) {
            delivered = o2_find_handlers_rec(address + 1, name,
                                             service, &msg->data, types);
        } else { // address is "/service", but "/service" is not a HANDLER
            o2_drop_message("there is no handler for this address", false);
        }
    }
#endif
    else { // the assumption that the service is local fails
        o2_drop_message("service is not local", false);
        delivered = true;
    }

    // STEP 6: if there are tappers, send the message to them as well
    o2_send_to_taps(msg, ss);

    // STEP 7: remove the message from the stack and free it
  done:
    if (!delivered) {
        o2_drop_message("no handler was found", false);
        // Too verbose? O2_DBsS(o2_ctx->show_tree());
    }
    o2_complete_delivery();
}


void o2_send_to_taps(O2message_ptr msg, Services_entry *ss)
{
    msg->data.misc |= O2_TAP_FLAG;
    msg->data.misc += (1 << 8);  // increment TTL field
    if ((msg->data.misc >> 8) <= O2_MAX_TAP_FORWARDING) {
        for (int i = 0; i < ss->taps.size(); i++) {
            msg_send_to_tap(&ss->taps[i]);
        }
    }
}




// This function is invoked by macros o2_send and o2_send_cmd.
// It expects arguments to end with O2_MARKER_A and O2_MARKER_B
O2err o2_send_marker(const char *path, double time, bool tcp_flag,
                          const char *typestring, ...)
{
    va_list ap;
    va_start(ap, typestring);

    O2message_ptr msg;
    RETURN_IF_ERROR(o2_message_build(&msg, time, NULL, path,
                                     typestring, tcp_flag, ap));
    O2_DB((msg->data.address[1] == '_' || msg->data.address[1] == '@') ?
          O2_DBS_FLAG : O2_DBs_FLAG,  // either non-system (s) or system (S)
          printf("%s sending%s (%p) ", o2_debug_prefix,
                 (tcp_flag ? " cmd" : ""), msg);
          o2_msg_data_print(&msg->data);
          printf("\n"));
    return o2_message_send(msg);
}


O2err o2_service_msg_send(O2node *service, Services_entry *services)
{
    if (!service) {
        o2_drop_message("service was not found", true);
        return O2_NO_SERVICE;
    } else if (ISA_PROXY(service)) {
        Proxy_info *ri = (Proxy_info *) service;
        return ri->send(true);
    } else {
        o2_send_local(service, services);
        return O2_SUCCESS;
    }
}


// This is the externally visible message send function.
// 
// Ownership of message is transferred to o2 system.
// Assume that msg is schedulable
O2err o2_message_send(O2message_ptr msg)
{
    o2_prepare_to_deliver(msg);
    // Find the remote service, note that we skip over the leading '/':
    Services_entry *services;
    O2node *service = o2_msg_service(&msg->data, &services);
    if (service) {
        if (ISA_PROXY(service)) {
            Proxy_info *ri = (Proxy_info *) service;
            if (msg->data.timestamp > 0.0 &&
                msg->data.timestamp > o2_gtsched.last_time &&
                ri->schedule_before_send()) {
                return o2_schedule(&o2_gtsched);
            }
        } else if (msg->data.timestamp > 0.0 &&
               msg->data.timestamp > o2_gtsched.last_time) { // local delivery
            return o2_schedule(&o2_gtsched); // local delivery later
        }
    }
    return o2_service_msg_send(service, services);
}

// version that assumes not schedulable: send it now
O2err o2_msg_send_now()
{
    O2message_ptr msg = o2_current_message(); // get the "active" message
    // Find the remote service, note that we skip over the leading '/':
    Services_entry *services;
    O2node *service = o2_msg_service(&msg->data, &services);
    return o2_service_msg_send(service, services);
}


