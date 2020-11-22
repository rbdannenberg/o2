// bridge.c -- support extensions to non-IP transports
//
// Roger B. Dannenberg
// April 2018

#ifndef O2_NO_BRIDGES

#include "o2.h"
#include "o2internal.h"
#include "bridge.h"
#include "services.h"
#include "message.h"
#include "msgsend.h"
#include "pathtree.h"
#include "discovery.h"

static bool bridges_initialized;
static dyn_array bridges;

bridge_protocol_ptr o2_bridge_new(const char *name, bridge_fn bridge_poll,
                                  bridge_fn bridge_send, bridge_fn bridge_recv,
                                  bridge_fn bridge_inst_finish,
                                  bridge_fn bridge_finish)
{
    if (!o2_ensemble_name) {
        return NULL;
    }
    bridge_protocol_ptr protocol = O2_MALLOCT(bridge_protocol);
    strncpy(protocol->protocol, name, 8);
    protocol->protocol[7] = 0; // make sure string is terminated
    protocol->bridge_poll = bridge_poll;
    protocol->bridge_send = bridge_send;
    protocol->bridge_recv = bridge_recv;
    protocol->bridge_inst_finish = bridge_inst_finish;
    protocol->bridge_finish = bridge_finish;
    o2_bridges_initialize(); // just in case it's not yet
    DA_APPEND(bridges, bridge_protocol_ptr, protocol);
    return protocol;
}


bridge_inst_ptr o2_bridge_inst_new(bridge_protocol_ptr protocol, void *info)
{
    bridge_inst_ptr inst = O2_MALLOCT(bridge_inst);
    inst->tag = BRIDGE_NOCLOCK;
    inst->proto = protocol;
    inst->info = info;
    return inst;
}


#ifndef O2_NO_DEBUG
void o2_bridge_show(bridge_inst_ptr bridge)
{
    printf(" bridge protocol %s info %p\n", bridge->proto->protocol,
           bridge->info);
}
#endif


void o2_bridges_initialize(void)
{
    if (!bridges_initialized) {
        DA_INIT(bridges, bridge_protocol_ptr, 2);
    }
    bridges_initialized = true;
}


void o2_bridges_finish(void)
{
    while (bridges.length > 0) {
        o2_bridge_remove(DA_GET(bridges, bridge_protocol_ptr, 0)->protocol);
    }
    DA_FINISH(bridges);
}


// search for matching protocol among bridges
// return the index of the bridge if found, otherwise -1
//
int o2_bridge_find_protocol(const char *name, bridge_protocol_ptr *protocol)
{
    for (int i = 0; i < bridges.length; i++) {
        *protocol = DA_GET(bridges, bridge_protocol_ptr, i);
        if (streql((*protocol)->protocol, name)) {
            return i;
        }
    }
    return -1;
}


void o2_bridge_inst_free(bridge_inst_ptr bi)
{
    (*bi->proto->bridge_inst_finish)(bi);
    O2_FREE(bi);
}

// remove all services that delegate to this bridge. If info is not
// null, remove only services whose inst->info matches info (typically
// there is a different info for each bridged process, so this is used
// to remove only the services that delegate via the protocol to that
// specific process.
o2_err_t o2_bridge_remove_services(bridge_protocol_ptr protocol, void *info)
{
    o2_err_t result = O2_SUCCESS;
    // since this might cause us to rehash services, first make a list
    dyn_array services_list;
    o2_list_services(&services_list);
    for (int j = 0; j < services_list.length; j++) {
        services_entry_ptr services =
            DA_GET(services_list, services_entry_ptr, j);
        for (int k = 0; k < services->services.length; k++) {
            service_provider_ptr spp =
                GET_SERVICE_PROVIDER(services->services, k);
            bridge_inst_ptr inst = (bridge_inst_ptr) (spp->service);
            if (ISA_BRIDGE(inst) && inst->proto == protocol &&
                (info == NULL || inst->info == info)) {
                if (o2_service_remove(services->key, o2_ctx->proc,
                                      services, k)) {
                    result = O2_FAIL; // this should never happen
                }
                O2_FREE(inst); // free the bridge_inst for service
                break; // can only be one service offered by proc, and maybe
                // even services was removed, so we should move on to the
                // next service in services list
            }
        }
    }
    DA_FINISH(services_list);
    return result;
}


