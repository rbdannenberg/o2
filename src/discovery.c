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

#define O2_DY_INFO 50
#define O2_DY_HUB 51
#define O2_DY_REPLY 52
#define O2_DY_CALLBACK 53
#define O2_DY_CONNECT 54

static void hub_has_new_client(proc_info_ptr nc);


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


// static int o2_send_discovery(o2n_info_ptr process);

// o2_discover:
//   initially send a discovery message every 0.133s, but increase the
//     interval by 10% each time until a maximum interval of 4s is
//     reached. This gives a message every 40ms on average when there
//     are 100 processes. Also gives 2 tries on 5 ports within first 2s.
//   next_discovery_index is the port we will send discover message to
//   next_discovery_recv_time is the time in seconds when we should try
//     to receive a discovery message
double next_discovery_recv_time = 0;
double o2_discovery_recv_interval = 0.1;
double o2_discovery_send_interval = 0.133;
int next_discovery_index = 0; // index to o2_port_map, port to send to
static int udp_recv_port = -1; // port we grabbed
o2_time o2_discovery_period = DEFAULT_DISCOVERY_PERIOD;
static int disc_port_index = -1;

// From Wikipedia: The range 49152–65535 (215+214 to 216−1) contains
//   dynamic or private ports that cannot be registered with IANA.[198]
//   This range is used for private, or customized services or temporary
//   purposes and for automatic allocation of ephemeral ports.
// These ports were randomly generated from that range.
int o2_port_map[PORT_MAX] = { 64541, 60238, 57143, 55764, 56975, 62711,
                              57571, 53472, 51779, 63714, 53304, 61696,
                              50665, 49404, 64828, 54859 };


// initialize this module: creates a UDP receive port and starts discovery
//
int o2_discovery_initialize()
{
    // Create socket to receive UDP (discovery and other)
    // Try to find an available port number from the discover port map.
    // If there are no available port number, print the error & return O2_FAIL.
    for (disc_port_index = 0; disc_port_index < PORT_MAX; disc_port_index++) {
        udp_recv_port = o2_port_map[disc_port_index];
        o2n_info_ptr server = o2n_udp_server_new(&udp_recv_port, NULL);
        if (!server) {
            break;
        }
    }
    if (disc_port_index >= PORT_MAX) {
        udp_recv_port = -1; // no port to receive discovery messages
        disc_port_index = -1;
        fprintf(stderr, "Unable to allocate a discovery port.\n");
        return O2_NO_PORT;
    } else {
        // use the discovery message receive port as the general UDP receive port
        o2_context->proc->udp_address.port = udp_recv_port;
    }
    O2_DBdo(printf("%s **** discovery port %ld (%d already taken).\n",
                   o2_debug_prefix, (long) udp_recv_port, disc_port_index));

    // do not run immediately so that user has a chance to call o2_hub() first,
    // which will disable discovery. This is not really time-dependent because
    // no logical time will pass until o2_poll() is called.
    o2_send_discovery_at(o2_local_time() + 0.01);
    return O2_SUCCESS;
}


int o2_discovery_finish(void)
{
    return O2_SUCCESS;
}


/**
 * Make /_o2/dy message, if this is a udp message, switch to network byte order
 */
