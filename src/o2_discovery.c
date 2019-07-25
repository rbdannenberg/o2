//
//  O2_discovery.c
//  O2
//
//  Created by 弛张 on 1/26/16.
//  Copyright © 2016 弛张. All rights reserved.
//
// Protocol steps: see o2.c under "Discovery"
//

#include "o2_internal.h"
#include "o2_message.h"
#include "o2_send.h"
#include "o2_clock.h"
#include "o2_discovery.h"

#define O2_DY_INFO 50
#define O2_DY_HUB 51
#define O2_DY_REPLY 52
#define O2_DY_CALLBACK 53
#define O2_DY_CONNECT 54

static void hub_has_new_client(o2n_info_ptr nc);


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
    int ret;
    for (disc_port_index = 0; disc_port_index < PORT_MAX; disc_port_index++) {
        udp_recv_port = o2_port_map[disc_port_index];
        ret = o2n_udp_recv_socket_new(INFO_UDP_SOCKET, &udp_recv_port);
        if (ret == O2_SUCCESS) break;
    }
    if (disc_port_index >= PORT_MAX) {
        udp_recv_port = -1; // no port to receive discovery messages
        disc_port_index = -1;
        fprintf(stderr, "Unable to allocate a discovery port.\n");
        return ret;
    } else {
        // use the discovery message receive port as the general UDP receive port
        o2_context->info->proc.udp_port = udp_recv_port;
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
static o2_message_ptr make_o2_dy_msg(o2n_info_ptr info, int tcp_flag,
                                     int dy_flag)
{
    /*printf("make_o2_dy_msg: ensemble %s name %s\n",
           o2_ensemble_name, info->proc.name); */
    char buffer[32];
    char *ip;
    int port;
    assert(o2_found_network);
    if (info == o2_context->info) {
        ip = o2_local_ip;
        port = o2_local_tcp_port;
    } else {
        ip = buffer;
        if (extract_ip_port(info->proc.name, ip, &port))
            return NULL;
    }

    int err = o2_send_start() || o2_add_string(o2_ensemble_name) ||
        o2_add_string(ip) || o2_add_int32(port) ||
        o2_add_int32(info->proc.udp_port) || o2_add_int32(dy_flag);
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
 * @param port_s  the destination port number
 *
 * Receiver will get message and call o2_discovery_handler()
 */
static void o2_broadcast_message(int port)
{
    // set up the address and port
    o2n_broadcast_to_addr.sin_port = htons(port);
    o2_message_ptr m = make_o2_dy_msg(o2_context->info, FALSE, O2_DY_INFO);
    if (!m) {
        return;
    }
    o2_msg_data_ptr msg = &m->data;
    int len = m->length;
    
    // broadcast the message
    if (o2_found_network) {
        O2_DBd(printf("%s broadcasting discovery msg to port %d\n",
                      o2_debug_prefix, port));
        if (sendto(o2n_broadcast_sock, (char *) msg, len, 0,
                   (struct sockaddr *) &o2n_broadcast_to_addr,
                   sizeof(o2n_broadcast_to_addr)) < 0) {
            perror("Error attempting to broadcast discovery message");
        }
    }
    // assume that broadcast messages are not received on the local machine
    // so we have to send separately to localhost using the same port;
    // since we own udp_recv_port, there is no need to send in that case
    if (port != udp_recv_port) {
        o2n_local_udp_send((char *) msg, len, o2n_broadcast_to_addr.sin_port);
    }
    o2_message_free(m);
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
// 1. a /dy message is received via broadcast
// 2. user calls o2_hub() to name another process
// 3. /dy message is received via tcp
//
int o2_discovered_a_remote_process(const char *ip, int tcp, int udp, int dy)
{
    o2n_info_ptr remote = o2_message_source;
    if (dy == O2_DY_CALLBACK) { // similar to info, but close connection first
        int i = remote->fds_index;  // we are going to be the client
        o2_socket_remove(i);
        dy = O2_DY_INFO; 
    }

    char name[32];
    // ip:port + pad with zeros
    snprintf(name, 32, "%s:%d%c%c%c%c", ip, tcp, 0, 0, 0, 0);

    if (dy == O2_DY_INFO) {
        int compare = strcmp(o2_context->info->proc.name, name);
        if (compare == 0) {
            O2_DBd(printf("   Ignored: I received my own broadcast message\n"));
            return O2_SUCCESS; // the "discovered process" is this one
        }
        // process is already discovered, ignore message
        o2_node_ptr *entry_ptr = o2_lookup(&o2_context->path_tree, name);
        if (*entry_ptr) {
            return O2_SUCCESS;
        }
        // process is unknown, start connecting...
        if (compare > 0) { // we are server, the other party should connect
            RETURN_IF_ERROR(o2n_connect(ip, tcp, INFO_TCP_NOCLOCK));
            o2n_info_ptr remote = *DA_LAST(o2_context->fds_info, o2n_info_ptr);
            // send /dy by TCP
            o2_send_by_tcp(remote, FALSE,
                    make_o2_dy_msg(o2_context->info, TRUE, O2_DY_CALLBACK));
            // this connection will be closed by receiving client
            O2_DBd(printf("%s ** discovery sending CALLBACK to %s\n",
                          o2_debug_prefix, name));
        } else { // we are the client
            RETURN_IF_ERROR(o2n_connect(ip, tcp, INFO_TCP_NOCLOCK));
            o2n_info_ptr remote = *DA_LAST(o2_context->fds_info, o2n_info_ptr);
            remote->proc.name = o2_heapify(name);
            remote->proc.udp_port = udp;
            o2_service_provider_new(name, NULL, (o2_node_ptr) remote, remote);
            O2_DBg(printf("%s ** discovery sending CONNECT to server %s\n",
                          o2_debug_prefix, name));
            o2_send_by_tcp(remote, FALSE,
                    make_o2_dy_msg(o2_context->info, TRUE, O2_DY_CONNECT));
            o2_send_clocksync(remote);
            o2_send_services(remote);
        }
    } else if (dy == O2_DY_HUB) {
        remote->proc.name = o2_heapify(name);
        remote->proc.udp_port = udp;
        o2_service_provider_new(name, NULL, (o2_node_ptr) remote, remote);
        O2_DBd(printf("%s ** discovery got HUB sending REPLY to hub %s\n",
                      o2_debug_prefix, name));
        o2_send_by_tcp(remote, FALSE,
                make_o2_dy_msg(o2_context->info, TRUE, O2_DY_REPLY));
        o2_send_clocksync(remote);
        o2_send_services(remote);
    } else if (dy == O2_DY_REPLY) { // first message from hub
        remote->proc.name = o2_heapify(name);
        remote->proc.udp_port = udp;
        o2_service_provider_new(name, NULL, (o2_node_ptr) remote, remote);
        if (streql(name, o2_context->hub)) { // should be true
            remote->proc.uses_hub = O2_HUB_REMOTE;
            o2_send_start();
            o2_message_ptr msg = o2_message_finish(0.0, "!_o2/hub", TRUE);
            if (!msg) return O2_FAIL;
            O2_DBd(printf("%s ** discovery got REPLY sending !_o2/hub %s\n",
                          o2_debug_prefix, name));
            o2_send_by_tcp(remote, FALSE, msg);
        } else {
            printf("Warning: expected O2_DY_REPLY to be from hub\n");
        }
    } else if (dy == O2_DY_CONNECT) { // similar to info, but close connection
        remote->proc.name = o2_heapify(name);
        remote->proc.udp_port = udp;
        o2_service_provider_new(name, NULL, (o2_node_ptr) remote, remote);
        o2_send_clocksync(remote);
        o2_send_services(remote);
        O2_DBg(printf("%s ** discovery got CONNECT from client %s, %s\n",
                       o2_debug_prefix, name, "connection complete"));
        if (streql(name, o2_context->hub)) {
            remote->proc.uses_hub = O2_HUB_REMOTE;
            O2_DBd(printf("%s ** discovery got CONNECT from hub, %s %s\n",
                          o2_debug_prefix, "sending !_o2/hub to", name));
            o2_send_start();
            o2_message_ptr msg = o2_message_finish(0.0, "!_o2/hub", TRUE);
            if (!msg) return O2_FAIL;
            o2_send_by_tcp(remote, FALSE, msg);
        }
    }        
    return O2_SUCCESS;
}

/*
// Send to !_o2/in.
// called by o2_discovery_handler in response to /_o2/dy
//
int o2_send_initialize(o2n_info_ptr process, int32_t hub_flag)
{
    assert(o2_context->info->port);
    assert(o2_found_network);
    // send initial message to newly connected process.
    int err = o2_send_start() ||
        o2_add_int32(hub_flag) ||
        o2_add_string(o2_ensemble_name) ||
        o2_add_string(o2_local_ip) ||
        o2_add_int32(o2_local_tcp_port) ||
        o2_add_int32(o2_context->info->port) ||
        o2_add_int32(o2_clock_is_synchronized);
    if (err) return err;
    // This will be expected as first TCP message and directly
    // delivered by the o2_tcp_initial_handler() callback
    o2_message_ptr msg = o2_message_finish(0.0, "!_o2/in", TRUE);
    if (!msg) return O2_FAIL;
    o2_send_by_tcp(process, msg);
    return O2_SUCCESS;
}
*/

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
int o2_send_services(o2n_info_ptr process)
{
    o2_send_start();
    o2_add_string(o2_context->info->proc.name);
    dyn_array_ptr services = &(o2_context->info->proc.services);
    o2string dest = process->proc.name;
    
    for (int i = 0; i < services->length; i++) {
        proc_service_data_ptr psdp = 
                DA_GET(*services, proc_service_data, i);
        services_entry_ptr ss = psdp->services;
        // ugly, but just a fast test if service is _o2:
        if (*((int32_t *) (ss->key)) != *((int32_t *) "_o2")) {
            o2_add_string(ss->key);
            o2_add_true();
            o2_add_true();
            o2_add_string(psdp->properties ? psdp->properties + 1 : "");
            O2_DBd(printf("%s o2_send_services sending %s to %s\n",
                          o2_debug_prefix, ss->key, dest));
        }   
    }

    for (int i = 0; i < process->proc.taps.length; i++) {
        proc_tap_data_ptr ptdp = DA_GET(process->proc.taps, proc_tap_data, i);
        services_entry_ptr ss = ptdp->services;
        o2_add_string(ss->key); // tappee
        o2_add_true();
        o2_add_false();
        o2_add_string(ptdp->tapper);
        O2_DBd(printf("%s o2_send_services sending tappee %s tapper %s to %s\n",
                      o2_debug_prefix, ss->key, ptdp->tapper, dest));
    }

    o2_message_ptr msg = o2_message_finish(0.0, "!_o2/sv", TRUE);
    if (!msg) return O2_FAIL;
    o2_send_by_tcp(process, FALSE, msg);
    return O2_SUCCESS;
}

#ifdef OLDCODE

// send discovery message to inform process about all known hosts
// this gets called as a result of a call to o2_hub(), and messages
// are sent via TCP
//
int o2_send_discovery(o2n_info_ptr process)
{
    // now send info on every host
    for (int i = 0; i < o2_context->fds_info.length; i++) {
        o2n_info_ptr info = GET_PROCESS(i);
        // parse ip & port from info. If we do not have a info.name, this
        // proc may be the result of a client running o2_hub() and making
        // a TCP connection solely for the purpose of sending a discovery
        // message. This tells us, the receiver, to make a TCP connection
        // in the other direction, so the "real" TCP socket pair is not
        // represented by this proc, and this connection will soon be
        // closed by the connected process. (All this is explaining why
        // we have to check for info->proc.name in the next line...)
        if ((info->net_tag == TCP_CLIENT || info->net_tag == TCP_CONNECTION) &&
            info->proc.name) {
            char ipaddress[32];
            strcpy(ipaddress, info->proc.name);
            char *colon = strchr(ipaddress, ':');
            if (!colon) {
                return O2_FAIL;
            }
            *colon = 0; // isolate the ipaddress from ip:port
            int port = atoi(colon + 1);

            // if this fails, we'll continue with other hosts
            o2_message_ptr msg = make_o2_dy_msg(o2_context->info, 
                                                O2_FROM_HUB, TRUE);
            err = o2_send_by_tcp(process, FALSE, msg);
            if (err) {
                return err;
            }
            O2_DBd(printf("%s o2_send_discovery sent %s,%d to %s\n",
                          o2_debug_prefix, ipaddress, port, process->proc.name));
        }            
    }
    return O2_SUCCESS;
}


// send discovery message via tcp as the result of a call to o2_hub()
// ipaddress:port is the remote process TCP address and ID
// be_server means this local process is the server (remote process must
//    initiate the connection)
// hub_flag means the remote process is a hub for the local process
//
int o2_discovery_by_tcp(const char *ipaddress, int port, char *name,
                        int be_server, int32_t hub_flag)
{
    int err = O2_SUCCESS;
    o2n_info_ptr remote;
    RETURN_IF_ERROR(o2_make_tcp_connection(ipaddress, port, &remote, O2_NO_HUB));
    if (be_server) { // we are server; connect to deliver a /dy message
        o2_message_ptr msg = make_o2_dy_msg(o2_context->info, 
                                 hub_flag ? O2_CLIENT_IS_HUB : O2_NO_HUB, TRUE); // TCP
        if (!msg) {
            return O2_FAIL;
        }
        O2_DBh(printf("%s in o2_discovery_by_tcp, we are server sending /dy to %s:%d\n",
                      o2_debug_prefix, ipaddress, port));
        err = o2_send_by_tcp(remote, FALSE, msg);
    } else { // we are the client remote host is the server and hub
        remote->proc.name = o2_heapify(name);
        o2_service_provider_new(name, NULL, (o2n_info_ptr) remote, remote);
        //TODO obsolete: o2_send_initialize(remote, hub_flag ? O2_SERVER_IS_HUB : O2_NO_HUB);
        o2_send_services(remote);
    }
    return err;
}



// Handler for !_o2/in messages:
// After a TCP connection is made, processes exchange information,
// arriving info is used to create a process description which is stored
// in the tcp port info of the connected tcp port. 
//
// Message was sent by o2_send_initialize()
// Message will be followed by services (/_o2/sv)
//
// This handler creates the service for the remote process and
// adds the name of the process to the info struct UNLESS the
// service was previously created, probably when this process
// received a discovery message and initiated the connection.
//
// The client always sends /in to the server first, and the server
// replies with its own /in message.
//
// Cases where we are receiveing /in message:
//
// 1. this is the result of calling o2_hub(), we (receiver) are the 
//    hub and TCP server. hub_flag = O2_BE_MY_HUB, announce sender
//    to other processes through discovery messages.
// 2. this is a reply from connecting to hub. sender is the hub and
//    we are the TCP client. hub_flag = O2_I_AM_HUB
// 3. sender is the hub who is the TCP client. hub_flag = O2_I_AM_HUB,
// 4. receiver is the hub who is the TCP client. hub_flag = O2_NO_HUB
// 5. non-hub client connects to non-hub server. hub_flag = O2_NO_HUB
// 6. non-hub server receives from non-hub client. hub_flag = O2_NO_HUB
//
void o2_discovery_init_handler(o2_msg_data_ptr msg, const char *types,
                               o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_arg_ptr hub_arg, ens_arg, ip_arg, tcp_arg, udp_arg, clocksync_arg;
    // get the arguments: ensemble name, ip as string,
    //                    tcp port, udp port
    if (o2_extract_start(msg) != 6 ||
        !(hub_arg = o2_get_next('i')) ||
        !(ens_arg = o2_get_next('s')) ||
        !(ip_arg = o2_get_next('s')) ||
        !(tcp_arg = o2_get_next('i')) ||
        !(udp_arg = o2_get_next('i')) ||
        !(clocksync_arg = o2_get_next('i'))) {
        printf("** error in o2_discovery_init_handler - bad msg **\n");
        return;
    }
    int tcp_port = tcp_arg->i32;
    char *ensemble = ens_arg->s;
    int hub_flag = hub_arg->i32;
    int udp_port = udp_arg->i32;
    if (udp_port == 0) return;
    char *ip = ip_arg->s;

    // ignore the message and disconnect if the ensemble name does not match
    if (!streql(ensemble, o2_ensemble_name)) {
        if (o2_message_source->tag != INFO_UDP_SOCKET) {
            o2_info_remove(o2_message_source);
        }
        return;
    }

    // get process name
    char name[32];
    // ip:port + pad with zeros
    snprintf(name, 32, "%s:%d%c%c%c%c", ip, tcp_port, 0, 0, 0, 0);

    // if o2_context->path_tree entry does not exist, create it
    o2n_info_ptr info = o2_message_source;
    // now info is the TCP info for the process that send the /in message
    assert(info->net_tag == NET_TCP_CONNECTION || info->net_tag == NET_TCP_CLIENT);
    if (info->tag != INFO_TCP_NOMSGYET) {
        printf("Unexpected /in message for %s ignored\n", name);
        return;
    }
    o2_node_ptr *entry_ptr = o2_lookup(&o2_context->path_tree, name);
    O2_DBd(printf("%s o2_discovery_init_handler looked up %s -> %p\n",
                  o2_debug_prefix, name, entry_ptr));

    // sanity check
    if (*entry_ptr) { // if name is a service, info must provide the service
        // (info describes the TCP connection to the process with that name)
        assert((*entry_ptr)->tag == NODE_SERVICES);
        services_entry_ptr services = (services_entry_ptr) entry_ptr;
        assert(o2_proc_service_find(info, services));
        // we are the client
        assert(info->net_tag == NET_TCP_CLIENT);
    } else { // we are the server & we accepted a connection,
        // but we did not yet create a service named for client's IP:port
        assert(o2_service_provider_new(name, NULL, (o2_node_ptr) info, info) ==
               O2_SUCCESS);
        assert(info->proc.name == NULL);
        info->proc.name = o2_heapify(name);
        // no byte-swap check needed for clocksync because it is boolean
        info->tag = (clocksync_arg->i32 ? INFO_TCP_SOCKET : INFO_TCP_NOCLOCK);

        info->proc.uses_hub = (hub_flag == O2_BE_MY_HUB ? O2_I_AM_HUB :
                               (hub_flag == O2_I_AM_HUB ? O2_HUB_REMOTE :
                                O2_NO_HUB));
        o2_send_initialize(info, info->proc.uses_hub);
        o2_send_services(info);
        if (hub_flag == O2_BE_MY_HUB) {
            hub_has_new_client(info);
        }
    }
    info->proc.udp_sa.sin_family = AF_INET;
    assert(info != o2_context->info);
    info->port = udp_port;
#ifdef __APPLE__
    info->proc.udp_sa.sin_len = sizeof(info->proc.udp_sa);
#endif

    inet_pton(AF_INET, ip, &(info->proc.udp_sa.sin_addr.s_addr));
    info->proc.udp_sa.sin_port = htons(udp_port);
}
#endif

// send a discovery message to introduce every remote process to new client
//
// find every connected process, send a discovery message to each one
// Since we just connected to info, don't send to info to discover itself,
// and don't send from here since info already knows us (we are the hub).
//
// Also, to make things go faster, we send discovery message to the client
// side of the pair, so whichever name is lower gets the message.
//
static void hub_has_new_client(o2n_info_ptr nc)
{
    for (int i = 0; i < o2_context->fds_info.length; i++) {
        o2n_info_ptr info = GET_PROCESS(i);
        switch (info->tag) {
            case INFO_TCP_NOCLOCK:
            case INFO_TCP_SOCKET: {
                o2n_info_ptr client_info, server_info;
                // figure out which is the client
                int compare = strcmp(info->proc.name, nc->proc.name);
                if (compare > 0) { // info is server
                    client_info = nc;
                    server_info = info;
                } else if (compare < 0) {
                    client_info = info;
                    server_info = nc;
                } else {      // if equal, tag should be INFO_TCP_SERVER
                    continue; // so this should never be reached
                }
                o2_message_ptr msg = make_o2_dy_msg(server_info, TRUE, FALSE);
                int err = o2_send_by_tcp(client_info, FALSE, msg);
                if (err) {
                    printf("ERROR sending discovery message from hub:\n"
                            "    client %s server %s hub %s\n",
                            client_info->proc.name, server_info->proc.name,
                            o2_context->info->proc.name);
                }
                O2_DBd(printf("%s hub_has_new_client %s sent %s to %s\n",
                                o2_debug_prefix, o2_context->info->proc.name,
                                server_info->proc.name, client_info->proc.name));
                break;
            }
            case INFO_TCP_SERVER:          // this represents the local process, ignore
            case INFO_UDP_SOCKET:          // not a process representation
            case INFO_OSC_UDP_SERVER:      // osc sockets are not O2 processes
            case INFO_OSC_TCP_SERVER:
            case INFO_OSC_TCP_CONNECTION:
            case INFO_OSC_TCP_CONNECTING:
            case INFO_OSC_TCP_CLIENT:
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
    assert(o2_message_source);
    if (TAG_IS_REMOTE(o2_message_source->tag)) {
        o2_context->info->proc.uses_hub = O2_I_AM_HUB;
        hub_has_new_client(o2_message_source);
    }
}



// /_o2/sv handler: called when services become available or are removed. Arguments are
//     process name, service1, added_flag, service_or_tapper, 
//     properties_or_tapper, service2, added_flag, service_or_tapper, 
//     properties_or_tapper, ...
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
    o2n_info_ptr proc = (o2n_info_ptr) o2_service_find(name, &services);
    if (!proc || proc->tag != INFO_TCP_SOCKET) {
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
                          o2_debug_prefix, service, proc->proc.name,
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
    if (o2_context->hub[0]) {
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

