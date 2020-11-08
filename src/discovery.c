/* discovery.c -- discovery protocol
 *
 * Roger B. Dannenberg
 * April 2020
 */

#include "ctype.h"
#include "o2internal.h"
#include "services.h"
#include "message.h"
#include "msgsend.h"
#include "clock.h"
#include "discovery.h"
#include "o2sched.h"
#include "bridge.h"
#include "pathtree.h"

// These parameters hit all 16 ports in 3.88s, then all 16 again by 30s,
// Then send every 4s to ports up to our index. So if we are able to open
// the first discovery port, we send a discovery message every 4s to that
// port only. If we open the 16th discovery port, we send to each one 
// every 64s.
#define INITIAL_DISCOVERY_PERIOD 0.1
#define DEFAULT_DISCOVERY_PERIOD 4.0
#define RATE_DECAY 1.125

// o2_discover:
//   initially send a discovery message at short intervals, but increase the
//     interval each time until a maximum interval of 4s is
//     reached. This gives a message every 40ms on average when there
//     are 100 processes. Also gives 2 tries on all ports initially, then
//     only sends to ports less than or equal to our discovery index.
//   next_disc_index is the port we will send discover message to
static int disc_msg_count = 0;
static double disc_period = INITIAL_DISCOVERY_PERIOD;
static int next_disc_index = 0; // index to o2_port_map, port to send to
static int my_port = -1; // port we grabbed, for UDP and TCP
static o2_time max_disc_period = DEFAULT_DISCOVERY_PERIOD;
static int disc_port_index = -1;

// From Wikipedia: The range 49152–65535 (215+214 to 216−1) contains
//   dynamic or private ports that cannot be registered with IANA.[198]
//   This range is used for private, or customized services or temporary
//   purposes and for automatic allocation of ephemeral ports.
// These ports were randomly generated from that range.
int o2_port_map[PORT_MAX] = { 64541, 60238, 57143, 55764, 56975, 62711,
                              57571, 53472, 51779, 63714, 53304, 61696,
                              50665, 49404, 64828, 54859 };
// picked from o2_port_map when discovery is initialized.
o2n_info_ptr o2_discovery_server = NULL;

static void hub_has_new_client(proc_info_ptr nc);


o2_time o2_set_discovery_period(o2_time period)
{
    o2_time old = max_disc_period;
    if (period < 0.1) period = 0.1;
    disc_period = period;
    max_disc_period = period;
    return old;
}


static int extract_ip_port(const char *name, char *ip, int *port)
{
    strcpy(ip, name);
    char *colon = strchr(ip, ':');
    if (!colon) {
        return O2_FAIL;
    }
    *colon = 0; // isolate the ipaddress from ip:port
    *port = atoi(colon + 1);
    return O2_SUCCESS;
}


// initialize this module: creates a UDP receive port and starts discovery
//
o2_err_t o2_discovery_initialize()
{
    o2_hub_addr[0] = 0;
    disc_period = INITIAL_DISCOVERY_PERIOD;
    next_disc_index = -1; // gets incremented before first use
    // Create socket to receive UDP (discovery and other)
    // Try to find an available port number from the discover port map.
    // If there are no available port number, print the error & return O2_FAIL.
    for (disc_port_index = 0; disc_port_index < PORT_MAX; disc_port_index++) {
        my_port = o2_port_map[disc_port_index];
        o2_discovery_server = o2n_udp_server_new(&my_port, false, NULL);
        if (o2_discovery_server) {
            break;
        }
    }
    if (disc_port_index >= PORT_MAX) {
        my_port = -1; // no port to receive discovery messages
        disc_port_index = -1;
        fprintf(stderr, "Unable to allocate a discovery port.\n");
        return O2_NO_PORT;
    } else {   // use the discovery message receive port as
               // the general UDP receive and TCP server port
        
    }
    O2_DBdo(printf("%s **** discovery port %ld (%d already taken).\n",
                   o2_debug_prefix, (long) my_port, disc_port_index));

    // do not run immediately so that user has a chance to call o2_hub() first,
    // which will disable discovery. This is not really time-dependent because
    // no logical time will pass until o2_poll() is called.
    o2_send_discovery_at(o2_local_time() + 0.01);
    return O2_SUCCESS;
}