o2_err_t o2_bridge_remove(const char *protocol_name)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    o2_err_t result = O2_SUCCESS;
    // find and remove from bridges (linear search)
    bridge_protocol_ptr protocol;
    int i = o2_bridge_find_protocol(protocol_name, &protocol);
    if (i >= 0) {
        result = o2_bridge_remove_services(protocol, NULL);
        (*protocol->bridge_finish)(NULL);
        O2_FREE(protocol);
        DA_REMOVE(bridges, bridge_protocol_ptr, i);
        return result;
    }
    return O2_FAIL;
}


int o2_poll_bridges(void)
{
    if (!bridges_initialized) return O2_FAIL;
    for (int i = 0; i < bridges.length; i++) {
        bridge_protocol_ptr proto = DA_GET(bridges, bridge_protocol_ptr, i);
        if (proto->bridge_poll) (*proto->bridge_poll)(NULL);
    }
    return O2_SUCCESS;
}


/************* IMPLEMENTATION OF O2-SIDE O2LITE PROTOCOL **************/

#define O2LITE_BRIDGE(i) DA_GET(o2lite_bridges, bridge_inst_ptr, i)

bridge_protocol_ptr o2lite_bridge = NULL; // indicates we are active
static dyn_array o2lite_bridges;

// o2lite_inst is allocated once and stored on the bridge_inst that is
// referenced by the o2n_info for the TCP connection. It is also
// referenced by every bridge_inst created as a service.
//
// If clock sync is obtained, we have to search all services to find
// those that point to the corresponding o2lite_inst and set their tag
// to BRIDGE_SYNCED.
//
typedef struct o2lite_inst {
    o2n_info_ptr net_info; // where is the connection for this instance?
    o2n_address udp_address; // how to send for o2lite to the bridged process
} o2lite_inst, *o2lite_inst_ptr;


// o2lite needs no polling function since it shares the o2n_ API


o2_err_t o2lite_send(bridge_inst_ptr inst)
{
    // we have a message to send to the service via o2lite -- find
    // socket and send it
    o2lite_inst_ptr o2lite = (o2lite_inst_ptr) inst->info;
    o2_message_ptr msg = o2_postpone_delivery();
    bool use_tcp = (msg->data.flags & O2_TCP_FLAG) != 0;
#if IS_LITTLE_ENDIAN
    o2_msg_swap_endian(&msg->data, true);
#endif
    if (use_tcp) {
        return o2n_send_tcp(o2lite->net_info, true, (o2n_message_ptr) msg);
    } else {
        return o2n_send_udp(&o2lite->udp_address, (o2n_message_ptr) msg);
    }
}


static void o2lite_clock_status_change(services_entry_ptr services,
                                       service_provider_ptr spp)
{
    O2_DBk(printf("%s o2lite_clock_status_change sends /si \"%s\" "
                  "O2_BRIDGE(%d) proc \"_o2\" properties \"%s\"\n",
                  o2_debug_prefix, services->key, O2_BRIDGE,
                  spp->properties ? spp->properties + 1 : ""));
    o2_send_cmd("!_o2/si", 0.0, "siss", services->key, O2_BRIDGE,
                "_o2", spp->properties ? spp->properties + 1 : "");
}


o2_err_t o2lite_recv(bridge_inst_ptr inst)
{
    return o2_message_send_sched(true);
}


// o2lite_inst_finish -- finalize an o2lite bridge_inst_ptr
//
// these are used both for services and as an o2n_info->application.
// If it's the latter, we need to remove every service that delegates
// to this o2lite bridge connection. 
// 
o2_err_t o2lite_inst_finish(bridge_inst_ptr inst)
{
    o2lite_inst_ptr o2lite = (o2lite_inst_ptr) (inst->info);
    if (o2lite) {
        // are we the o2n_info->application? If so, socket is closing
        // so we need to search all services using the connection and
        // remove them.
        if (o2lite->net_info->application == (void *) inst) {
            // remove all sockets serviced by this connection
            o2_bridge_remove_services(inst->proto, o2lite);
            inst->info = NULL; // prevent closing socket from calling again
            if (o2lite->net_info) {
                    o2n_close_socket(o2lite->net_info);
            }
            // free the o2lite_inst reference from o2lite_bridges
            for (int i = 0; i < o2lite_bridges.length; i++) {
                if (O2LITE_BRIDGE(i)->info == o2lite) {
                    DA_SET(o2lite_bridges, bridge_inst_ptr, i, NULL);
                } 
            }
            O2_FREE(o2lite);
        } // otherwise, this is just the removal of a service,
    }     // so we just need to free inst, which is done by caller
    return O2_SUCCESS;
}


