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
static int o2_port_map[PORT_MAX] = { 64541, 60238, 57143, 55764, 56975, 62711,
                              57571, 53472, 51779, 63714, 53304, 61696,
                              50665, 49404, 64828, 54859 };
static int o2_local_remote[PORT_MAX] = {3, 3, 3, 3, 3, 3, 3, 3,
                                        3, 3, 3, 3, 3, 3, 3, 3};
// picked from o2_port_map when discovery is initialized.
o2n_info_ptr o2_discovery_udp_server = NULL;

static bool hub_needs_public_ip = false;
static char hub_pip[O2_IP_LEN];
static char hub_iip[O2_IP_LEN];
static int hub_port;

static void hub_has_new_client(proc_info_ptr nc);


o2_time o2_set_discovery_period(o2_time period)
{
    o2_time old = max_disc_period;
    if (period < 0.1) period = 0.1;
    disc_period = period;
    max_disc_period = period;
    return old;
}


int o2_parse_name(const char *name, char *public_ip,
                  char *internal_ip, int *port)
{
    char *colon = strchr(name, ':');
    if (!colon || colon - name > O2_IP_LEN - 1) {
        return O2_FAIL;
    }
    colon++;  // colon is first char after ':'
    o2strcpy(public_ip, name, colon - name);
    char *colon2 = strchr(colon, ':');
    if (!colon2 || colon2 - colon > O2_IP_LEN - 1) {
        return O2_FAIL;
    }
    colon2++;  // colon2 is first char after second ':'
    o2strcpy(internal_ip, colon, colon2 - colon);
    *port = atoi(colon2);
    return O2_SUCCESS;
}


// initialize this module: creates a UDP receive port and starts discovery
// also ensures that we can open the selected port for both UDP and TCP
// servers.
//
o2_err_t o2_discovery_initialize()
{
    o2_hub_addr[0] = 0;
    hub_needs_public_ip = false;
    disc_period = INITIAL_DISCOVERY_PERIOD;
    next_disc_index = -1; // gets incremented before first use
    // Create socket to receive UDP (discovery and other)
    // Try to find an available port number from the discover port map.
    // If there are no available port number, print the error & return O2_FAIL.
    for (disc_port_index = 0; disc_port_index < PORT_MAX; disc_port_index++) {
        my_port = o2_port_map[disc_port_index];
        o2_discovery_udp_server = o2n_udp_server_new(&my_port, false, NULL);
        if (o2_discovery_udp_server) {
            o2_ctx->proc = o2_create_tcp_proc(PROC_TCP_SERVER, NULL,
                                                         my_port);
            if (o2_ctx->proc) {
                break;
            }
        }
    }
    if (disc_port_index >= PORT_MAX) {
        my_port = -1; // no port to receive discovery messages
        disc_port_index = -1;
        fprintf(stderr, "Unable to allocate a discovery port.\n");
        return O2_NO_PORT;
    }
    // use the discovery message receive port as
    // the general UDP receive and TCP server port
    O2_DBdo(printf("%s **** discovery port %ld (%d already taken).\n",
                   o2_debug_prefix, (long) my_port, disc_port_index));
    
    // do not send local discovery msg to this port
    o2_local_remote[disc_port_index] &= ~1;
    
    // do not run until the STUN protocol determines the public IP
    // This also allows the user to call o2_hub() and disable discovery
    // before any messages are sent. This is not really a race because
    // no reply will return until o2_poll() is called.

    return O2_SUCCESS;
}