void o2_discovery_initialize2()
{
    o2_method_new_internal("/_o2/dy", "ssii", &o2_discovery_handler,
                           NULL, false, false);
    o2_method_new_internal("/_o2/hub", "", &o2_hub_handler,
                           NULL, false, false);
    o2_method_new_internal("/_o2/sv", NULL, &o2_services_handler,
                           NULL, false, false);
    o2_method_new_internal("/_o2/ds", NULL, &o2_discovery_send_handler,
                           NULL, false, false);
}


o2_err_t o2_discovery_finish(void)
{
    return O2_SUCCESS;
}


/**
 * Make /_o2/dy message, if this is a udp message, switch to network byte order
 */
o2_message_ptr o2_make_dy_msg(proc_info_ptr proc, int tcp_flag,
                              int dy_flag)
{
    char buffer[O2_MAX_PROCNAME_LEN];
    char *ip;
    int port;
    assert(o2n_found_network);
    if (proc == o2_ctx->proc) {
        ip = o2n_local_ip;
        port = proc->net_info->port;
    } else {
        ip = buffer;
        if (extract_ip_port(proc->name, ip, &port))
            return NULL;
    }

    int err = o2_send_start() || o2_add_string(o2_ensemble_name) ||
        o2_add_string(ip) || o2_add_int32(port) ||
        o2_add_int32(dy_flag);
    if (err) return NULL;
    o2_message_ptr msg = o2_message_finish(0.0, "!_o2/dy", tcp_flag);
    if (!msg) return NULL;
#if IS_LITTLE_ENDIAN
    if (!tcp_flag) {
        o2_msg_swap_endian(&msg->data, true);
    }
#endif
    return msg;
}


/**
 * Broadcast discovery message (!o2/dy) to a discovery port.
 *
 * @param port  the destination port number
 *
 * Receiver will get message and call o2_discovery_handler()
 */
static void o2_broadcast_message(int port)
{
    // set up the address and port
    o2_message_ptr m = o2_make_dy_msg(o2_ctx->proc, false, O2_DY_INFO);
    // m is in network order
    if (!m) {
        return;
    }
    
    // broadcast the message
    if (o2n_found_network) {
        O2_DBd(printf("%s broadcasting discovery msg to port %d\n",
                      o2_debug_prefix, port));
        o2n_send_broadcast(port, (o2n_message_ptr) m);
    }
    // assume that broadcast messages are not received on the local machine
    // so we have to send separately to localhost using the same port;
    // since we own o2_discovery_port, there is no need to send in that case
    // because any broadcast message is a discovery message and we don't
    // need to discover ourselves.
    if (port != my_port) {
        o2n_send_udp_local(port, (o2n_message_ptr) m);
    } else {
        O2_FREE(m);
    }
}


// /_o2/dy handler, parameters are: ensemble name, ip, port, dy_type
//
// If we are the server, send discovery message to client and we are done.
// If we are the client, o2_send_services()
//
void o2_discovery_handler(o2_msg_data_ptr msg, const char *types,
                          o2_arg_ptr *argv, int argc, const void *user_data)
{
    O2_DBd(o2_dbg_msg("o2_discovery_handler gets", NULL, msg, NULL, NULL));
    o2_arg_ptr ens_arg, ip_arg, tcp_arg, dy_arg;
    // get the arguments: ensemble name, ip as string,
    //                    port, discovery port
    o2_extract_start(msg);
    if (!(ens_arg = o2_get_next(O2_STRING)) ||
        !(ip_arg = o2_get_next(O2_STRING)) ||
        !(tcp_arg = o2_get_next(O2_INT32)) ||
        !(dy_arg = o2_get_next(O2_INT32))) {
        return;
    }
    const char *ens = ens_arg->s;
    const char *ip = ip_arg->s;
    int port = tcp_arg->i32;
    int dy = dy_arg->i32;
    
    if (!streql(ens, o2_ensemble_name)) {
        O2_DBd(printf("    Ignored: ensemble name %s is not %s\n", 
                      ens, o2_ensemble_name));
        return;
    }
    o2_discovered_a_remote_process(ip, port, dy);
}


