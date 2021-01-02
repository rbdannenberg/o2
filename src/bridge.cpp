// bridge.cpp -- support extensions to non-IP transports
//
// Roger B. Dannenberg
// April 2018

#ifndef O2_NO_BRIDGES

#include "o2.h"
#include "o2internal.h"
#include "services.h"
#include "message.h"
#include "msgsend.h"
#include "pathtree.h"
#include "discovery.h"
#include "clock.h"

static bool bridges_initialized;
int o2_bridge_next_id = 1;

static Vec<Bridge_protocol *> bridges;

#ifndef O2_NO_DEBUG
// subclasses should call this from their show() methods and print any
// additional data and a newline.
void Bridge_info::show(int indent)
{
    O2node::show(indent);
    printf(" bridge protocol %s id %d", proto->protocol, id);
}
#endif


void o2_bridges_initialize(void)
{
    bridges_initialized = true;
    o2_bridge_next_id = 1;
}


Bridge_protocol::Bridge_protocol(const char *name) {
    // O2 must be initialized!  No checks here since we cannot
    // return an O2err
    o2_bridges_initialize();  // just in case it's not yet
    strncpy(protocol, name, 8);
    protocol[7] = 0; // make sure string is terminated
    bridges.push_back(this);
}


Bridge_protocol::~Bridge_protocol() {
    remove_services(NULL);  // remove all Bridge_info for this protocol
    int i = o2_bridge_find_protocol(protocol, NULL);
    if (i >= 0) {
        bridges.remove(i);
    }
}


void o2_bridges_finish(void)
{
    while (bridges.size() > 0) {
        delete bridges.pop_back();
    }
}


// search for matching protocol among bridges
// return the index of the bridge if found, otherwise -1
//
int o2_bridge_find_protocol(const char *name, Bridge_protocol **protocol)
{
    for (int i = 0; i < bridges.size(); i++) {
        if (streql(bridges[i]->protocol, name)) {
            if (protocol) *protocol = bridges[i];
            return i;
        }
    }
    return -1;
}


// remove all services that delegate to this bridge. If info is not
// null, remove only service providers that matches info (typically
// there is a different info for each bridged process, so this is used
// to remove only the services that delegate via the protocol to that
// specific process.
O2err Bridge_protocol::remove_services(Bridge_info *bi)
{
    O2err result = O2_SUCCESS;
    // since this might cause us to rehash services, first make a list
    Vec<Services_entry *> services_list;
    Services_entry::list_services(services_list);
    for (int j = 0; j < services_list.size(); j++) {
        Services_entry *services = services_list[j];
        for (int k = 0; k < services->services.size(); k++) {
            Service_provider *spp = &services->services[k];
            O2node *service = spp->service;
            if (service && ISA_BRIDGE(service) &&
                (!bi || bi == (Bridge_info *) service) &&
                ((Bridge_info *) service)->proto == this) {
                if (services->proc_service_remove(services->key, o2_ctx->proc,
                                                  services, k)) {
                    result = O2_FAIL; // this should never happen
                }
                delete service; // free the bridge_info
                break; // can only be one service offered by proc, and maybe
                // even services was removed, so we should move on to the
                // next service in services list
            }
        }
    }
    return result;
}


int o2_poll_bridges(void)
{
    if (!bridges_initialized) return O2_FAIL;
    for (int i = 0; i < bridges.size(); i++) {
        Bridge_protocol *proto = bridges[i];
        proto->bridge_poll();
    }
    return O2_SUCCESS;
}


// given a Bridge instance ID, find the location of the instance in
// the protocol's instances array. Return -1 if not found.
//
int Bridge_protocol::find_loc(int id)
{
    for (int i = 0; i < instances.size(); i++) {
        if (instances[i]->id == id) {
            return i;
        }
    }
    return -1;
}



/************* IMPLEMENTATION OF O2-SIDE O2LITE PROTOCOL **************/

/*
O2lite is a TCP server on the O2 host side. Each connection request
creates a new O2lite_info to represent the connection to that client.
There is also an O2lite_info to represent the server port, and it has
tag BRIDGE_SERVER.
*/

