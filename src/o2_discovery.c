//
//  O2_discovery.c
//  O2
//
//  Created by 弛张 on 1/26/16.
//  Copyright © 2016 弛张. All rights reserved.
//

#include "o2_internal.h"
#include "o2_message.h"
#include "o2_send.h"
#include "o2_clock.h"
#include "o2_discovery.h"

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
int next_discovery_index = 0; // which port to send to (0 - 4)
struct sockaddr_in broadcast_to_addr; // address for sending broadcast messages
struct sockaddr_in local_to_addr; // address for sending local discovery msgs
SOCKET broadcast_sock = INVALID_SOCKET;
int broadcast_recv_port = -1; // port we grabbed
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

int o2_discovery_initialize()
{
#ifdef WIN32
    //Initialize (in Windows)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif // WIN32

    // Set up a socket for broadcasting discovery info
    if ((broadcast_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Create broadcast socket");
        return O2_FAIL;
    }
    O2_DBo(printf("%s broadcast socket %ld created\n",
                  o2_debug_prefix, (long) broadcast_sock));
    
    // Set the socket's option to broadcast
    int optval = TRUE;
    if (setsockopt(broadcast_sock, SOL_SOCKET, SO_BROADCAST,
                   (const char *) &optval, sizeof(int)) == -1) {
        perror("Set socket to broadcast");
        return O2_FAIL;
    }

    // Initialize addr for broadcasting
    broadcast_to_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, "255.255.255.255",
                  &(broadcast_to_addr.sin_addr.s_addr)) != 1)
        return O2_FAIL;

    // Create socket to receive broadcasts
    // Try to find an available port number from the discover port map.
    // If there are no available port number, print the error and return O2_FAIL.
    int ret;
    for (disc_port_index = 0; disc_port_index < PORT_MAX; disc_port_index++) {
        broadcast_recv_port = o2_port_map[disc_port_index];
        process_info_ptr info;
        ret = o2_make_udp_recv_socket(DISCOVER_SOCKET, &broadcast_recv_port, &info);
        if (ret == O2_SUCCESS) break;
    }
    if (disc_port_index >= PORT_MAX) {
        broadcast_recv_port = -1; // no port to receive discovery messages
        disc_port_index = -1;
        fprintf(stderr, "Unable to allocate a discovery port.");
        return ret;
    }
    O2_DBo(printf("%s created discovery port %ld\n",
                  o2_debug_prefix, (long) broadcast_recv_port));

    // Set up a socket for sending discovery info locally
    if ((local_send_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Create local discovery send socket");
        return O2_FAIL;
    }
    O2_DBo(printf("%s discovery send socket (UDP) %lld created\n",
                  o2_debug_prefix, (long long) local_send_sock));

    // Initialize addr for local sending
    local_to_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, "127.0.0.1",
                  &(local_to_addr.sin_addr.s_addr)) != 1) {
        return O2_FAIL;
    }

    return O2_SUCCESS;
}

// we are the "client" connecting to a remote process acting as the "server"
static int make_tcp_connection(char *ip, int tcp_port,
                               o2_socket_handler handler, process_info_ptr *info)
{
    // We are the client because our ip:port string is lower
    struct sockaddr_in remote_addr;
    //set up the sockaddr_in
#ifndef WIN32
    bzero(&remote_addr, sizeof(remote_addr));
#endif
    // expand socket arrays for new port
    RETURN_IF_ERROR(o2_make_tcp_recv_socket(TCP_SOCKET, 0, handler, info));
    o2_process_initialize(*info, PROCESS_CONNECTED);    
    // set up the connection
    remote_addr.sin_family = AF_INET;      //AF_INET means using IPv4
    inet_pton(AF_INET, ip, &(remote_addr.sin_addr));
    remote_addr.sin_port = htons(tcp_port);

    // note: our local port number is not recorded, not needed
    // get the socket just created by o2_make_tcp_recv_socket
    SOCKET sock = DA_LAST(o2_fds, struct pollfd)->fd;

    O2_DBo(printf("%s connect to %s:%d with socket %ld\n",
                  o2_debug_prefix, ip, tcp_port, (long) sock));
    if (connect(sock, (struct sockaddr *) &remote_addr,
                sizeof(remote_addr)) == -1) {
        perror("Connect Error!\n");
        o2_fds_info.length--;   // restore socket arrays 
        o2_fds.length--;
        return O2_FAIL;
    }
    o2_disable_sigpipe(sock);
    O2_DBd(printf("%s connected to %s:%d index %d\n",
                  o2_debug_prefix, ip, tcp_port, o2_fds.length - 1));
    return O2_SUCCESS;
}


