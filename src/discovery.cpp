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
#include "pathtree.h"

#ifndef O2_NO_O2DISCOVERY
// These parameters hit all 16 ports in 3.88s, then all 16 again by 30s,
// Then send every 4s to ports up to our index. So if we are able to open
// the first discovery port, we send a discovery message every 4s to that
// port only. If we open the 16th discovery port, we send to each one 
// every 64s.
//
// We also give 2 tries on all ports initially, then only send to ports
// less than or equal to our discovery index.
//
// With 100 processes, we send and get 25 messages per second, which
// seems too high. We'll use the size of o2n_fds_info and
// o2_mqtt_procs to estimate the number of processes out there and
// increase the DEFAULT_DISCOVERY_PERIOD by (num_procs - 40/ 10) if
// non-negative so that the max messages per second in each direction
// is 10.
//
#define INITIAL_DISCOVERY_PERIOD 0.1
#define DEFAULT_DISCOVERY_PERIOD 4.0
#define RATE_DECAY 1.125

static int disc_msg_count = 0;
static double disc_period = INITIAL_DISCOVERY_PERIOD;
static int next_disc_index = 0; // index to o2_port_map, port to send to
static O2time max_disc_period = DEFAULT_DISCOVERY_PERIOD;
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
#endif
Fds_info *o2_udp_server = NULL;

static int my_tcp_port = -1; // port we grabbed, for TCP (maybe also UDP)

#ifndef O2_NO_HUB
static bool hub_needs_public_ip = false;
static char hub_pip[O2N_IP_LEN];
static char hub_iip[O2N_IP_LEN];
static int hub_tcp_port;
static int hub_udp_port;

static void hub_has_new_client(Proc_info *nc);
#endif

#ifndef O2_NO_O2DISCOVERY
O2time o2_set_discovery_period(O2time period)
{
    O2time old = max_disc_period;
    if (period < 0.1) period = 0.1;
    disc_period = period;
    max_disc_period = period;
    return old;
}
#endif

int o2_parse_name(const char *name, char *public_ip,
                  char *internal_ip, int *port)
{
    const char *colon = strchr(name, ':');
    if (*name != '@') {
        return O2_FAIL;
    }
    if (!colon || colon - name > O2N_IP_LEN - 1) {
        return O2_FAIL;
    }
    // we allow extra char after public ip, which becomes EOS:
    o2_strcpy(public_ip, name + 1, colon - name);
    colon++;  // colon is now first char after ':'
    const char *colon2 = strchr(colon, ':');
    if (!colon2 || colon2 - colon > O2N_IP_LEN - 1) {
        return O2_FAIL;
    }
    colon2++;  // colon2 is first char after second ':'
    o2_strcpy(internal_ip, colon, colon2 - colon);
    *port = o2_hex_to_int(colon2);
    return O2_SUCCESS;
}