// o2lite_dy_handler - generic bridge discovery handler for !_o2/o2lite/dy
//   message parameters are ensemble, port
void o2lite_dy_handler(o2_msg_data_ptr msgdata, const char *types,
                       o2_arg_ptr *argv, int argc, const void *user_data)
{
    O2_DBd(o2_dbg_msg("o2lite_dy_handler gets", NULL, msgdata, NULL, NULL));
    // get the arguments: ensemble name, protocol name,
    // if this is "O2lite" protocol, get ip as string,
    //  tcp port, discovery port.
    
    
    if (!streql(argv[0]->s, o2_ensemble_name)) {
        O2_DBd(printf("    Ignored: ensemble name %s is not %s\n", 
                      argv[0]->s, o2_ensemble_name));
        return;
    }
    // assumes o2lite is initialized, but it must be
    // because the handler is installed
    const char *ip = argv[1]->s;
    int port = argv[2]->i32;
    
    // send !_o2/dy back to bridged process:
    o2n_address address;
    o2_err_t err = o2n_address_init(&address, ip, port, false);
    O2_DBd(if (err) printf("%s o2lite_dy_handler: ip %s, udp %d, err %s\n", \
                    o2_debug_prefix, ip, port, o2_error_to_string(err)));
    o2_send_start();
    o2_message_ptr msg = o2_make_dy_msg(o2_ctx->proc, false, O2_DY_INFO);
    o2n_send_udp(&address, (o2n_message_ptr) msg); // send and free the message
}


// returns id and places new bridge at location id in o2lite_bridges
static int make_o2lite_bridge_inst(const char *ip, int udp)
{
    o2lite_inst_ptr o2lite;
    int i = 0;
    // find place to put it and sequence number
    while (i < o2lite_bridges.length) {
        if (!O2LITE_BRIDGE(i)) goto got_i;
        i++;
    }
    DA_EXPAND(o2lite_bridges, bridge_inst_ptr);
  got_i:
    // put the new bridge instance at i
    o2lite = O2_CALLOCT(o2lite_inst);
    o2lite->net_info = o2n_message_source;
    o2n_address_init(&o2lite->udp_address, ip, udp, false);
    bridge_inst_ptr bi = o2_bridge_inst_new(o2lite_bridge, o2lite);
    o2n_message_source->application = (void *) bi;
    DA_SET(o2lite_bridges, bridge_inst_ptr, i, bi);
    return i;
}


// o2lite_con_handler - handler for !_o2/o2lite/con; called immediately
//   after o2lite bridged process makes a TCP connection, message
//   parameters are ip address and port number for bridged process
//
void o2lite_con_handler(o2_msg_data_ptr msgdata, const char *types,
                        o2_arg_ptr *argv, int argc, const void *user_data)
{
    O2_DBd(o2_dbg_msg("o2lite_con_handler gets", NULL, msgdata, NULL, NULL));
    // get the arguments: ensemble name, protocol name,
    // if this is "O2lite" protocol, get ip as string,
    //  tcp port, discovery port.
    // assumes o2lite is initialized, but it must be
    // because the handler is installed
    const char *ip = argv[0]->s;
    int port = argv[1]->i32;
    // find the o2_info that received this message: this represents
    // the accepted TCP port. It points to a proc_info. Free it.
    O2_FREE(o2n_message_source->application);
    // replace with a bridge_inst
    int id = make_o2lite_bridge_inst(ip, port);
    // reply with id
    o2_send_start();
    o2_add_int32(id);
    o2_message_ptr msg = o2_message_finish(0.0, "!_o2/id", true);
    O2_DBd(o2_dbg_msg("o2lite_con_handler sending", msg, &msg->data,
                      NULL, NULL));
#if IS_LITTLE_ENDIAN
    o2_msg_swap_endian(&msg->data, true);
#endif
    o2_err_t err = o2n_send_tcp(o2n_message_source, false,
                                (o2n_message_ptr) msg);
    if (err) {
        char errmsg[80];
        snprintf(errmsg, 80, "o2lite_con_handler error sending id %s",
                 o2_error_to_string(err));
        o2_drop_msg_data(errmsg, msgdata);
    }
}
       

