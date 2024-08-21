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
#include "properties.h"

static bool bridges_initialized;
O2_EXPORT int o2_bridge_next_id = 1;

static Vec<Bridge_protocol *> bridges;

#ifndef O2_NO_DEBUG
// subclasses should call this from their show() methods and print any
// additional data and a newline.
void Bridge_info::show(int indent)
{
    O2node::show(indent);
    printf(" bridge protocol %s id %d\n", proto->protocol, id);
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
    O2_DBb(dbprintf("deleting Bridge_protocol@%p name %s size %d\n",
                    this, protocol, instances.size()));
    remove_services(NULL);  // remove all Bridge_info for this protocol
    // this is a little tricky: deleting an instance, which is a Bridge_info,
    // removes the Bridge_info from instances. There's a search involved, but
    // since we remove the first element at each iteration, the search always
    // finds the object in the 0th location and removes it by copying the
    // last element of instances to index 0. The size of instances shrinks
    // by 1 each iteration until there is nothing left.
    while (instances.size() > 0) {
        O2_DBb(dbprintf("deleting %s Bridge instance@%p\n",
                        protocol, instances[0]));
        instances[0]->o2_delete();
    }
    int i = o2_bridge_find_protocol(protocol, NULL);
    if (i >= 0) {
        O2_DBb(dbprintf("removing Bridge_protocol@%p name %s index %d "
                        "size %d from array of protocols\n",
                        this, protocol, i, instances.size()));
        bridges.remove(i);
    }
}


void o2_bridges_finish()
{
    while (bridges.size() > 0) {
        delete bridges.pop_back();
    }
    bridges.finish();
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


// remove all services that delegate to this bridge. If bi is not
// null, remove only service providers that matches bi (typically
// there is a different info for each bridged process, so this is used
// to remove only the services that delegate via the protocol to that
// specific process.
O2err Bridge_protocol::remove_services(Bridge_info *bi)
{
    O2_DBb(dbprintf("remove_services delegating to bridge protocol@%p name"
                    " %s instance %p%s\n", this, protocol, bi,
                    bi ? "" : " (all)"));
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
                O2_DBb(dbprintf("remove_services removing %s delegating to "
                                "bridge instance %p protocol %s\n",
                                services->key, bi, protocol));
                if (services->proc_service_remove(services->key, o2_ctx->proc,
                                                  services, k)) {
                    result = O2_FAIL; // this should never happen
                }
                // bridge_info is owned by protocol
                break; // can only be one service offered by proc, and maybe
                // even services was removed, so we should move on to the
                // next service in services list
            }
        }
    }
    return result;
}


int o2_poll_bridges()
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


// This is a generalized handler for !_o2/o2lite/cs/get and !_o2/ws/cs/get
// Both direct to specific message handlers that find the sender in code
// that is bridge-protocol-specific. Then, they direct here to finish the work.
void o2_bridge_csget_handler(O2msg_data_ptr msgdata, int seqno,
                             const char *replyto)
{
    if (!o2_message_source || !ISA_BRIDGE(o2_message_source)) {
        o2_drop_message("bad ID in o2lite/cs/get message", msgdata);
        return;  // some non-O2lite sender invoked /_o2/o2lite/get!
    }
    if (!o2_clock_is_synchronized) {  // can't reply with time
        o2_drop_msg_data("no global time yet for /_o2/*/cs/get message",
                         msgdata);
        return;
    }
    o2_send_start();
    o2_add_int32(seqno);
    // should we get the time again? It would be a little more accurate
    // than o2_global_now, but it's more computation, so no.
    o2_add_time(o2_global_now);
    O2message_ptr msg = o2_message_finish(0, replyto, false);
    O2_DBk(o2_dbg_msg("o2_bridge_csget_handler sends", msg, &msg->data,
                      NULL, NULL));
    o2_prepare_to_deliver(msg);
    o2_message_source->send(false);
}