// Called when a remote process is discovered. This can happen in various ways:
// 1. a /dy message is received via broadcast (O2_DY_INFO)
//
// 2. user calls o2_hub() to name another process
//
// 3. /dy message is received via tcp
//    if the message is O2_DY_CALLBACK, we will become the client. Close
//        the connection. Then we can behave like O2_DY_INFO.
//
o2_err_t o2_discovered_a_remote_process(const char *ip, int port, int dy)
{
    o2n_info_ptr remote = o2n_message_source;
    if (dy == O2_DY_CALLBACK) { // similar to info, but close connection first
        o2n_close_socket(remote); // we are going to be the client
        dy = O2_DY_INFO; 
    }

    char name[O2_MAX_PROCNAME_LEN];
    // ip:port + pad with zeros
    snprintf(name, O2_MAX_PROCNAME_LEN, "%s:%d%c%c%c%c",
             ip, port, 0, 0, 0, 0);

    proc_info_ptr proc = NULL;
    o2_message_ptr reply_msg = NULL;
    if (dy == O2_DY_INFO) {
        int compare = strcmp(o2_ctx->proc->name, name);
        if (compare == 0) {
            O2_DBd(printf("%s Ignored: I received my own broadcast message\n",
                          o2_debug_prefix));
            return O2_SUCCESS; // the "discovered process" is this one
        }
        o2_node_ptr *entry_ptr = o2_lookup(&o2_ctx->path_tree, name);
        if (*entry_ptr) { // process is already discovered, ignore message
            O2_DBd(printf("%s ** process already discovered, ignore %s\n",
                          o2_debug_prefix, name));
            return O2_SUCCESS;
        }
        // process is unknown, make a proc_info for it and start connecting...
        proc = o2_create_tcp_proc(PROC_TEMP, ip, port); // proc name is NULL

        if (compare > 0) { // we are the server, the other party should connect
            if (!proc) {
                return O2_FAIL;
            }
            // send /dy by TCP
            o2_prepare_to_deliver(o2_make_dy_msg(o2_ctx->proc, true,
                                                 O2_DY_CALLBACK));
            if (o2_send_remote(proc, false) != O2_SUCCESS) {
                o2_proc_info_free(proc); // error recovery: don't leak memory
            } else {
                // this connection will be closed by receiving client
                O2_DBd(printf("%s ** discovery sending O2_DY_CALLBACK to %s\n",
                              o2_debug_prefix, name));
            }
            return O2_SUCCESS;
        }

        // else we are the client
        proc->tag = PROC_NOCLOCK;
        proc->name = o2_heapify(name);
        int dy_flag = (streql(name, o2_hub_addr) ? O2_DY_HUB : O2_DY_CONNECT);
        o2_service_provider_new(name, NULL, (o2_node_ptr) proc, proc);
        O2_DBG(printf("%s ** discovery sending O2_DY_CONNECT to server %s\n",
                      o2_debug_prefix, name));
        reply_msg = o2_make_dy_msg(o2_ctx->proc, true, dy_flag);
    } else { // dy is not O2_DY_INFO, must be O2_DY_CALLBACK or O2_DY_CONNECT
             //    or O2_DY_REPLY or O2_DY_HUB
        proc = TO_PROC_INFO(remote->application);
        proc->name = o2_heapify(name);
        o2_service_provider_new(proc->name, NULL, (o2_node_ptr) proc, proc);
        if (dy == O2_DY_HUB) { // this is the hub, this is the server side
            printf("######## This is the hub server side #######\n");
            // send a /dy to remote with O2_DY_REPLY
            O2_DBd(printf("%s ** discovery got HUB sending REPLY to hub %s\n",
                          o2_debug_prefix, name));
            reply_msg = o2_make_dy_msg(o2_ctx->proc, true, O2_DY_REPLY);
        } else if (dy == O2_DY_REPLY) { // first message from hub
            if (!streql(name, o2_hub_addr)) { // should be equal
                printf("Warning: expected O2_DY_REPLY to be from hub\n");
                o2n_close_socket(remote);
                return O2_FAIL;
            }
            printf("####### This is the hub client side #######\n");
            proc->uses_hub = O2_HUB_REMOTE;
            o2_send_start();
            reply_msg = o2_message_finish(0.0, "!_o2/hub", true);
            O2_DBd(printf("%s ** discovery got REPLY sending !_o2/hub %s\n",
                          o2_debug_prefix, name));
        } else if (dy == O2_DY_CONNECT) { 
            // similar to info, but close connection
            O2_DBG(printf("%s ** discovery got CONNECT from client %s, %s\n",
                           o2_debug_prefix, name, "connection complete"));
            if (streql(name, o2_hub_addr)) {
                proc->uses_hub = O2_HUB_REMOTE;
                O2_DBd(printf("%s ** discovery got CONNECT from hub, %s %s\n",
                              o2_debug_prefix, "sending !_o2/hub to", name));
                o2_send_start();
                reply_msg = o2_message_finish(0.0, "!_o2/hub", true);
            }
        } else {
            O2_DBd(printf("Warning: unexpected dy type %d name %s\n",
                          dy, name));
            o2_proc_info_free(proc);
        }
    }
    o2_err_t err = O2_SUCCESS;
    if (reply_msg) {
        o2_prepare_to_deliver(reply_msg);
        err = o2_send_remote(proc, false);
    }
    if (!err) err = o2_send_clocksync_proc(proc);
    if (!err) err = o2_send_services(proc);
    if (!err) err = o2n_address_init(&proc->udp_address, ip, port, false);
    O2_DBd(printf("%s UDP port %d for remote proc %s set to %d avail as %d\n",
                  o2_debug_prefix, port, ip,
                  ntohs(proc->udp_address.sa.sin_port),
                  o2n_address_get_port(&proc->udp_address)));
    return err;
}