static o2_message_ptr make_o2_dy_msg(proc_info_ptr proc, int tcp_flag,
                                     int dy_flag)
{
    /*printf("make_o2_dy_msg: ensemble %s name %s\n",
           o2_ensemble_name, info->proc.name); */
    char buffer[32];
    char *ip;
    int port;
    assert(o2n_found_network);
    if (proc == o2_context->proc) {
        ip = o2n_local_ip;
        port = o2n_local_tcp_port;
    } else {
        ip = buffer;
        if (extract_ip_port(proc->name, ip, &port))
            return NULL;
    }

    int err = o2_send_start() || o2_add_string(o2_ensemble_name) ||
        o2_add_string(ip) || o2_add_int32(port) ||
        o2_add_int32(proc->udp_address.port) || o2_add_int32(dy_flag);
    if (err) return NULL;
    o2_message_ptr msg = o2_message_finish(0.0, "!_o2/dy", tcp_flag);
    if (!msg) return NULL;
#if IS_LITTLE_ENDIAN
    if (!tcp_flag) {
        o2_msg_swap_endian(&msg->data, TRUE);
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
    o2_message_ptr m = make_o2_dy_msg(o2_context->proc, FALSE, O2_DY_INFO);
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
    // since we own udp_recv_port, there is no need to send in that case
    // because any broadcast message is a discovery message and we don't
    // need to discover ourselves.
    if (port != udp_recv_port) {
        o2n_send_udp_local(port, (o2n_message_ptr) m);
    } else {
        O2_FREE(m);
    }
}


// /_o2/dy handler, parameters are: ensemble name, ip, tcp, udp, sync
//
// If we are the server, send discovery message to client and we are done.
// If we are the client, o2_send_services()
//
void o2_discovery_handler(o2_msg_data_ptr msg, const char *types,
                          o2_arg_ptr *argv, int argc, void *user_data)
{
    O2_DBd(o2_dbg_msg("o2_discovery_handler gets", msg, NULL, NULL));
    o2_arg_ptr ens_arg, ip_arg, tcp_arg, udp_arg, dy_arg;
    // get the arguments: ensemble name, ip as string,
    //                    tcp port, discovery port
    o2_extract_start(msg);
    if (!(ens_arg = o2_get_next('s')) ||
        !(ip_arg = o2_get_next('s')) ||
        !(tcp_arg = o2_get_next('i')) ||
        !(udp_arg = o2_get_next('i')) ||
        !(dy_arg = o2_get_next('i'))) {
        return;
    }
    char *ens = ens_arg->s;
    char *ip = ip_arg->s;
    int tcp = tcp_arg->i32;
    int udp = udp_arg->i32;
    int dy = dy_arg->i32;
    
    if (!streql(ens, o2_ensemble_name)) {
        O2_DBd(printf("    Ignored: ensemble name is not %s\n", 
                      o2_ensemble_name));
        return;
    }
    o2_discovered_a_remote_process(ip, tcp, udp, dy);
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
int o2_discovered_a_remote_process(const char *ip, int tcp, int udp, int dy)
{
    o2n_info_ptr remote = o2n_message_source;
    if (dy == O2_DY_CALLBACK) { // similar to info, but close connection first
        o2n_close_socket(remote); // we are going to be the client
        dy = O2_DY_INFO; 
    }

    char name[32];
    // ip:port + pad with zeros
    snprintf(name, 32, "%s:%d%c%c%c%c", ip, tcp, 0, 0, 0, 0);

    proc_info_ptr proc = NULL;
    o2_message_ptr reply_msg = NULL;
    if (dy == O2_DY_INFO) {
        int compare = strcmp(o2_context->proc->name, name);
        if (compare == 0) {
            O2_DBd(printf("   Ignored: I received my own broadcast message\n"));
            return O2_SUCCESS; // the "discovered process" is this one
        }
        o2_node_ptr *entry_ptr = o2_lookup(&o2_context->path_tree, name);
        if (*entry_ptr) { // process is already discovered, ignore message
            O2_DBd(printf("%s ** process already discovered, ignore %s\n",
                          o2_debug_prefix, name));
            return O2_SUCCESS;
        }
        // process is unknown, make a proc_info for it and start connecting...
        proc = o2_create_tcp_proc(PROC_TEMP, ip, tcp); // proc name is NULL

        // we are the server
        if (compare > 0) { // the other party should connect
            if (!proc) {
                return O2_FAIL;
            }
            // send /dy by TCP
            if (o2_send_remote(make_o2_dy_msg(o2_context->proc, TRUE,
                                   O2_DY_CALLBACK), proc) != O2_SUCCESS) {
                o2_proc_info_free(proc); // error recovery: don't leak memory
            } else {
                // this connection will be closed by receiving client
                O2_DBd(printf("%s ** discovery sending CALLBACK to %s\n",
                              o2_debug_prefix, name));
            }
            return O2_SUCCESS;
        }

        // else we are the client
        proc->tag = PROC_NOCLOCK;
        proc->name = o2_heapify(name);
        o2_service_provider_new(name, NULL, (o2_node_ptr) proc, proc);
        O2_DBg(printf("%s ** discovery sending CONNECT to server %s\n",
                      o2_debug_prefix, name));
        reply_msg = make_o2_dy_msg(o2_context->proc, TRUE, O2_DY_CONNECT);
    } else {
        proc = TO_PROC_INFO(remote->application);
        proc->name = o2_heapify(name);
        o2_service_provider_new(proc->name, NULL, (o2_node_ptr) proc, proc);
        if (dy == O2_DY_HUB) { // this is the hub, this is the server side
            // send a /dy to remote with O2_DY_REPLY
            O2_DBd(printf("%s ** discovery got HUB sending REPLY to hub %s\n",
                          o2_debug_prefix, name));
            reply_msg = make_o2_dy_msg(o2_context->proc, TRUE, O2_DY_REPLY);
        } else if (dy == O2_DY_REPLY) { // first message from hub
            if (!streql(name, o2_hub_addr)) { // should be equal
                printf("Warning: expected O2_DY_REPLY to be from hub\n");
                o2n_close_socket(remote);
                return O2_FAIL;
            }
            proc->uses_hub = O2_HUB_REMOTE;
            o2_send_start();
            reply_msg = o2_message_finish(0.0, "!_o2/hub", TRUE);
            O2_DBd(printf("%s ** discovery got REPLY sending !_o2/hub %s\n",
                          o2_debug_prefix, name));
        } else if (dy == O2_DY_CONNECT) { // similar to info, but close connection
            O2_DBg(printf("%s ** discovery got CONNECT from client %s, %s\n",
                           o2_debug_prefix, name, "connection complete"));
            if (streql(name, o2_hub_addr)) {
                proc->uses_hub = O2_HUB_REMOTE;
                O2_DBd(printf("%s ** discovery got CONNECT from hub, %s %s\n",
                              o2_debug_prefix, "sending !_o2/hub to", name));
                o2_send_start();
                reply_msg = o2_message_finish(0.0, "!_o2/hub", TRUE);
            }
        } else {
            O2_DBd(printf("Warning: unexpected dy type %d name %s\n", dy, name));
            o2_proc_info_free(proc);
        }
    }
    int err = o2_send_remote(reply_msg, proc) ||
              o2_send_clocksync(proc) ||
              o2_send_services(proc) ||
             o2n_address_init(&(proc->udp_address), ip, udp, FALSE);
    return err;
}


// send local services info to remote process. The address is !_o2/sv
// The parameters are this process name, e.g. IP:port (as a string),
// followed by (for each service): service_name, TRUE, "", where TRUE means
// the service exists (not deleted), and "" is a placeholder for a tappee
//
// Send taps as well.
//
// called by o2_discovery_handler in response to /_o2/dy
// the first service is the process itself, which contains important
// properties information
//
int o2_send_services(proc_info_ptr proc)
{
    o2_send_start();
    o2_add_string(o2_context->proc->name);
    o2string dest = proc->name;
    enumerate enumerator;
    o2_enumerate_begin(&enumerator, &o2_context->path_tree.children);
    o2_node_ptr entry;
    while ((entry = o2_enumerate_next(&enumerator))) {
        services_entry_ptr services = TO_SERVICES_ENTRY(entry);
        for (int i = 0; i < services->services.length; i++) {
            service_provider_ptr spp = GET_SERVICE_PROVIDER(services->services, i);
            if (!ISA_PROC(spp->service)) {
                // must be local so it's a service to report
                // ugly, but just a fast test if service is _o2:
                if ((*((int32_t *) (entry->key)) != *((int32_t *) "_o2")) &&
                    // also filter out IP:PORT entry which remote knows about
                    (!isdigit(entry->key[0]))) {
                    o2_add_string(entry->key);
                    o2_add_true();
                    o2_add_true();
                    o2_add_string(spp->properties ? spp->properties + 1 : "");
                    O2_DBd(printf("%s o2_send_services sending %s to %s\n",
                                  o2_debug_prefix, entry->key, dest));
                }
                break; // can only be one locally provided service, so stop searching
            }
        }
        for (int i = 0; i < services->taps.length; i++) {
            service_tap_ptr stp = GET_TAP_PTR(services->services, i);
            o2_add_string(entry->key); // tappee
            o2_add_true();
            o2_add_false();
            o2_add_string(stp->tapper);
            O2_DBd(printf("%s o2_send_services sending tappee %s tapper %s to %s\n",
                          o2_debug_prefix, entry->key, stp->tapper, dest));
        }
    }
    o2_message_ptr msg = o2_message_finish(0.0, "!_o2/sv", TRUE);
    if (!msg) return O2_FAIL;
    o2_send_remote(msg, proc);
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
                o2_message_ptr msg = make_o2_dy_msg(server_info, TRUE, FALSE);
                int err = o2_send_remote(msg, client_info);
                if (err) {
                    printf("ERROR sending discovery message from hub:\n"
                            "    client %s server %s hub %s\n",
                            client_info->name, server_info->name,
                            o2_context->proc->name);
                }
                O2_DBd(printf("%s hub_has_new_client %s sent %s to %s\n",
                                o2_debug_prefix, o2_context->proc->name,
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
                    o2_arg_ptr *argv, int argc, void *user_data)
{
    assert(o2n_message_source);
    if (PROC_IS_REMOTE((o2_node_ptr) (o2n_message_source->application))) {
        o2_context->proc->uses_hub = O2_I_AM_HUB;
        hub_has_new_client(TO_PROC_INFO(o2n_message_source));
    }
}



// /_o2/sv handler: called when services become available or are removed. Arguments are
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
                         o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_extract_start(msg);
    o2_arg_ptr arg = o2_get_next('s');
    if (!arg) return;
    char *name = arg->s;
    // note that name is padded with zeros to 32-bit boundary
    services_entry_ptr services;
    proc_info_ptr proc = TO_PROC_INFO(o2_service_find(name, &services));
    if (!proc || !PROC_IS_REMOTE(proc)) {
        O2_DBg(printf("%s ### ERROR: o2_services_handler did not find %s\n", 
                      o2_debug_prefix, name));
        return; // message is bogus (should we report this?)
    }
    o2_arg_ptr addarg;     // boolean - adding a service or deleting one?
    o2_arg_ptr isservicearg; // boolean - service (true) or tap (false)?
    o2_arg_ptr prop_tap_arg;  // string - properties string or tapper name
    while ((arg = o2_get_next('s')) && (addarg = o2_get_next('B')) &&
           (isservicearg = o2_get_next('B')) &&
           (prop_tap_arg = o2_get_next('s'))) {
        char *service = arg->s;
        char *prop_tap = prop_tap_arg->s;
        if (strchr(service, '/')) {
            O2_DBg(printf("%s ### ERROR: o2_services_handler got bad service "
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
    o2_message_ptr ds_msg = o2_message_finish(when, "!_o2/ds", TRUE);
    if (!ds_msg) return;
    o2_schedule(&o2_ltsched, ds_msg);
}


/// callback function that implements sending discovery messages
//   this is the handler for /_o2/ds
//
//    message args are:
//    o2_context->info_ip (as a string), udp port (int), tcp port (int)
//
void o2_discovery_send_handler(o2_msg_data_ptr msg, const char *types,
                               o2_arg_ptr *argv, int argc, void *user_data)
{
    if (o2_hub_addr[0]) {
        return; // end discovery broadcasts after o2_hub()
    }
    // O2 is not going to work if we did not get a discovery port
    if (disc_port_index < 0) return;
    next_discovery_index = (next_discovery_index + 1) % (disc_port_index + 1);
    o2_broadcast_message(o2_port_map[next_discovery_index]);
    o2_time next_time = o2_local_time() + o2_discovery_send_interval;
    // back off rate by 10% until we're sending every o2_discovery_period (4s):
    o2_discovery_send_interval *= 1.1;

    if (o2_discovery_send_interval > o2_discovery_period) {
        o2_discovery_send_interval = o2_discovery_period;
    }

    o2_send_discovery_at(next_time);
}