void o2_bridge_cscs_handler(O2msg_data_ptr msgdata, const char *types,
                            O2arg_ptr *argv, int argc, const void *user_data)
{
    O2_DBd(printf("o2ws_bridge_cscs_handler, source is:\n");
           (o2_message_source ? o2_message_source->show(4)
                              : (void) printf("    NULL\n")));
    // make sure source is really an Http_conn: check tag and proto:
    if (!o2_message_source || !ISA_BRIDGE(o2_message_source)) {
        return;  // some non-websocket sender invoked /_o2/ws/cs/cs!
    }
    if (IS_SYNCED(o2_message_source)) {
        o2_drop_msg_data("/_o2/*/cs/cs is from synced process", msgdata);
        return;
    }
    o2_message_source->tag |= O2TAG_SYNCED;
    o2_clock_status_change(o2_message_source);
}


// handler for /_o2/*/sv messages which register services for o2lite
// protocols (including websocket interface)
//
void o2_bridge_sv_handler(O2msg_data_ptr msgdata, const char *types,
                          O2arg_ptr *argv, int argc, const void *user_data)
{
    O2err rslt = O2_SUCCESS;

    O2_DBw(o2_dbg_msg("o2_bridge_sv_handler gets", NULL, msgdata, NULL, NULL));
    // get the arguments: bridge id, service name, 
    //     add-or-remove flag, is-service-or-tap flag, property string
    // assumes o2ws is initialized, but it must be
    // because the handler is installed
    const char *serv = argv[0]->s;
    bool add = argv[1]->i;
    bool is_service = argv[2]->i;
    const char *prtp = argv[3]->s;
    O2tap_send_mode send_mode = (O2tap_send_mode) (argv[4]->i32);
    if (!ISA_BRIDGE(o2_message_source)) {
        o2_drop_msg_data("source of /_o2/*/sv is not a bridge", msgdata);
    } else if (is_service) {
        Service_provider *spp = Services_entry::find_local_entry(serv);
        if (spp) {
            if (spp->service != o2_message_source) {  // cannot replace
                o2_drop_msg_data("/_o2/*/sv not from service provider",
                                 msgdata);
            } else {
                if (add) { // service already exists, set properties
                    if (prtp) {
                        if (prtp[0] == 0 || (prtp[0] == ';' && prtp[1] == 0)) {
                            prtp = NULL;  // change "" or ";" to NULL
                        } else {
                            prtp = o2_heapify(prtp);
                        }
                    }
                    rslt = o2_set_service_properties(spp, serv, (char *) prtp);
                } else {
                    rslt = Services_entry::proc_service_remove(serv,
                                             o2_ctx->proc, NULL, -1);
                }
            }
        } else {
            if (add) {
                rslt = Services_entry::service_provider_new(serv, prtp,
                                       o2_message_source, o2_ctx->proc);
                if (rslt == O2_SUCCESS) {
                    o2_notify_others(serv, true, NULL, prtp, 0);
                }
            }  // else remove service, but there is no service; we're done
        }
    } else {
        if (add) {
            rslt = o2_tap_new(serv, o2_ctx->proc, prtp, send_mode);
        } else {
            rslt = o2_tap_remove(serv, o2_ctx->proc, prtp);
        }
    }
    if (rslt) {
        char errmsg[100];
        snprintf(errmsg, 100, "/_o2/*/sv handler got %s for service %s",
                 o2_error_to_string(rslt), serv);
        o2_drop_msg_data(errmsg, msgdata);
    }
    return;
}


// handler for /_o2/*/st messages which return status of a service
// for o2lite protocols (including websocket interface)
//
void o2_bridge_st_handler(O2msg_data_ptr msgdata, const char *types,
                          O2arg_ptr *argv, int argc, const void *user_data)
{
    const char *service = argv[0]->s;
    int status = o2_status(service);
    o2_send_start();
    o2_add_string(service);
    o2_add_int32(status);
    O2message_ptr msg = o2_message_finish(0, "!_o2/st", true);
    o2_prepare_to_deliver(msg);
    o2_message_source->send(false);
}