// send local services info to remote process. The address is !_o2/sv
// The parameters are this process name, e.g. IP:port (as a string),
// followed by (for each service): service_name, true, "", where true means
// the service exists (not deleted), and "" is a placeholder for a tappee
//
// Send taps as well.
//
// called by o2_discovery_handler in response to /_o2/dy
// the first service is the process itself, which contains important
// properties information
//
o2_err_t o2_send_services(proc_info_ptr proc)
{
    o2_send_start();
    o2_add_string(o2_ctx->proc->name);
    o2string dest = proc->name;
    enumerate enumerator;
    o2_enumerate_begin(&enumerator, &o2_ctx->path_tree.children);
    o2_node_ptr entry;
    while ((entry = o2_enumerate_next(&enumerator))) {
        services_entry_ptr services = TO_SERVICES_ENTRY(entry);
        for (int i = 0; i < services->services.length; i++) {
            service_provider_ptr spp =
                    GET_SERVICE_PROVIDER(services->services, i);
            if (!ISA_PROC(spp->service)) {
                // must be local so it's a service to report
                // ugly, but just a fast test if service is _o2:
                if ((*((int32_t *) (entry->key)) != *((int32_t *) "_o2")) &&
                    // also filter out IP:PORT entry which remote knows about
                    (!isdigit(entry->key[0]))) {
                    o2_add_string(entry->key);
                    o2_add_true();
                    o2_add_true();
                    o2_add_string(spp->properties ? spp->properties : ";");
                    O2_DBd(printf("%s o2_send_services sending %s to %s\n",
                                  o2_debug_prefix, entry->key, dest));
                }
                // can only be one locally provided service, so stop searching
                break; 
            }
        }
        for (int i = 0; i < services->taps.length; i++) {
            service_tap_ptr stp = GET_TAP_PTR(services->taps, i);
            o2_add_string(entry->key); // tappee
            o2_add_true();
            o2_add_false();
            o2_add_string(stp->tapper);
            O2_DBd(printf("%s o2_send_services sending tappee %s tapper %s"
                          "to %s\n",
                          o2_debug_prefix, entry->key, stp->tapper, dest));
        }
    }
    o2_message_ptr msg = o2_message_finish(0.0, "!_o2/sv", true);
    if (!msg) return O2_FAIL;
    o2_prepare_to_deliver(msg);
    o2_send_remote(proc, false);
    return O2_SUCCESS;
}