// Since discovery message is fixed, we'll cache it and reuse it.
// the o2_discovery_msg is in network byte order
o2_message_ptr o2_discovery_msg = NULL;

/// construct a discovery message for this process
int o2_discovery_msg_initialize()
{
    int err = o2_send_start() ||
        o2_add_string(o2_application_name) ||
        o2_add_string(o2_local_ip) ||
        o2_add_int32(o2_local_tcp_port) ||
        o2_add_int32(broadcast_recv_port);
    o2_message_ptr msg;
    if (err || !(msg = o2_message_finish(0.0, "!_o2/dy", FALSE)))
        return O2_FAIL;
    int size = MESSAGE_SIZE_FROM_ALLOCATED(msg->length);
    if (!((o2_discovery_msg = (o2_message_ptr) o2_malloc(size)))) {
        return O2_FAIL;
    }
    O2_DBd(printf("%s broadcast discovery message created:\n    ", o2_debug_prefix);
           o2_message_print(msg);
           printf("\n"));
#if IS_LITTLE_ENDIAN
    o2_msg_swap_endian(&msg->data, TRUE);
#endif
    memcpy(o2_discovery_msg, msg, size);
    o2_message_free(msg);
    O2_DBg(printf("%s in o2_initialize,\n    name is %s, local IP is %s, \n"
            "    udp receive port is %d,\n"
            "    tcp connection port is %d,\n    broadcast recv port is %d\n",
            o2_debug_prefix, o2_application_name, o2_local_ip, o2_process->port,
            o2_local_tcp_port, broadcast_recv_port));
    return O2_SUCCESS;
}
    

int o2_discovery_finish()
{
    // sockets are all freed elsewhere
    O2_FREE(o2_discovery_msg);
    return O2_SUCCESS;
}


/**
 *  Broadcast o2_discovery_msg to a discovery port.
 *
 *  @param port_s  the destination port number
 */
static void o2_broadcast_message(int port)
{
    // set up the address and port
    broadcast_to_addr.sin_port = htons(port);
    o2_msg_data_ptr msg = &o2_discovery_msg->data;
    int len = o2_discovery_msg->length;
    
    // broadcast the message
    if (o2_found_network) {
        O2_DBd(printf("%s broadcasting discovery msg to port %d\n",
                      o2_debug_prefix, port));
        if (sendto(broadcast_sock, (char *) msg, len, 0,
                   (struct sockaddr *) &broadcast_to_addr,
                   sizeof(broadcast_to_addr)) < 0) {
            perror("Error attempting to broadcast discovery message");
        }
    }
    // assume that broadcast messages are not received on the local machine
    // so we have to send separately to localhost using the same port;
    // since we own broadcast_recv_port, there is no need to send in that case
    if (port != broadcast_recv_port) {
        local_to_addr.sin_port = broadcast_to_addr.sin_port; // copy the port number
        O2_DBd(printf("%s sending localhost discovery msg to port %d\n",
                      o2_debug_prefix, port));
        if (sendto(local_send_sock, (char *) msg, len, 0,
                   (struct sockaddr *) &local_to_addr,
                   sizeof(local_to_addr)) < 0) {
            perror("Error attempting to send discovery message locally");
        }
    }
}


/// callback function that implements sending discovery messages
//    message args are:
//    o2_process_ip (as a string), udp port (int), tcp port (int)
//
void o2_discovery_send_handler(o2_msg_data_ptr msg, const char *types,
                               o2_arg_ptr *argv, int argc, void *user_data)
{
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
    // want to schedule another call to send again. Do not use send()
    // because we are operating off of local time, not synchronized
    // global time. Instead, form a message and schedule it:
    if (o2_send_start()) return;
    o2_message_ptr ds_msg = o2_message_finish(next_time, "!_o2/ds", TRUE);
    // printf("ds_msg %p\n", ds_msg);
    if (!ds_msg) return;
    o2_schedule(&o2_ltsched, ds_msg);
}