// handler for /_o2/*/ls messages which return information on all 
// services to for o2lite clients (including websocket interfaces)
// Sends messages with: service_name, service_type, process_name, properties
// properties is sent WITHOUT a leading ";"
void o2_bridge_ls_handler(O2msg_data_ptr msgdata, const char *types,
                          O2arg_ptr *argv, int argc, const void *user_data)
{
    o2_services_list();
    for (int i = 0; true; i++) {
        // prepare for the end of services when we send ("", 0, "", ""):
        const char *name = o2_service_name(i);
        int service_type = 0;
        const char *process_name = "";
        const char *properties = "";
        if (name) {  // replace all the parameters with "real" data
            service_type = o2_service_type(i);
            process_name = o2_service_process(i);
            properties = (service_type == O2_TAP ?
                          o2_service_tapper(i) : o2_service_properties(i));
        }
        o2_send_start();
        o2_add_string(name ? name : "");
        o2_add_int32(service_type);
        o2_add_string(process_name);
        o2_add_string(properties);
        O2message_ptr msg = o2_message_finish(0, "!_o2/ls", true);
        o2_prepare_to_deliver(msg);
        o2_message_source->send(false);
        if (!name) break;  // no more messages to send, exit loop
    }
    o2_services_list_free();
}

O2err Bridge_info::send_to_taps(O2message_ptr msg)
{
    Services_entry *ss;
    if (!o2_msg_service(&msg->data, &ss)) {
        return O2_NO_SERVICE;
    }
    o2_send_to_taps(msg, ss);
    return O2_SUCCESS;
}



/************* IMPLEMENTATION OF O2-SIDE O2LITE PROTOCOL **************/

/*
O2lite is a TCP server on the O2 host side. Each connection request
creates a new O2lite_info to represent the connection to that client.
There is also an O2lite_info to represent the server port, and it has
tag BRIDGE_SERVER.
*/

Bridge_protocol *o2lite_protocol = NULL; // indicates we are active

class O2lite_protocol : public Bridge_protocol {
public:
    O2lite_protocol() : Bridge_protocol("O2lite") { }
    virtual ~O2lite_protocol();
    /// bridge_poll() - o2lite needs no polling function since it
    ///     shares the o2n_ API
};


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
        O2_DBb(dbprintf("deleting O2lite_info@%p\n", this));
        if (!this) return;
        // remove all sockets serviced by this connection
        proto->remove_services(this);
        delete_fds_info();
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
        bool tcp_flag;
        // send to taps before byte swap. taps are handled here on host side.
        O2err taperr = send_to_taps(o2_current_message());
        // send taps first because we will lose ownership of msg to network
        O2message_ptr msg = pre_send(&tcp_flag);
        if (!msg) return O2_NO_SERVICE;
        if (tcp_flag) {
            rslt = fds_info->send_tcp(block, (O2netmsg_ptr) msg);
        } else {  // send via UDP
            rslt = o2n_send_udp(&udp_address, (O2netmsg_ptr) msg);
            if (rslt != O2_SUCCESS) {
                O2_DBn(printf("Bridge_info::send error, port %d\n",
                              udp_address.get_port()));
            }
        }
        // report the first error we saw
        return (O2err) ((int) taperr || (int) rslt);
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
        conn->close_socket(true);
        return O2_FAIL;
    }
    O2err connected() { return O2_FAIL; } // we are not a TCP client
};


O2lite_protocol::~O2lite_protocol()
{
    O2_DBb(dbprintf("deleting O2lite_protocol@%p\n", this));
    o2_method_free("/_o2/o2lite");
    // also free all O2lite connections
    for (int i = 0; i < o2n_fds_info.size(); i++) {
        O2lite_info *o2lite = (O2lite_info *) o2n_fds_info[i]->owner;
        // o2lite could be anything: Proc_info, Osc_info, Bridge_info, etc.
        if (o2lite && ISA_O2LITE(o2lite)) {  // only remove an O2lite_info
            o2lite->o2_delete();
        }
    }
    o2lite_protocol = NULL;
}