// send a discovery message to introduce every remote proc to new client
//
// find every connected proc, send a discovery message to each one
// Since we just connected to info, don't send to info to discover itself,
// and don't send from here since info already knows us (we are the hub).
//
// Also, to make things go faster, we send discovery message to the client
// side of the pair, so whichever name is lower gets the message.
//
static void hub_has_new_client(proc_info_ptr nc)
{
    o2n_info_ptr info;
    int i = 0;
    while ((info = o2n_get_info(i++))) {
        proc_info_ptr proc = (proc_info_ptr) (info->application);
        switch (proc->tag) {
            case PROC_NOCLOCK:
            case PROC_SYNCED: {
                proc_info_ptr client_info, server_info;
                // figure out which is the client
                int compare = strcmp(proc->name, nc->name);
                if (compare > 0) { // proc is server
                    client_info = nc;
                    server_info = proc;
                } else if (compare < 0) {
                    client_info = proc;
                    server_info = nc;
                } else {      // if equal, tag should be PROC_TCP_SERVER
                    continue; // so this should never be reached
                }
                o2_message_ptr msg = o2_make_dy_msg(server_info, true, false);
                o2_prepare_to_deliver(msg);
                int err = o2_send_remote(client_info, false);
                if (err) {
                    printf("ERROR sending discovery message from hub:\n"
                            "    client %s server %s hub %s\n",
                            client_info->name, server_info->name,
                            o2_ctx->proc->name);
                }
                O2_DBd(printf("%s hub_has_new_client %s sent %s to %s\n",
                                o2_debug_prefix, o2_ctx->proc->name,
                                server_info->name, client_info->name));
                break;
            }
            default:
                break;
        }
    }
}


// /_o2/hub handler: makes this the hub of the sender
//
void o2_hub_handler(o2_msg_data_ptr msg, const char *types,
                    o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(o2n_message_source);
    if (IS_REMOTE_PROC((o2_node_ptr) (o2n_message_source->application))) {
        o2_ctx->proc->uses_hub = O2_I_AM_HUB;
        printf("####### I am the hub #########\n");
        hub_has_new_client(TO_PROC_INFO((o2_node_ptr)
                                        (o2n_message_source->application)));
    }
}



// /_o2/sv handler: called when services become available or are removed.
// Arguments are
//     proc name,
//     service1, added_flag, service_or_tapper, properties_or_tapper,
//     service2, added_flag, service_or_tapper, properties_or_tapper,
//     ...
//
// Message was sent by o2_send_services()
// After this message is handled, this host is able to send/receive messages
//      to/from services
//
void o2_services_handler(o2_msg_data_ptr msg, const char *types,
                         o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(msg);
    o2_arg_ptr arg = o2_get_next(O2_STRING);
    if (!arg) return;
    char *name = arg->s;
    // note that name is padded with zeros to 32-bit boundary
    services_entry_ptr services;
    proc_info_ptr proc = TO_PROC_INFO(o2_service_find(name, &services));
    if (!proc || !IS_REMOTE_PROC(proc)) {
        O2_DBG(printf("%s ### ERROR: o2_services_handler did not find %s\n", 
                      o2_debug_prefix, name));
        return; // message is bogus (should we report this?)
    }
    o2_arg_ptr addarg;     // boolean - adding a service or deleting one?
    o2_arg_ptr isservicearg; // boolean - service (true) or tap (false)?
    o2_arg_ptr prop_tap_arg;  // string - properties string or tapper name
    while ((arg = o2_get_next(O2_STRING)) &&
           (addarg = o2_get_next(O2_BOOL)) &&
           (isservicearg = o2_get_next(O2_BOOL)) &&
           (prop_tap_arg = o2_get_next(O2_STRING))) {
        char *service = arg->s;
        char *prop_tap = prop_tap_arg->s;
        if (isdigit(service[0])) {
            printf("    this /sv info is for a remote proc\n");
        }
        if (strchr(service, '/')) {
            O2_DBG(printf("%s ### ERROR: o2_services_handler got bad service "
                          "name - %s\n", o2_debug_prefix, service));
        } else if (addarg->B) { // add a new service or tap from remote proc
            O2_DBd(printf("%s found service /%s offered by /%s%s%s\n",
                          o2_debug_prefix, service, proc->name,
                          (isservicearg->B ? " tapper " : ""), prop_tap));
            if (isservicearg->B) {
                o2_service_provider_new(service, prop_tap,
                                        (o2_node_ptr) proc, proc);
            } else {
                o2_tap_new(service, proc, prop_tap);
            }
        } else { // remove a service - it is no longer offered by proc
            if (isservicearg->B) {
                o2_service_remove(service, proc, NULL, -1);
            } else {
                o2_tap_remove(service, proc, prop_tap);
            }
        }
    }
}