// initialize this module: creates a UDP receive port and starts discovery
// also ensures that we can open the selected port for both UDP and TCP
// servers.
//
O2err o2_discovery_initialize()
{
#ifndef O2_NO_HUB
    o2_hub_addr[0] = 0;
    hub_needs_public_ip = false;
#endif
#ifndef O2_NO_O2DISCOVERY
    disc_period = INITIAL_DISCOVERY_PERIOD;
    next_disc_index = -1; // gets incremented before first use
    // Create socket to receive UDP (discovery and other)
    // Try to find an available port number from the discover port map.
    // If there are no available port number, print the error & return O2_FAIL.
    for (disc_port_index = 0; disc_port_index < PORT_MAX; disc_port_index++) {
        my_tcp_port = o2_port_map[disc_port_index];
        o2_udp_server = Fds_info::create_udp_server(&my_tcp_port, false);
        if (o2_udp_server) {
            o2_ctx->proc = Proc_info::create_tcp_proc(O2TAG_PROC_TCP_SERVER,
                                                      NULL, &my_tcp_port);
            if (o2_ctx->proc) {
                break;
            }
        }
    }
    if (disc_port_index >= PORT_MAX) {
        my_tcp_port = -1; // no port to receive discovery messages
        disc_port_index = -1;
        fprintf(stderr, "Unable to allocate a discovery port.\n");
        return O2_NO_PORT;
    }
    O2_DBdo(printf("%s **** discovery port %ld (%d already taken).\n",
               o2_debug_prefix, (long) my_tcp_port, disc_port_index));
#else
    // no fixed port list, so any port will do
    int my_udp_port = 0;
    my_tcp_port = 0;
    o2_udp_server = Fds_info::create_udp_server(&my_udp_port, false);
    if (o2_udp_server) {
        o2_ctx->proc = Proc_info::create_tcp_proc(O2TAG_PROC_TCP_SERVER,
                                                  NULL, &my_tcp_port);
    }
    if (!o2_udp_server || !o2_ctx->proc) {
        return O2_FAIL;
    }
#endif
    O2_DBc(o2_ctx->proc->co_info(o2_udp_server, "created upd server port");
           o2_ctx->proc->co_info(o2_ctx->proc->fds_info,
                                 "created tcp server port"));
#ifndef O2_NO_O2DISCOVERY
    // do not send local discovery msg to this port
    o2_local_remote[disc_port_index] &= ~1;
#endif    
    // do not run until the STUN protocol determines the public IP
    // This also allows the user to call o2_hub() and disable discovery
    // before any messages are sent. This is not really a race because
    // no reply will return until o2_poll() is called.

    return O2_SUCCESS;
}


void o2_discovery_init_phase2()
{
#ifndef O2_NO_O2DISCOVERY
    o2_method_new_internal("/_o2/ds", NULL, &o2_discovery_send_handler,
                           NULL, false, false);
#ifndef O2_NO_HUB
    if (hub_needs_public_ip) {
        snprintf(o2_hub_addr, O2_MAX_PROCNAME_LEN, "@%s:%s:%04x%c%c%c%c",
                 hub_pip, hub_iip, hub_tcp_port, 0, 0, 0, 0);
        o2_discovered_a_remote_process(hub_pip, hub_iip, hub_tcp_port,
                                       hub_udp_port, O2_DY_INFO);
        hub_needs_public_ip = false;  // unlock o2_hub() for additional calls
    }
    o2_method_new_internal("/_o2/hub", "", &o2_hub_handler,
                           NULL, false, false);
#endif
#endif
    o2_method_new_internal("/_o2/sv", NULL, &o2_services_handler,
                           NULL, false, false);
    o2_method_new_internal("/_o2/dy", "sssiii", &o2_discovery_handler,
                           NULL, false, false);
}


O2err o2_discovery_finish(void)
{
    return O2_SUCCESS;
}


/**
 * Make /_o2/dy message, if swap_flag, switch to network byte order
 */
O2message_ptr o2_make_dy_msg(Proc_info *proc, bool tcp_flag, bool swap_flag,
                             int dy_flag)
{
    char public_ip_buff[O2N_IP_LEN];
    char internal_ip_buff[O2N_IP_LEN];
    char *public_ip;
    char *internal_ip;
    int tcp_port;
    int udp_port;
    assert(o2n_public_ip);
    assert(o2n_internal_ip);
    // careful: if our TCP socket is closed, proc->fds_info->port will
    // not exist. Instead, we just return NULL
    if (proc == o2_ctx->proc && proc->fds_info) {
        public_ip = o2n_public_ip;
        internal_ip = o2n_internal_ip;
        tcp_port = proc->fds_info->port;
    } else {
        public_ip  = public_ip_buff;
        internal_ip = internal_ip_buff;
        if (o2_parse_name(proc->key, public_ip_buff, internal_ip_buff,
                          &tcp_port))
            return NULL;
    }
    udp_port = proc->udp_address.get_port();

    int err = o2_send_start() || o2_add_string(o2_ensemble_name) ||
        o2_add_string(public_ip) || o2_add_string(internal_ip) ||
        o2_add_int32(tcp_port) || o2_add_int32(udp_port) ||
        o2_add_int32(dy_flag);
    if (err) return NULL;
    O2message_ptr msg = o2_message_finish(0.0, "!_o2/dy", tcp_flag);
    if (!msg) return NULL;
#if IS_LITTLE_ENDIAN
    if (swap_flag) {
        o2_msg_swap_endian(&msg->data, true);
    }
#endif
    return msg;
}