void o2_discovery_init_phase2()
{
    if (hub_needs_public_ip) {
        snprintf(o2_hub_addr, O2_MAX_PROCNAME_LEN, "%s:%s:%d%c%c%c%c",
                 hub_pip, hub_iip, hub_port, 0, 0, 0, 0);
        o2_discovered_a_remote_process(hub_pip, hub_iip, hub_port, O2_DY_INFO);
        hub_needs_public_ip = false;  // unlock o2_hub() for additional calls
    }
    o2_method_new_internal("/_o2/dy", "sssii", &o2_discovery_handler,
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
    char public_ip_buff[O2_IP_LEN];
    char internal_ip_buff[O2_IP_LEN];
    char *public_ip;
    char *internal_ip = internal_ip_buff;
    int port;
    assert(o2n_public_ip);
    assert(o2n_internal_ip);
    if (proc == o2_ctx->proc) {
        public_ip = o2n_public_ip;
        internal_ip = o2n_internal_ip;
        port = proc->net_info->port;
    } else {
        public_ip  = public_ip_buff;
        internal_ip = internal_ip_buff;
        if (o2_parse_name(proc->name, public_ip_buff, internal_ip_buff, &port))
            return NULL;
    }

    int err = o2_send_start() || o2_add_string(o2_ensemble_name) ||
        o2_add_string(public_ip) || o2_add_string(internal_ip) ||
        o2_add_int32(port) || o2_add_int32(dy_flag);
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
 * @param local_remote send local (1), send remote (2) or send both (3)
 *
 * Receiver will get message and call o2_discovery_handler()
 */
static o2_err_t o2_broadcast_message(int port, int local_remote)
{
    if (local_remote == 0) {  // no sending is enabled
        return O2_SUCCESS;
    }
    // set up the address and port
    o2_message_ptr m = o2_make_dy_msg(o2_ctx->proc, false, O2_DY_INFO);
    // m is in network order
    if (!m) {
        return O2_FAIL;
    }
    
    // broadcast the message remotely if remote flag is set
    if (o2n_network_found && (local_remote & 2)) {
        O2_DBd(printf("%s broadcasting discovery msg to port %d\n",
                      o2_debug_prefix, port));
        if (o2n_send_broadcast(port, (o2n_message_ptr) m) < 0) {
            O2_FREE(m);
            return O2_SEND_FAIL; // skips local send, but that's OK because
            // next time, remote flag will be cleared and we'll do local send.
        }
    }
    // assume that broadcast messages are not received on the local machine
    // so we have to send separately to localhost using the same port;
    // If the port is our own o2_discovery_port, local flag will be 0,
    // and we skip the local send (no sense sending to ourselves).
    if (local_remote & 1) {
        o2n_send_udp_local(port, (o2n_message_ptr) m); // frees m
    } else {
        O2_FREE(m);
    }
    return O2_SUCCESS;
}


// /_o2/dy handler, parameters are:
//     ensemble name, public_ip, internal_ip, port, dy_type
//
// If we are the server, send discovery message to client and we are done.
// If we are the client, o2_send_services()
//
void o2_discovery_handler(o2_msg_data_ptr msg, const char *types,
                          o2_arg_ptr *argv, int argc, const void *user_data)
{
    O2_DBd(o2_dbg_msg("o2_discovery_handler gets", NULL, msg, NULL, NULL));
    o2_arg_ptr ens_arg, pip_arg, iip_arg, tcp_arg, dy_arg;
    // get the arguments: ensemble name, ip as string,
    //                    port, discovery port
    o2_extract_start(msg);
    if (!(ens_arg = o2_get_next(O2_STRING)) ||
        !(pip_arg = o2_get_next(O2_STRING)) ||
        !(iip_arg = o2_get_next(O2_STRING)) ||
        !(tcp_arg = o2_get_next(O2_INT32)) ||
        !(dy_arg = o2_get_next(O2_INT32))) {
        return;
    }
    const char *ens = ens_arg->s;
    const char *public_ip = pip_arg->s;
    const char *internal_ip = iip_arg->s;
    int port = tcp_arg->i32;
    int dy = dy_arg->i32;
    
    if (!streql(ens, o2_ensemble_name)) {
        O2_DBd(printf("    Ignored: ensemble name %s is not %s\n", 
                      ens, o2_ensemble_name));
        return;
    }
    o2_discovered_a_remote_process(public_ip, internal_ip, port, dy);
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
o2_err_t o2_discovered_a_remote_process(const char *public_ip,
                       const char *internal_ip, int port, int dy)
{
    // note: in the case of o2_hub(), there may be no incoming discovery
    // message and so remote will be bogus, but since o2_hug() passes
    // O2_DY_INFO for dy, remote will not be used so the value is immaterial
    o2n_info_ptr remote = o2n_message_source;
    if (dy == O2_DY_CALLBACK) { // similar to info, but close connection first
        o2n_close_socket(remote); // we are going to be the client
        dy = O2_DY_INFO; 
    }

    char name[O2_MAX_PROCNAME_LEN];
    // public:internal:port + pad with zeros
    snprintf(name, O2_MAX_PROCNAME_LEN, "%s:%s:%d%c%c%c%c",
             public_ip, internal_ip, port, 0, 0, 0, 0);

    proc_info_ptr proc = NULL;
    o2_message_ptr reply_msg = NULL;
    if (dy == O2_DY_INFO) {
        assert(o2_ctx->proc->name);
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
        proc = o2_create_tcp_proc(PROC_TEMP, internal_ip, port);
        // proc name is NULL

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
        assert(proc->name == NULL);  // make sure we don't leak memory
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
            // similar to info, but sender has just made a tcp connection
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
    if (!err) err = o2n_address_init(&proc->udp_address,
                                     internal_ip, port, false);
    O2_DBd(printf("%s UDP port %d for remote proc %s set to %d avail as %d\n",
                  o2_debug_prefix, port, internal_ip,
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
    assert(o2_ctx->proc->name);
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
                    // also filter out public:internal:port entry which remote
                    // knows about
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
                    assert(o2_ctx->proc->name);
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
                      o2_debug_prefix, name);
               o2_node_show((o2_node_ptr) (&o2_ctx->path_tree), 2));
        
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
    int local_remote = o2_local_remote[next_disc_index];
    if (local_remote) {
        o2_err_t err = o2_broadcast_message(o2_port_map[next_disc_index],
                                            local_remote);
        if (err == O2_SEND_FAIL) { // broadcast failed, so disable it
            o2_local_remote[next_disc_index] &= ~2;
        }
    }
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
int o2_hub(const char *public_ip, const char *internal_ip, int port)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    // end broadcasting: see o2_discovery.c
    if (!public_ip || !internal_ip) {
        strncpy(o2_hub_addr, ".", O2_MAX_PROCNAME_LEN);
        return O2_SUCCESS; // NULL address -> just disable broadcasting
    }
    if (hub_needs_public_ip) {
        return O2_FAIL;  // second call to o2_hub() before we know Public IP
        // we do not queue up pending discovery messages to hubs, so we fail
    }
    if (o2n_public_ip[0]) {
        snprintf(o2_hub_addr, O2_MAX_PROCNAME_LEN, "%s:%s:%d%c%c%c%c",
                 public_ip, internal_ip, port, 0, 0, 0, 0);
        return o2_discovered_a_remote_process(public_ip, internal_ip,
                                              port, O2_DY_INFO);
    } else {
        o2strcpy(hub_pip, public_ip, O2_IP_LEN);
        o2strcpy(hub_iip, internal_ip, O2_IP_LEN);
        hub_port = port;
        hub_needs_public_ip = true;
    }
    return O2_SUCCESS;
}