/*********** scheduling for discovery protocol ***********/

// o2_send_discovery_at() is called from o2.c to launch discovery
//      and used by the /_o2/ds handler (below) to reschedule itself
//
void o2_send_discovery_at(o2_time when)
{
    // want to schedule another call to send again. Do not use send()
    // because we are operating off of local time, not synchronized
    // global time. Instead, form a message and schedule it:
    if (o2_send_start()) return;
    o2_message_ptr ds_msg = o2_message_finish(when, "!_o2/ds", true);
    if (!ds_msg) return;
    o2_prepare_to_deliver(ds_msg);
    o2_schedule(&o2_ltsched);
}


/// callback function that implements sending discovery messages
//   this is the handler for /_o2/ds
//
//    message args are:
//    o2_ctx->info_ip (as a string), udp port (int), tcp port (int)
//
void o2_discovery_send_handler(o2_msg_data_ptr msg, const char *types,
                    o2_arg_ptr *argv, int argc, const void *user_data)
{
    if (o2_hub_addr[0]) {
        return; // end discovery broadcasts after o2_hub()
    }
    // O2 is not going to work if we did not get a discovery port
    if (disc_port_index < 0) return;
    next_disc_index = (next_disc_index + 1) % PORT_MAX;
    // initially send two tries to all ports (i.e. mod PORT_MAX).
    // After two tries to each port, only send up to our index
    // (the other side will do the same, so at least one will work)
    // except if o2lite is enabled, keep sending to all ports because
    // the o2lite client may depend on getting any port and may not
    // be sending/broadcasting any discovery messages.
    if (disc_msg_count >= 2 * PORT_MAX || !o2lite_bridge) {
        next_disc_index = next_disc_index % (disc_port_index + 1);
    }
    o2_broadcast_message(o2_port_map[next_disc_index]);
    disc_msg_count++;
    o2_time next_time = o2_local_time() + disc_period;
    // back off rate by 10% until we're sending every max_disc_period (4s):
    disc_period *= RATE_DECAY;

    if (disc_period > max_disc_period) {
        disc_period = max_disc_period;
    }

    o2_send_discovery_at(next_time);
}


// o2_hub() - this should be like a discovery message handler that
//     just discovered a remote process, except we want to tell the
//     remote process that it is designated as our hub.
//
int o2_hub(const char *ipaddress, int port)
{
    // end broadcasting: see o2_discovery.c
    if (!ipaddress) {
        strncpy(o2_hub_addr, ".", 32);
        return O2_SUCCESS; // NULL address -> just disable broadcasting
    }
    snprintf(o2_hub_addr, 32, "%s:%d%c%c%c%c", ipaddress, port, 0, 0, 0, 0);
    return o2_discovered_a_remote_process(ipaddress, port, O2_DY_INFO);
}