// Handler for !_o2/o2lite/sv message. This is to create/modify a
// service/tapper for o2lite client. Parameters are: id, service-name,
// exists-flag, service-flag, and tapper-or-properties string.
//
void o2lite_sv_handler(o2_msg_data_ptr msgdata, const char *types,
                        o2_arg_ptr *argv, int argc, const void *user_data)
{
    O2_DBd(o2_dbg_msg("o2lite_sv_handler gets", NULL, msgdata, NULL, NULL));
    // get the arguments: ensemble name, protocol name,
    // if this is "O2lite" protocol, get ip as string,
    //  tcp port, discovery port.
    // assumes o2lite is initialized, but it must be
    // because the handler is installed
    // int id = argv[0]->i32; // ignore id since we have o2n_message_source
    const char *serv = argv[1]->s;
    bool add = argv[2]->i;
    bool is_service = argv[3]->i;
    const char *prtp = argv[4]->s;
    bridge_inst_ptr inst = TO_BRIDGE_INST(o2n_message_source->application);
    o2lite_inst_ptr o2lite = (o2lite_inst_ptr) inst->info;
    o2_err_t err = O2_SUCCESS;
    if (add) { // add a new service or tap
        if (is_service) {
            // copy the bridge inst and share o2lite info:
            bridge_inst_ptr bi = o2_bridge_inst_new(inst->proto, o2lite);
            bi->tag = inst->tag;
            err = o2_service_provider_new(serv, prtp, (o2_node_ptr) bi,
                                          o2_ctx->proc);
            if (err) { // oops, didn't work, so free allocations
                O2_FREE(bi);
            }
        } else { // add tap
            err = o2_tap_new(serv, o2_ctx->proc, prtp);
        }
    } else {
        if (is_service) { // remove a service
            err = o2_service_remove(serv, o2_ctx->proc, NULL, -1);
        } else { // remove a tap
            err = o2_tap_remove(serv, o2_ctx->proc, prtp);
        }
    }
    if (err) {
        char errmsg[100];
        snprintf(errmsg, 100, "o2lite/sv handler got %s for service %s",
                 o2_error_to_string(err), serv);
        o2_drop_msg_data(errmsg, msgdata);
    }
}


// Handler for !_o2/o2lite/cs/get message. This is to get the time from
// an o2lite client. Parameters are: id, sequence-number, reply-to
//
void o2lite_csget_handler(o2_msg_data_ptr msgdata, const char *types,
                          o2_arg_ptr *argv, int argc, const void *user_data)
{
    O2_DBk(o2_dbg_msg("o2lite_csget_handler gets", NULL, msgdata, NULL, NULL));
    // get the arguments: ensemble name, protocol name,
    // if this is "O2lite" protocol, get ip as string,
    //  tcp port, discovery port.
    // assumes o2lite is initialized, but it must be
    // because the handler is installed
    int id = argv[0]->i32;
    int seqno = argv[1]->i32;
    const char *replyto = argv[2]->s;
    // find the o2lite_inst record for this id
    if (id < 0 || id >= o2lite_bridges.length) {
        o2_drop_msg_data("bad id in o2lite/cs/get message", msgdata);
        return; // bad 
    }
    o2lite_inst_ptr o2lite = (o2lite_inst_ptr) (O2LITE_BRIDGE(id)->info);
    if (!o2_clock_is_synchronized) {  // can't reply with time
        o2_drop_msg_data("no global time yet for o2lite/cs/get message",
                         msgdata);
        return;
    }
    o2_send_start();
    o2_add_int32(seqno);
    // should we get the time again? It would be a little more accurate
    // than o2_global_now, but it's more computation
    o2_add_time(o2_global_now);
    o2_message_ptr msg = o2_message_finish(0, replyto, false);
    O2_DBk(o2_dbg_msg("o2lite_csget_handler sends", msg, &msg->data,
                      NULL, NULL));
#if IS_LITTLE_ENDIAN
    o2_msg_swap_endian(&msg->data, true);
#endif
    o2n_send_udp(&o2lite->udp_address, (o2n_message_ptr) msg);
}