class O2lite_protocol : public Bridge_protocol {
public:
    O2lite_protocol() : Bridge_protocol("O2lite") { }
    virtual ~O2lite_protocol();
    /// bridge_poll() - o2lite needs no polling function since it
    ///     shares the o2n_ API
};


Bridge_protocol *o2lite_protocol = NULL; // indicates we are active

#define ISA_O2LITE(node) (ISA_BRIDGE(node) && \
                          ((Bridge_info *) node)->proto == o2lite_protocol)

#ifdef O2_NO_DEBUG
#define TO_O2LITE_INFO(node) ((O2lite_info *) (node))
#else
#define TO_O2LITE_INFO(node) (assert(ISA_O2LITE(node)),\
                              ((O2lite_info *) (node)))
#endif


class O2lite_info : public Bridge_info {
public:
    Net_address udp_address; // where to send o2lite udp msg
    
    O2lite_info(const char *ip, int udp) : Bridge_info(o2lite_protocol) {
        udp_address.init_hex(ip, udp, false);
    }

    virtual ~O2lite_info() {
        if (!this) return;
        // remove all sockets serviced by this connection
        proto->remove_services(this);
        if (fds_info) {
            fds_info->owner = NULL; // prevent recursion, stale pointer
            fds_info->close_socket();
        }
    }


    // O2lite is always "synchronized" with the Host because it uses the
    // host's scheduler. Also, since 3rd party processes do not distinguish
    // between O2lite services and Host services at this IP address, they
    // see the service status according to the Host status. Once the Host
    // is synchronized with the 3rd party, the 3rd party expects that
    // timestamps will work. Thus, we always report that the O2lite
    // process is synchronized.
    bool local_is_synchronized() { return true; }

    // O2lite does scheduling on the Host side. This could change in the
    // future. Note that shared memory protocol allows timestamps, but
    // requires timestamps to be non-decreasing so that scheduling is
    // trivial.
    bool schedule_before_send() { return true; }

    virtual O2err send(bool block) {
        O2err rslt;
        int tcp_flag;
        O2message_ptr msg = pre_send(&tcp_flag);
        if (!msg) return O2_NO_SERVICE;
        if (tcp_flag) {
            rslt = fds_info->send_tcp(block, (o2n_message_ptr) msg);
        } else {  // send via UDP
            rslt = o2n_send_udp(&udp_address, (o2n_message_ptr) msg);
            if (rslt != O2_SUCCESS) {
                O2_DBn(printf("Bridge_info::send error, port %d\n",
                              udp_address.get_port()));
            }
        }
        return rslt;
    }

#ifndef O2_NO_DEBUG
    virtual void show(int indent) {
        Bridge_info::show(indent);
        printf("\n");
    }
#endif
    // virtual O2status status(const char **process);  -- see Bridge_info

    // Net_interface:
    O2err accepted(Fds_info *conn) {
        printf("ERROR: O2lite_info is not a server\n");
        conn->close_socket();
        return O2_FAIL;
    }
    O2err connected() { return O2_FAIL; } // we are not a TCP client
};


O2lite_protocol::~O2lite_protocol()
{
    o2_method_free("/_o2/o2lite");
    // also free all O2lite connections
    for (int i = 0; i < o2n_fds_info.size(); i++) {
        O2lite_info *o2lite = (O2lite_info *) o2n_fds_info[i]->owner;
        // o2lite could be anything: Proc_info, Osc_info, Bridge_info, etc.
        if (o2lite && ISA_O2LITE(o2lite)) {  // only remove an O2lite_info
            delete o2lite;
        }
    }
}


// o2lite_dy_handler - generic bridge discovery handler for !_o2/o2lite/dy
//   message parameters are ensemble, port
void o2lite_dy_handler(o2_msg_data_ptr msgdata, const char *types,
                       O2arg_ptr *argv, int argc, const void *user_data)
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
    Net_address address;
    O2err err = address.init_hex(ip, port, false);
    O2_DBd(if (err) printf("%s o2lite_dy_handler: ip %s, udp %d, err %s\n",
              o2_debug_prefix, ip, port, o2_error_to_string(err)));
    o2_send_start();
    O2message_ptr msg = o2_make_dy_msg(o2_ctx->proc, false, true, O2_DY_INFO);
    o2n_send_udp(&address, (o2n_message_ptr) msg); // send and free the message
}