int o2_send_initialize(process_info_ptr process)
{
    assert(o2_process->port);
    // send initial message to newly connected process
    int err = o2_send_start() ||
        o2_add_string(o2_local_ip) ||
        o2_add_int32(o2_local_tcp_port) ||
        o2_add_int32(o2_process->port) ||
        o2_add_int32(o2_clock_is_synchronized);
    if (err) return err;
    // This will be expected as first TCP message and directly
    // delivered by the o2_tcp_initial_handler() callback
    o2_message_ptr msg = o2_message_finish(0.0, "!_o2/in", TRUE);
    if (!msg) return O2_FAIL;
    err = send_by_tcp_to_process(process, &msg->data);
    o2_message_free(msg);
    return err;
}


int o2_send_services(process_info_ptr process)
{
    // send services if any
    if (o2_process->proc.services.length <= 0) {
        return O2_SUCCESS;
    }
    o2_send_start();
    o2_add_string(o2_process->proc.name);
    for (int i = 0; i < o2_process->proc.services.length; i++) {
        char *service = *DA_GET(o2_process->proc.services, char *, i);
        // ugly, but just a fast test if service is _o2:
        if ((*((int32_t *) service) != *((int32_t *) "_o2"))) {
            o2_add_string(service);
            o2_add_true();
            O2_DBd(printf("%s o2_send_services sending %s to %s\n",
                          o2_debug_prefix, service, process->proc.name));
        }
    }
    char address[32];
    snprintf(address, 32, "!%s/sv", process->proc.name);
    return o2_send_finish(0.0, address, TRUE);
}


// /_o2/dy handler, parameters are: application name, ip, tcp, and upd
// 
void o2_discovery_handler(o2_msg_data_ptr msg, const char *types,
                          o2_arg_ptr *argv, int argc, void *user_data)
{
    O2_DBd(o2_dbg_msg("o2_discovery_handler gets", msg, NULL, NULL));
    o2_arg_ptr app_arg, ip_arg, tcp_arg, udp_arg;
    // get the arguments: application name, ip as string,
    //                    tcp port, discovery port
    o2_extract_start(msg);
    if (!(app_arg = o2_get_next('s')) ||
        !(ip_arg = o2_get_next('s')) ||
        !(tcp_arg = o2_get_next('i')) ||
        !(udp_arg = o2_get_next('i'))) { // the discovery port
        return;
    }
    char *ip = ip_arg->s;
    int tcp = tcp_arg->i32;
    
    if (!streql(app_arg->s, o2_application_name)) {
        O2_DBd(printf("    Ignored: application name is not %s\n", o2_application_name));
        return;
    }
    char name[32];
    // ip:port + pad with zeros
    snprintf(name, 32, "%s:%d%c%c%c%c", ip, tcp, 0, 0, 0, 0);
    int compare = strcmp(o2_process->proc.name, name);
    if (compare == 0) {
        O2_DBd(printf("    Ignored: I received my own broadcast message\n"));
        return; // the "discovered process" is this one
    }
    o2_entry_ptr *entry_ptr = o2_lookup(&o2_path_tree, name);
    // if process is connected, ignore it
    if (*entry_ptr) {
#ifndef NDEBUG
        process_info_ptr remote = NULL;
        services_entry_ptr services = (services_entry_ptr) *entry_ptr;
        assert(services && services->tag == SERVICES && services->services.length == 1);
        remote = (process_info_ptr) GET_SERVICE(services->services, 0);
        assert(remote && remote->tag == TCP_SOCKET && remote->fds_index != -1);
        O2_DBd(printf("    Ignored: already connected\n"));
#endif
        return; // we've already connected or accepted, so ignore the /dy data
    }
    if (compare > 0) { // we are server, the other party should connect
        // send a discover message back to sender's UDP port, which is now known
        struct sockaddr_in udp_sa;
        udp_sa.sin_family = AF_INET;
#ifdef __APPLE__
        udp_sa.sin_len = sizeof(udp_sa);
#endif
        inet_pton(AF_INET, ip, &(udp_sa.sin_addr.s_addr));
        udp_sa.sin_port = htons(udp_arg->i32);
        if (sendto(local_send_sock, (char *) &o2_discovery_msg->data,
                   o2_discovery_msg->length, 0,
                   (struct sockaddr *) &udp_sa,
                   sizeof(udp_sa)) < 0) {
            perror("Error attepting to send discovery message directly");
        }
        O2_DBd(printf("%s o2_discovery_handler to become server for %s\n",
                      o2_debug_prefix, name));
    } else { // we are the client
        process_info_ptr remote;
        O2_DBg(printf("%s ** Discovered and connecting to %s\n",
                      o2_debug_prefix, name));
        if (make_tcp_connection(ip, tcp, &o2_tcp_initial_handler, &remote)) {
            return;
        }
        remote->proc.name = o2_heapify(name);
        assert(remote->tag == TCP_SOCKET);
        o2_service_provider_new(name, (o2_info_ptr) remote, remote);
        o2_send_initialize(remote);
        o2_send_services(remote);
    }
}