// Handler for !_o2/o2lite/cs/cs message. This is to announce the
// o2lite client has clock sync. Parameters are: id
//
void o2lite_cscs_handler(o2_msg_data_ptr msgdata, const char *types,
                         o2_arg_ptr *argv, int argc, const void *user_data)
{
    O2_DBd(o2_dbg_msg("o2lite_cscs_handler gets", NULL, msgdata, NULL, NULL));
    bridge_inst_ptr inst = TO_BRIDGE_INST(o2n_message_source->application);
    if (inst->tag == BRIDGE_SYNCED) {
        o2_drop_msg_data("o2lite/cs/cs is from synced process", msgdata);
        return;
    }        
    if (o2_clock_is_synchronized) { // update all connected services
        inst->tag = BRIDGE_SYNCED; // tag the connection bridge_info
        o2_do_not_reenter++;
        enumerate enumerator; // update tags on all the service providers
        o2_enumerate_begin(&enumerator, &o2_ctx->path_tree.children);
        o2_node_ptr entry;
        while ((entry = o2_enumerate_next(&enumerator))) {
            services_entry_ptr services = TO_SERVICES_ENTRY(entry);
            for (int i = 0; i < services->services.length; i++) {
                service_provider_ptr spp =
                        GET_SERVICE_PROVIDER(services->services, i);
                bridge_inst_ptr bi = (bridge_inst_ptr) (spp->service);
                if (bi->tag == BRIDGE_NOCLOCK && bi->info == inst->info) {
                    bi->tag = BRIDGE_SYNCED;
                    if (i == 0) { // active service change
                        o2lite_clock_status_change(services, spp);
                    }
                }
            }
        }
    }
}


// callback function. Parameter is ignored.
o2_err_t o2lite_finish(bridge_inst_ptr bi)
{
    // first free all o2lite_inst using o2lite_bridges to find them
    for (int i = 0; i < o2lite_bridges.length; i++) {
        // bi is already declared and unused as a parameter, so it is
        // ok to use it here (hopefully the compiler will not complain
        // about parameter being a dead variable)
        bi = O2LITE_BRIDGE(i);
        if (!bi) {
            continue;  // connections may be closed already
        }
        o2lite_inst_ptr o2lite = (o2lite_inst_ptr) (bi->info);
        o2n_info_ptr info = o2lite->net_info;
        // services using this bridge have already been removed.
        // avoid searching services again by preventing a call to
        // o2lite_inst_finish() when the socket is closed. (This would
        // be bad for another reason: the whole protocol will be gone,
        // so bi->proto will be a dangling pointer.)
        O2_FREE(o2lite);
        DA_SET(o2lite_bridges, bridge_inst_ptr, i, NULL); // just to be safe
        info->application = NULL;
        O2_FREE(bi);
        o2n_close_socket(info);
    }
    o2lite_bridge = NULL;
    DA_FINISH(o2lite_bridges);
    return O2_SUCCESS;
}


o2_err_t o2lite_initialize()
{
    if (o2lite_bridge) return O2_ALREADY_RUNNING; // already initialized
    DA_INIT(o2lite_bridges, bridge_inst_ptr, 2);
    o2lite_bridge = o2_bridge_new("o2lite", NULL, &o2lite_send, &o2lite_recv,
                                  &o2lite_inst_finish, &o2lite_finish);
    o2_method_new_internal("/_o2/o2lite/dy", "ssi", &o2lite_dy_handler,
                           NULL, false, true);
    o2_method_new_internal("/_o2/o2lite/con", "si", &o2lite_con_handler,
                           NULL, false, true);
    o2_method_new_internal("/_o2/o2lite/sv", "isiis", &o2lite_sv_handler,
                           NULL, false, true);
    o2_method_new_internal("/_o2/o2lite/cs/get", "iis", &o2lite_csget_handler,
                           NULL, false, true);
    o2_method_new_internal("/_o2/o2lite/cs/cs", "", &o2lite_cscs_handler,
                           NULL, false, true);
    return O2_SUCCESS;
}

#endif