// o2lite_con_handler - handler for !_o2/o2lite/con; called immediately
//   after o2lite bridged process makes a TCP connection, message
//   parameters are ip address and port number for bridged process
//
void o2lite_con_handler(o2_msg_data_ptr msgdata, const char *types,
                        O2arg_ptr *argv, int argc, const void *user_data)
{
    O2_DBd(o2_dbg_msg("o2lite_con_handler gets", NULL, msgdata, NULL, NULL));
    // get the arguments: ensemble name, protocol name,
    // if this is "O2lite" protocol, get ip as string,
    //  tcp port, discovery port.
    // assumes o2lite is initialized, but it must be
    // because the handler is installed
    const char *ip = argv[0]->s;
    int port = argv[1]->i32;
    // make sure o2_message_source is an O2TAG_TCP_PROC. If o2lite mistakenly
    // sends via UDP, we'll get the message, but o2_message_source will be
    // our local O2TAG_PROC_TCP_SERVER
    if (!(o2_message_source->tag & O2TAG_PROC)) {
        o2_drop_msg_data("/_o2/o2lite/con not received from O2TAG_TCP_PROC",
                         msgdata);
        return;
    }
    // replace o2_message_source with a bridge_info:
    O2lite_info *info = new O2lite_info(ip, port);
    info->fds_info = o2_message_source->fds_info;
    o2_message_source->fds_info = NULL;
    info->fds_info->owner = info;
    // now we've moved ownership of the socket to an O2lite_info;
    // The o2_info that received this message is a Proc_info that
    // was created when we accepted the TCP port. Free it now.
    delete o2_message_source;
    // reply with id
    o2_send_start();
    o2_add_int32(info->id);
    O2message_ptr msg = o2_message_finish(0.0, "!_o2/id", true);
    O2_DBd(o2_dbg_msg("o2lite_con_handler sending", msg, &msg->data,
                      NULL, NULL));
    o2_prepare_to_deliver(msg);
    O2err err = info->send(false); // byte swapping is done here
    if (err) {
        char errmsg[80];
        snprintf(errmsg, 80, "o2lite_con_handler sending id %s",
                 o2_error_to_string(err));
        o2_drop_msg_data(errmsg, msgdata);
    }
}
       