// After a TCP connection is made, processes exchange information,
// arriving info is used to create a process description which is stored
// in the tcp port info of the connected tcp port
//
void o2_discovery_init_handler(o2_msg_data_ptr msg, const char *types,
                               o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_arg_ptr ip_arg, tcp_arg, udp_arg, clocksync_arg;
    // get the arguments: application name, ip as string,
    //                    tcp port, udp port
    if (o2_extract_start(msg) != 4 ||
        !(ip_arg = o2_get_next('s')) ||
        !(tcp_arg = o2_get_next('i')) ||
        !(udp_arg = o2_get_next('i')) ||
        !(clocksync_arg = o2_get_next('i'))) {
        printf("**** error in o2_tcp_initial_handler -- code incomplete ****\n");
        return;
    }
    int tcp_port = tcp_arg->i32;
    int udp_port = udp_arg->i32;
    if (udp_port == 0) return;
    char *ip = ip_arg->s;
    // get process name
    char name[32];
    // ip:port + pad with zeros
    snprintf(name, 32, "%s:%d%c%c%c%c", ip, tcp_port, 0, 0, 0, 0);
    // no byte-swap check needed for clocksync because it is just zero/non-zero
    int status = (clocksync_arg->i32 ? PROCESS_OK : PROCESS_NO_CLOCK);
    
    // if o2_path_tree entry does not exist, create it
    process_info_ptr info = (process_info_ptr) user_data;
    assert(info->proc.status == PROCESS_CONNECTED);
    o2_entry_ptr *entry_ptr = o2_lookup(&o2_path_tree, name);
    if (*entry_ptr) { // we are the client, and we connected after receiving a
        // /dy message, also created a service named for server's IP:port
        if (o2_process_initialize(info, status)) return;
    } else { // we are the server, and we accepted a client connection,
        // but we did not yet create a service named for client's IP:port
        assert(info->tag == TCP_SOCKET);
        o2_service_provider_new(name, (o2_info_ptr) info, info);
        assert(info->proc.name == NULL);
        info->proc.name = o2_heapify(name);
        // now that we have a name and service, we can send init message back:
        o2_send_initialize(info);
        o2_send_services(info);
    }
    info->proc.status = status;
    info->proc.udp_sa.sin_family = AF_INET;
    assert(info != o2_process);
    info->port = udp_port;

#ifdef __APPLE__
    info->proc.udp_sa.sin_len = sizeof(info->proc.udp_sa);
#endif

    inet_pton(AF_INET, ip, &(info->proc.udp_sa.sin_addr.s_addr));
    info->proc.udp_sa.sin_port = htons(udp_port);

    O2_DBd(printf("%s init msg from %s (udp port %ld)\n   to local socket %ld process_info %p\n",
                  o2_debug_prefix, name, (long) udp_port, (long) (info->fds_index), info));
    return;
}


// /ip:port/sv: called to announce services available or removed. Arguments are
//     process name, service1, added_flag, service2, added_flag, ...
//
void o2_services_handler(o2_msg_data_ptr msg, const char *types,
                         o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_extract_start(msg);
    o2_arg_ptr arg = o2_get_next('s');
    if (!arg) return;
    char *name = arg->s;
    // note that name is padded with zeros to 32-bit boundary
    process_info_ptr proc = (process_info_ptr) o2_service_find(name);
    if (!proc || proc->tag != TCP_SOCKET) {
        O2_DBg(printf("%s ### ERROR: o2_services_handler did not find %s\n", o2_debug_prefix, name));
        return; // message is bogus (should we report this?)
    }
    o2_arg_ptr addarg;
    while ((arg = o2_get_next('s')) && (addarg = o2_get_next('B'))) {
        if (strchr(arg->s, '/')) {
            O2_DBg(printf("%s ### ERROR: o2_services_handler got bad service name - %s\n", o2_debug_prefix, arg->s));
        } else if (addarg->B) {
            O2_DBd(printf("%s found service /%s offered by /%s\n",
                          o2_debug_prefix, arg->s, proc->proc.name));
            o2_service_provider_new(arg->s, (o2_info_ptr) proc, proc);
        } else {
            o2_service_provider_replace(proc, arg->s, NULL);
        }
    }
}