// o2lite_dy_handler - generic bridge discovery handler for !_o2/o2lite/dy
//   message parameters are ensemble, tcp port, udp_port
void o2lite_dy_handler(O2msg_data_ptr msgdata, const char *types,
                       O2arg_ptr *argv, int argc, const void *user_data)
{
    O2_DBd(o2_dbg_msg("o2lite_dy_handler gets", NULL, msgdata, NULL, NULL));
    // get the arguments: ensemble name, ip as string,
    //  tcp port, discovery port, DY_INFO
    
    
    if (!streql(argv[0]->s, o2_ensemble_name)) {
        O2_DBd(printf("    Ignored: ensemble name %s is not %s\n", 
                      argv[0]->s, o2_ensemble_name));
        return;
    }
    // assumes o2lite is initialized, but it must be
    // because the handler is installed
    const char *ip = argv[1]->s;
    // unused: int tcp_port = argv[2]->i32;
    int udp_port = argv[3]->i32;
    
    // send !_o2/dy back to bridged process:
    Net_address address;
    O2err err = address.init_hex(ip, udp_port, false);
    O2_DBd(if (err) dbprintf("o2lite_dy_handler: ip %s, udp %d, err %s\n",
                             ip, udp_port, o2_error_to_string(err)));
    o2_send_start();
    O2message_ptr msg = o2_make_dy_msg(o2_ctx->proc, false, true, O2_DY_INFO);
    o2n_send_udp(&address, (O2netmsg_ptr) msg); // send and free the message
}


// o2lite_con_handler - handler for !_o2/o2lite/con; called immediately
//   after o2lite bridged process makes a TCP connection, message
//   parameters are ip address (in hex) and udp port number for bridged process
//
void o2lite_con_handler(O2msg_data_ptr msgdata, const char *types,
                        O2arg_ptr *argv, int argc, const void *user_data)
{
    O2_DBd(o2_dbg_msg("o2lite_con_handler gets", NULL, msgdata, NULL, NULL));
    // get the arguments: ensemble name, protocol name,
    // if this is "O2lite" protocol, get ip as string and int udp port.
    // assume o2lite is initialized; it must be because the handler is installed
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
    o2_message_source->o2_delete();  // avoid recursive delete
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
       

// Handler for !_o2/o2lite/cs/get message. This is to get the time for
// an o2lite client. Parameters are: id, sequence-number, reply-to
//
void o2lite_csget_handler(O2msg_data_ptr msgdata, const char *types,
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
    // use common code to reply to cs/get:
    o2_bridge_csget_handler(msgdata, seqno, replyto);
}


O2err o2lite_initialize()
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    if (o2lite_protocol) return O2_ALREADY_RUNNING; // already initialized
    o2lite_protocol = new O2lite_protocol();
    o2_method_new_internal("/_o2/o2lite/dy", "ssiii",
                           &o2lite_dy_handler, NULL, false, true);
    o2_method_new_internal("/_o2/o2lite/con", "si",
                           &o2lite_con_handler, NULL, false, true);
    o2_method_new_internal("/_o2/o2lite/sv", "siisi",
                           &o2_bridge_sv_handler, NULL, false, true);
    o2_method_new_internal("/_o2/o2lite/cs/get", "iis",
                           &o2lite_csget_handler, NULL, false, true);
    o2_method_new_internal("/_o2/o2lite/st", "s",
                           &o2_bridge_st_handler, NULL, false, true);
    o2_method_new_internal("/_o2/o2lite/ls", "",
                           &o2_bridge_ls_handler, NULL, false, true);
    // in principle, we should test return values on all of the above,
    // but in practice, they will all succeed unless we are in deep trouble.
    return o2_method_new_internal("/_o2/o2lite/cs/cs", "",
                                  &o2_bridge_cscs_handler, NULL, false, true);
}

#endif