// Handler for !_o2/o2lite/sv message. This is to create/modify a
// service/tapper for o2lite client. Parameters are: service-name,
// exists-flag, service-flag, and tapper-or-properties string, send_mode
// (for taps).
//
void o2lite_sv_handler(o2_msg_data_ptr msgdata, const char *types,
                        O2arg_ptr *argv, int argc, const void *user_data)
{
    O2_DBd(o2_dbg_msg("o2lite_sv_handler gets", NULL, msgdata, NULL, NULL));
    // get the arguments: ensemble name, protocol name,
    // if this is "O2lite" protocol, get ip as string,
    //  tcp port, discovery port.
    // assumes o2lite is initialized, but it must be
    // because the handler is installed
    // int id = argv[0]->i32; // ignore id since we have o2_message_source
    const char *serv = argv[0]->s;
    bool add = argv[1]->i;
    bool is_service = argv[2]->i;
    const char *prtp = argv[3]->s;
    O2tap_send_mode send_mode = (O2tap_send_mode) (argv[4]->i);
    O2lite_info *o2lite = (O2lite_info *) o2_message_source;
    // make sure o2lite is really an O2lite_info: check tag and proto:
    if (!ISA_BRIDGE(o2lite) ||
        TO_BRIDGE_INFO(o2lite)->proto != o2lite_protocol) {
        return;  // some non-O2lite sender invoked /_o2/o2lite/sv!
    }
    O2err err = O2_SUCCESS;
    if (add) { // add a new service or tap
        if (is_service) {
             err = Services_entry::service_provider_new(serv, prtp, o2lite,
                                                        o2_ctx->proc);
       } else { // add tap
            err = o2_tap_new(serv, o2_ctx->proc, prtp, send_mode);
        }
    } else {
        if (is_service) { // remove a service
            err = Services_entry::proc_service_remove(serv, o2_ctx->proc,
                                                      NULL, -1);
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
                          O2arg_ptr *argv, int argc, const void *user_data)
{
    O2_DBk(o2_dbg_msg("o2lite_csget_handler gets", NULL, msgdata, NULL, NULL));
    // assumes o2lite is initialized, but it must be
    // because the handler is installed
    int id = argv[0]->i32;
    int seqno = argv[1]->i32;
    const char *replyto = argv[2]->s;
    // since this comes by UDP to the local Proc_info, we don't know who
    // the sender is. But sender includes their id, so we look them up.
    // (Maybe it would be better to just send their UDP port or have
    // network.cpp stash the UDP reply port, but maybe this is a little
    // more secure -- you can't send a bogus message to cause this
    // process to send a random message to a random UDP port!)
    o2_message_source = o2lite_protocol->find(id);
    if (!o2_message_source || !ISA_O2LITE(o2_message_source)) {
        o2_drop_message("bad ID in o2lite/cs/get message", msgdata);
        return;  // some non-O2lite sender invoked /_o2/o2lite/get!
    }
    if (!o2_clock_is_synchronized) {  // can't reply with time
        o2_drop_msg_data("no global time yet for o2lite/cs/get message",
                         msgdata);
        return;
    }
    o2_send_start();
    o2_add_int32(seqno);
    // should we get the time again? It would be a little more accurate
    // than o2_global_now, but it's more computation, so no.
    o2_add_time(o2_global_now);
    O2message_ptr msg = o2_message_finish(0, replyto, false);
    O2_DBk(o2_dbg_msg("o2lite_csget_handler sends", msg, &msg->data,
                      NULL, NULL));
#if IS_LITTLE_ENDIAN
    o2_msg_swap_endian(&msg->data, true);
#endif
    o2n_send_udp(&((O2lite_info *) o2_message_source)->udp_address,
                 (o2n_message_ptr) msg);
}


// Handler for !_o2/o2lite/cs/cs message. This is to announce the
// o2lite client has clock sync. Parameters are: id
//
void o2lite_cscs_handler(o2_msg_data_ptr msgdata, const char *types,
                         O2arg_ptr *argv, int argc, const void *user_data)
{
    O2_DBd(o2_dbg_msg("o2lite_cscs_handler gets", NULL, msgdata, NULL, NULL));
    // make sure o2lite is really an O2lite_info: check tag and proto:
    if (!ISA_BRIDGE(o2_message_source) ||
        TO_BRIDGE_INFO(o2_message_source)->proto != o2lite_protocol) {
        return;  // some non-O2lite sender invoked /_o2/o2lite/cs/cs!
    }
    if (IS_SYNCED(o2_message_source)) {
        o2_drop_msg_data("o2lite/cs/cs is from synced process", msgdata);
        return;
    }
    o2_message_source->tag |= O2TAG_SYNCED;
    o2_clock_status_change(o2_message_source);
}


O2err o2lite_initialize()
{
    if (o2lite_protocol) return O2_ALREADY_RUNNING; // already initialized
    o2lite_protocol = new O2lite_protocol();
    o2_method_new_internal("/_o2/o2lite/dy", "ssii",
                           &o2lite_dy_handler, NULL, false, true);
    o2_method_new_internal("/_o2/o2lite/con", "si",
                           &o2lite_con_handler, NULL, false, true);
    o2_method_new_internal("/_o2/o2lite/sv", "siisi",
                           &o2lite_sv_handler, NULL, false, true);
    o2_method_new_internal("/_o2/o2lite/cs/get", "iis",
                           &o2lite_csget_handler, NULL, false, true);
    // in principle, we should test return values on all of the above,
    // but in practice, they will all succeed unless we are in deep trouble.
    return o2_method_new_internal("/_o2/o2lite/cs/cs", "",
                                  &o2lite_cscs_handler, NULL, false, true);
}

#endif