#ifndef O2_NO_O2DISCOVERY
/**
 * Broadcast discovery message (!o2/dy) to a discovery port.
 *
 * @param port  the destination port number
 * @param local_remote send local (1), send remote (2) or send both (3)
 *
 * Receiver will get message and call o2_discovery_handler()
 */
static O2err o2_broadcast_message(int port, int local_remote)
{
    if (local_remote == 0) {  // no sending is enabled
        return O2_SUCCESS;
    }
    // set up the address and port
    O2message_ptr m = o2_make_dy_msg(o2_ctx->proc, false, true, O2_DY_INFO);
    // m is in network order
    if (!m) {
        return O2_FAIL;
    }
    
    // broadcast the message remotely if remote flag is set
    if (o2n_network_found && (local_remote & 2)) {
        O2_DBd(printf("%s broadcasting discovery msg to port %d\n",
                      o2_debug_prefix, port));
        if (o2n_send_broadcast(port, (O2netmsg_ptr) m) < 0) {
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
        o2n_send_udp_local(port, (O2netmsg_ptr) m); // frees m
    } else {
        O2_FREE(m);
    }
    return O2_SUCCESS;
}
#endif

// /_o2/dy handler, parameters are:
//     ensemble name, public_ip, internal_ip, tcp_port, udp_port, dy_type
//
// If we are the server, send discovery message to client and we are done.
// If we are the client, o2_send_services()
//
void o2_discovery_handler(o2_msg_data_ptr msg, const char *types,
                          O2arg_ptr *argv, int argc, const void *user_data)
{
    O2_DBd(o2_dbg_msg("o2_discovery_handler gets", NULL, msg, NULL, NULL));
    O2arg_ptr ens_arg, pip_arg, iip_arg, tcp_arg, udp_arg, dy_arg;
    // get the arguments: ensemble name, ip as string,
    //                    port, discovery port
    o2_extract_start(msg);
    if (!(ens_arg = o2_get_next(O2_STRING)) ||
        !(pip_arg = o2_get_next(O2_STRING)) ||
        !(iip_arg = o2_get_next(O2_STRING)) ||
        !(tcp_arg = o2_get_next(O2_INT32)) ||
        !(udp_arg = o2_get_next(O2_INT32)) ||
        !(dy_arg = o2_get_next(O2_INT32))) {
        return;
    }
    const char *ens = ens_arg->s;
    const char *public_ip = pip_arg->s;
    const char *internal_ip = iip_arg->s;
    int tcp_port = tcp_arg->i32;
    int udp_port = udp_arg->i32;
    int dy = dy_arg->i32;
    
    if (!streql(ens, o2_ensemble_name)) {
        O2_DBd(printf("    Ignored: ensemble name %s is not %s\n", 
                      ens, o2_ensemble_name));
        return;
    }
    o2_discovered_a_remote_process(public_ip, internal_ip, tcp_port, udp_port,
                                   dy);
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
// public_ip and internal_ip are in hex notation
O2err o2_discovered_a_remote_process(const char *public_ip,
        const char *internal_ip, int tcp_port, int udp_port, int dy)
{
    // note: in the case of o2_hub(), there may be no incoming discovery
    // message and so remote will be bogus, but since o2_hug() passes
    // O2_DY_INFO for dy, remote will not be used so the value is immaterial
    if (dy == O2_DY_CALLBACK && o2_message_source) { // similar to info,
        // but close connection first. In this case, we expect the socket
        // to be a temporary TCP connection created just to reliably request
        // a connection by the client to the server. We can also get an
        // O2_DY_CALLBACK via MQTT, for example, when broadcasting is
        // disabled. In that case, o2_message_source == NULL which is good
        // because we do NOT want to shut down our MQTT broker connection.
        o2_message_source->fds_info->close_socket(true);
        dy = O2_DY_INFO; 
    }

    char name[O2_MAX_PROCNAME_LEN];
    // @public:internal:port + pad with zeros
    snprintf(name, O2_MAX_PROCNAME_LEN, "@%s:%s:%04x%c%c%c%c",
             public_ip, internal_ip, tcp_port, 0, 0, 0, 0);

    O2_DBd(printf("    o2_discovery_handler: remote %s local %s\n",
                  name, o2_ctx->proc->key));
    return o2_discovered_a_remote_process_name(name, internal_ip, tcp_port,
                                               udp_port, dy);
}


O2err o2_discovered_a_remote_process_name(const char *name,
        const char *internal_ip, int tcp_port, int udp_port, int dy)
{
    Proc_info *proc = NULL;
    O2message_ptr reply_msg = NULL;
    if (dy == O2_DY_INFO) {
        assert(o2_ctx->proc->key);
        int compare = strcmp(o2_ctx->proc->key, name);
        if (compare == 0) {
            O2_DBd(printf("%s Ignored: I received my own broadcast message\n",
                          o2_debug_prefix));
            return O2_SUCCESS; // the "discovered process" is this one
        }
        O2node **entry_ptr = o2_ctx->path_tree.lookup(name);
        if (*entry_ptr) { // process is already discovered, ignore message
            Services_entry *services = *((Services_entry **) entry_ptr);
#ifndef O2_NO_MQTT
            if (services) {  // discovery is also a keep-alive signal for MQTT
                O2node *proc = services->services[0].service;
                if (ISA_MQTT(proc)) {
                    ((MQTT_info *) proc)->timeout = o2_local_time() + 5;
                    return O2_SUCCESS;
                }
            }
#endif
            O2_DBd(printf("%s ** process already discovered, ignore %s\n",
                          o2_debug_prefix, name));
            return O2_SUCCESS;
        }
        // process is unknown, make a proc_info for it and start connecting...
        char ipdot[O2N_IP_LEN];
        o2_hex_to_dot(internal_ip, ipdot);
        proc = Proc_info::create_tcp_proc(O2TAG_PROC_TEMP,
                                          (const char *) ipdot, &tcp_port);
        O2_DBc(proc->co_info(proc->fds_info,
                         "created temp proc to connect to discovered proc"));
        // proc name is NULL

        if (compare > 0) { // we are the server, the other party should connect
            if (!proc) {
                return O2_FAIL;
            }
            // send /dy by TCP
            o2_prepare_to_deliver(o2_make_dy_msg(o2_ctx->proc, true, false,
                                                 O2_DY_CALLBACK));
            if (proc->send(false) != O2_SUCCESS) {
                delete proc; // error recovery: don't leak memory
            } else {
                // this connection will be closed by receiving client
                O2_DBd(printf("%s ** discovery sending O2_DY_CALLBACK to %s\n",
                              o2_debug_prefix, name));
            }
            return O2_SUCCESS;
        }

        // else we are the client
        proc->tag = O2TAG_PROC;
        assert(proc->key == NULL);  // make sure we don't leak memory
        proc->key = o2_heapify(name);
#ifndef O2_NO_HUB
        int dy_flag = (streql(name, o2_hub_addr) ? O2_DY_HUB : O2_DY_CONNECT);
#else
        int dy_flag = O2_DY_CONNECT;
#endif
        Services_entry::service_provider_new(name, NULL, proc, proc);
        O2_DBd(printf("%s ** discovery sending O2_DY_CONNECT to server %s\n",
                      o2_debug_prefix, name));
        reply_msg = o2_make_dy_msg(o2_ctx->proc, true, false, dy_flag);
    } else { // dy is not O2_DY_INFO, must be O2_DY_CONNECT
             //    or O2_DY_REPLY or O2_DY_HUB
        if (!o2_message_source || !ISA_PROC(o2_message_source)) {
            O2_DBG(printf("%s ** o2_discovered_a_remote_process_name dy %d "
                          "o2_message_source %p tag %s\n", o2_debug_prefix,
                          dy, o2_message_source, (o2_message_source ?
                          o2_tag_to_string(o2_message_source->tag) : "null")));
            return O2_FAIL;
        }
        proc = TO_PROC_INFO(o2_message_source);
        proc->key = o2_heapify(name);
        Services_entry::service_provider_new(proc->key, NULL, proc, proc);
        if (dy == O2_DY_HUB) { // this is the hub, this is the server side
            printf("######## This is the hub server side #######\n");
            // send a /dy to remote with O2_DY_REPLY
            O2_DBd(printf("%s ** discovery got HUB sending REPLY to hub %s\n",
                          o2_debug_prefix, name));
            reply_msg = o2_make_dy_msg(o2_ctx->proc, true, false, O2_DY_REPLY);
#ifndef O2_NO_HUB
        } else if (dy == O2_DY_REPLY) { // first message from hub
            if (!streql(name, o2_hub_addr)) { // should be equal
                printf("Warning: expected O2_DY_REPLY to be from hub\n");
                o2_message_source->fds_info->close_socket(true);
                return O2_FAIL;
            }
            printf("####### This is the hub client side #######\n");
            proc->uses_hub = O2_HUB_REMOTE;
            o2_send_start();
            reply_msg = o2_message_finish(0.0, "!_o2/hub", true);
            O2_DBd(printf("%s ** discovery got REPLY sending !_o2/hub %s\n",
                          o2_debug_prefix, name));
#endif
        } else if (dy == O2_DY_CONNECT) { 
            // similar to info, but sender has just made a tcp connection
            O2_DBG(printf("%s ** discovery got CONNECT from client %s, %s\n",
                           o2_debug_prefix, name, "connection complete"));
#ifndef O2_NO_HUB
            if (streql(name, o2_hub_addr)) {
                proc->uses_hub = O2_HUB_REMOTE;
                O2_DBd(printf("%s ** discovery got CONNECT from hub, %s %s\n",
                              o2_debug_prefix, "sending !_o2/hub to", name));
                o2_send_start();
                reply_msg = o2_message_finish(0.0, "!_o2/hub", true);
            }
#endif
        } else {
            O2_DBd(printf("Warning: unexpected dy type %d name %s\n",
                          dy, name));
            delete proc;
        }
    }
    O2err err = O2_SUCCESS;
    if (reply_msg) {
        o2_prepare_to_deliver(reply_msg);
        err = proc->send(false);
    }
    if (!err) err = o2_send_clocksync_proc(proc);
    if (!err) err = o2_send_services(proc);
    if (!err) err = proc->udp_address.init_hex(internal_ip, udp_port, false);
    O2_DBd(printf("%s UDP port %d for remote proc %s set to %d avail as %d\n",
                  o2_debug_prefix, udp_port, internal_ip,
                  ntohs(proc->udp_address.sa.sin_port),
                  proc->udp_address.get_port()));
    return err;
}


// send local services info to remote process. The address is !_o2/sv
// The parameters are this process name, e.g. @pip:iip:port (as a string),
// followed by (for each service): service_name, added_flag (true),
// service_or_tapper (true), properties, send_mode (0, ignored).
//
// Send taps as well: service_name, added_flag (true), service_or_tapper
// (false), tapper, send_mode (for tap messages).
//
// called by o2_discovery_handler in response to /_o2/dy
// the first service is the process itself, which contains important
// properties information
//
O2err o2_send_services(Proxy_info *proc)
{
    o2_send_start();
    assert(o2_ctx->proc->key);
    o2_add_string(o2_ctx->proc->key);
    O2string dest = proc->key;
    Enumerate enumerator(&o2_ctx->path_tree);
    O2node *entry;
    while ((entry = enumerator.next())) {
        Services_entry *services = TO_SERVICES_ENTRY(entry);
        for (int i = 0; i < services->services.size(); i++) {
            Service_provider *spp = &services->services[i];
            if (!ISA_PROC(spp->service)) {
                // must be local so it's a service to report unless it is
                // _o2 or the local process:
                if (entry->key[0] != '@' && !streql(entry->key, "_o2")) {
                    o2_add_string(entry->key);
                    o2_add_true();
                    o2_add_true();
                    o2_add_string(spp->properties ? spp->properties : ";");
                    o2_add_int32(0);  // send_mode is ignored for services
                    O2_DBd(printf("%s o2_send_services sending %s to %s\n",
                                  o2_debug_prefix, entry->key, dest));
                }
                // can only be one locally provided service, so stop searching
                break; 
            }
        }
        for (int i = 0; i < services->taps.size(); i++) {
            Service_tap *stp = &services->taps[i];
            o2_add_string(entry->key); // tappee
            o2_add_true();
            o2_add_false();
            o2_add_string(stp->tapper);
            o2_add_int32(stp->send_mode);
            O2_DBd(printf("%s o2_send_services sending tappee %s tapper %s "
                          "to %s\n",
                          o2_debug_prefix, entry->key, stp->tapper, dest));
        }
    }
    O2message_ptr msg = o2_message_finish(0.0, "!_o2/sv", true);
    if (!msg) return O2_FAIL;
    o2_prepare_to_deliver(msg);
    proc->send(false);
    return O2_SUCCESS;
}

#ifndef O2_NO_HUB
// send a discovery message to introduce every remote proc to new client
//
// find every connected proc, send a discovery message to each one
// Since we just connected to info, don't send to info to discover itself,
// and don't send from here since info already knows us (we are the hub).
//
// Also, to make things go faster, we send discovery message to the client
// side of the pair, so whichever name is lower gets the message.
//
static void hub_has_new_client(Proc_info *nc)
{
    for (int i = 0; i < o2n_fds_info.size(); i++) {
        Fds_info *info = o2n_fds_info[i];
        Proc_info *proc = (Proc_info *) (info->owner);
        if (ISA_PROC(proc)) {  // TODO: is this the right condition?
            Proc_info *client_info, *server_info;
            // figure out which is the client
            int compare = strcmp(proc->key, nc->key);
            if (compare > 0) { // proc is server
                client_info = nc;
                server_info = proc;
            } else if (compare < 0) {
                client_info = proc;
                server_info = nc;
            } else {      // if equal, tag should be PROC_TCP_SERVER
                continue; // so this should never be reached
            }
            O2message_ptr msg = o2_make_dy_msg(server_info, true, false,
                                               O2_DY_INFO);
            o2_prepare_to_deliver(msg);
            int err = client_info->send(false);
            if (err) {
                assert(o2_ctx->proc->key);
                printf("ERROR sending discovery message from hub:\n"
                        "    client %s server %s hub %s\n",
                        client_info->key, server_info->key,
                        o2_ctx->proc->key);
            }
            O2_DBd(printf("%s hub_has_new_client %s sent %s to %s\n",
                            o2_debug_prefix, o2_ctx->proc->key,
                            server_info->key, client_info->key));
        }
    }
}


// /_o2/hub handler: makes this the hub of the sender
//
void o2_hub_handler(o2_msg_data_ptr msg, const char *types,
                    O2arg_ptr *argv, int argc, const void *user_data)
{
    assert(o2_message_source);
    if (ISA_REMOTE_PROC(o2_message_source)) {
        o2_ctx->proc->uses_hub = O2_I_AM_HUB;
        printf("####### I am the hub #########\n");
        hub_has_new_client(TO_PROC_INFO(o2_message_source));
    }
}
#endif


// /_o2/sv handler: called when services become available or are removed.
// Arguments are
//     proc name,
//     service1, added_flag, service_or_tapper, properties_or_tapper, send_mode
//     service2, added_flag, service_or_tapper, properties_or_tapper, send_mode
//     ...
//
// Message was sent by o2_send_services()
// After this message is handled, this host is able to send/receive messages
//      to/from services
//
void o2_services_handler(o2_msg_data_ptr msg, const char *types,
                         O2arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(msg);
    O2arg_ptr arg = o2_get_next(O2_STRING);
    if (!arg) return;
    char *name = arg->s;
    // note that name is padded with zeros to 32-bit boundary
    Services_entry *services;
    Proxy_info *proc = (Proxy_info *)
                        Services_entry::service_find(name, &services);
    // proc might not really be a Proxy_info, but at least it is an O2node,
    // and we can check the tag:
    if (!proc || (!ISA_REMOTE_PROC(proc))) {
        O2_DBG(printf("%s ### ERROR: o2_services_handler did not find %s\n", 
                      o2_debug_prefix, name);
               o2_ctx->show_tree());
        
        return; // message is bogus (should we report this?)
    }
    O2arg_ptr addarg;     // boolean - adding a service or deleting one?
    O2arg_ptr isservicearg; // boolean - service (true) or tap (false)?
    O2arg_ptr prop_tap_arg;  // string - properties string or tapper name
    O2arg_ptr send_mode_arg;  // O2tap_send_mode - for taps
    while ((arg = o2_get_next(O2_STRING)) &&
           (addarg = o2_get_next(O2_BOOL)) &&
           (isservicearg = o2_get_next(O2_BOOL)) &&
           (prop_tap_arg = o2_get_next(O2_STRING)) &&
           (send_mode_arg = o2_get_next(O2_INT32))) {
        char *service = arg->s;
        char *prop_tap = prop_tap_arg->s;
        O2tap_send_mode send_mode = (O2tap_send_mode) send_mode_arg->i32;
        if (strchr(service, '/')) {
            O2_DBG(printf("%s ### ERROR: o2_services_handler got bad service "
                          "name - %s\n", o2_debug_prefix, service));
        } else if (addarg->B) { // add a new service or tap from remote proc
            O2_DBd(printf("%s found service /%s offered by /%s%s %s\n",
                          o2_debug_prefix, service, proc->key,
                          (isservicearg->B ? " tapper " : ""), prop_tap));
            if (isservicearg->B) {
                Services_entry::service_provider_new(service, prop_tap,
                                                     proc, proc);
            } else {
                o2_tap_new(service, proc, prop_tap, send_mode);
            }
        } else { // remove a service - it is no longer offered by proc
            if (isservicearg->B) {
                Services_entry::proc_service_remove(service, proc, NULL, -1);
            } else {
                o2_tap_remove(service, proc, prop_tap);
            }
        }
    }
}

#ifndef O2_NO_O2DISCOVERY
/*********** scheduling for discovery protocol ***********/

// o2_send_discovery_at() is called from o2.c to launch discovery
//      and used by the /_o2/ds handler (below) to reschedule itself
//
void o2_send_discovery_at(O2time when)
{
    // want to schedule another call to send again. Do not use send()
    // because we are operating off of local time, not synchronized
    // global time. Instead, form a message and schedule it:
    if (o2_send_start()) return;
    O2message_ptr ds_msg = o2_message_finish(when, "!_o2/ds", true);
    if (!ds_msg) return;
    o2_schedule_msg(&o2_ltsched, ds_msg);
}


/// callback function that implements sending discovery messages
//   this is the handler for /_o2/ds
//
//    message args are:
//    o2_ctx->info_ip (as a string), udp port (int), tcp port (int)
//
void o2_discovery_send_handler(o2_msg_data_ptr msg, const char *types,
                    O2arg_ptr *argv, int argc, const void *user_data)
{
#ifndef O2_NO_HUB
    if (o2_hub_addr[0]) {
        return; // end discovery broadcasts after o2_hub()
    }
#endif
    // O2 is not going to work if we did not get a discovery port
    if (disc_port_index < 0) return;
    next_disc_index = (next_disc_index + 1) % PORT_MAX;
    // initially send two tries to all ports (i.e. mod PORT_MAX).
    // After two tries to each port, only send up to our index
    // (the other side will do the same, so at least one will work)
    // except if o2lite is enabled, keep sending to all ports because
    // the o2lite client may depend on getting any port and may not
    // be sending/broadcasting any discovery messages.
    if (disc_msg_count >= 2 * PORT_MAX
#ifndef O2_NO_BRIDGES
        || !o2lite_protocol
#endif
        ) {
        next_disc_index = next_disc_index % (disc_port_index + 1);
    }
    int local_remote = o2_local_remote[next_disc_index];
    if (local_remote) {
        O2err err = o2_broadcast_message(o2_port_map[next_disc_index],
                                            local_remote);
        if (err == O2_SEND_FAIL) { // broadcast failed, so disable it
            o2_local_remote[next_disc_index] &= ~2;
        }
    }
    // If the estimated incoming message rate is >10/second,
    // increase the discovery period:
    int num_procs = (o2n_fds_info.size() - 2)
#ifndef O2_NO_MQTT
        + o2_mqtt_procs.size()
#endif
        ;
    double final_disc_period = num_procs * 0.1;
    if (final_disc_period < max_disc_period) {
        final_disc_period = max_disc_period;
    }
    // back off rate by 10% until we're sending every final_disc_period:
    disc_period *= RATE_DECAY;
    if (disc_period > final_disc_period) {
        disc_period = final_disc_period;
    }
    
    disc_msg_count++;
    O2time next_time = o2_local_time() + disc_period;
    o2_send_discovery_at(next_time);
}
#endif

#ifndef O2_NO_HUB
// o2_hub() - this should be like a discovery message handler that
//     just discovered a remote process, except we want to tell the
//     remote process that it is designated as our hub.
// public_ip and internal_ip are dot addresses, e.g. "127.0.0.1"
// or a domain name
//
O2err o2_hub(const char *public_ip, const char *internal_ip,
             int tcp_port, int udp_port)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    // end broadcasting: see o2_discovery.c
    if (!public_ip || !internal_ip) {
        strncpy(o2_hub_addr, "@", O2_MAX_PROCNAME_LEN);
        return O2_SUCCESS; // NULL address -> just disable broadcasting
    }
    if (hub_needs_public_ip) {
        return O2_FAIL;  // second call to o2_hub() before we know Public IP
        // we do not queue up pending discovery messages to hubs, so we fail
    }
    Net_address pub_address, int_address;
    RETURN_IF_ERROR(pub_address.init(public_ip, tcp_port, true));
    RETURN_IF_ERROR(int_address.init(internal_ip, tcp_port, true));
    char pip[O2N_IP_LEN];
    char iip[O2N_IP_LEN];
    snprintf(pip, O2N_IP_LEN, "%08x", ntohl(pub_address.sa.sin_addr.s_addr));
    snprintf(iip, O2N_IP_LEN, "%08x", ntohl(int_address.sa.sin_addr.s_addr));
    if (o2n_public_ip[0]) {
        snprintf(o2_hub_addr, O2_MAX_PROCNAME_LEN, "@%s:%s:%04x%c%c%c%c",
                 pip, iip, tcp_port, 0, 0, 0, 0);
        return o2_discovered_a_remote_process(pip, iip, tcp_port,
                                              udp_port, O2_DY_INFO);
    } else {
        o2_strcpy(hub_pip, pip, O2N_IP_LEN);
        o2_strcpy(hub_iip, iip, O2N_IP_LEN);
        hub_tcp_port = tcp_port;
        hub_udp_port = udp_port;
        hub_needs_public_ip = true;
    }
    return O2_SUCCESS;
}
#endif

