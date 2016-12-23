//
//  O2_discovery.c
//  O2
//
//  Created by 弛张 on 1/26/16.
//  Copyright © 2016 弛张. All rights reserved.
//

#include "o2.h"
#include "o2_dynamic.h"
#include "o2_socket.h"
#include "o2_search.h"
#include "o2_internal.h"
#include "o2_discovery.h"
#include "o2_send.h"
#include "o2_clock.h"

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

// From Wikipedia: The range 49152–65535 (215+214 to 216−1) contains
//   dynamic or private ports that cannot be registered with IANA.[198]
//   This range is used for private, or customized services or temporary
//   purposes and for automatic allocation of ephemeral ports.
// These ports were randomly generated from that range in two groups: 5
//   because PORT_MAX is 5 and that's how many you'll use. The next 11 are
//   also randomly generated and sorted. You'll get some of these if you
//   increase PORT_MAX because you need more processes on a single host
//   (or you need more ports because of conflicts with some other software.)
int o2_port_map[16] = { 53472, 54859, 55764, 60238, 62711,
                        49404, 50665, 51779, 53304, 56975, 57143,
                        57571, 61696, 63714, 64541, 64828 };
    /* previously, port list was: 
     * {  41110, 41729, 42381, 42897, 43666, 44521, 44849, 46778, 47996,
     *    48822, 49112, 49443, 49555, 50812, 51334, 52234  };
     */

int o2_discovery_init()
{
    int i;
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
    for (i = 0; i < PORT_MAX; i++) {
        broadcast_recv_port = o2_port_map[i];
        fds_info_ptr info;
        ret = make_udp_recv_socket(DISCOVER_SOCKET, &broadcast_recv_port, &info);
        if (ret == O2_SUCCESS) break;
    }
    if (i >= PORT_MAX) {
        broadcast_recv_port = -1; // no port to receive discovery messages
        printf("The ports are not enough."); // TODO: error message to stderr
        return ret;
    }

    //printf("%s: discovery receive port is %d\n", debug_prefix, broadcast_recv_port);
    
    // Set up a socket for sending discovery info locally
    if ((local_send_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Create local discovery send socket");
        return O2_FAIL;
    }
    O2_DB4(printf("local_send_sock (UDP) %d created\n", local_send_sock));

    // Initialize addr for local sending
    local_to_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, "127.0.0.1",
                  &(local_to_addr.sin_addr.s_addr)) != 1) {
        return O2_FAIL;
    }

    return O2_SUCCESS;
}


void o2_broadcast_message(int port, SOCKET sock, o2_message_ptr message)
{
    // set up the address and port
    broadcast_to_addr.sin_port = htons(port);
    
    // broadcast the message
    if (o2_found_network) {
        // printf("+    %s: sending broadcast message to %d\n", debug_prefix, port);
        if (sendto(broadcast_sock, (char *) &message->data, message->length, 0,
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
        // printf("+    %s: sending local discovery message to %d\n", debug_prefix, port);
        if (sendto(local_send_sock, (char *) &message->data, message->length, 0,
                   (struct sockaddr *) &local_to_addr,
                   sizeof(local_to_addr)) < 0) {
            perror("Error attempting to send discovery message locally");
        }
        // printf("%s: sent to local port %d\n", debug_prefix, port);
    }
}


int make_tcp_connection(char *ip, int tcp_port,
                        o2_socket_handler handler, fds_info_ptr *info)
{
    // We are the client because our ip:port string is lower
    struct sockaddr_in remote_addr;
    //set up the sockaddr_in
#ifndef WIN32
    bzero(&remote_addr, sizeof(remote_addr));
#endif
    // expand socket arrays for new port
    RETURN_IF_ERROR(make_tcp_recv_socket(TCP_SOCKET, handler, info));
        
    // set up the connection
    remote_addr.sin_family = AF_INET;      //AF_INET means using IPv4
    inet_pton(AF_INET, ip, &(remote_addr.sin_addr));
    remote_addr.sin_port = htons(tcp_port);

    // note: our local port number is not recorded, not needed
    // get the socket just created by make_tcp_recv_socket
    SOCKET sock = DA_LAST(o2_fds, struct pollfd)->fd;
    // printf("%s: connecting to %s:%d index %d\n", debug_prefix, ip, tcp_port, o2_fds.length - 1);
    if (connect(sock, (struct sockaddr *) &remote_addr,
                sizeof(remote_addr)) == -1) {
        perror("Connect Error!\n");
        o2_fds_info.length--;   // restore socket arrays 
        o2_fds.length--;
        return O2_FAIL;
    }
    (*info)->proc.status = PROCESS_CONNECTED;
    return O2_SUCCESS;
}


// Since discovery message is fixed, we'll cache it and reuse it:
o2_message_ptr o2_discovery_msg = NULL;

/// construct a discovery message for this process
int o2_discovery_msg_init()
{
    int err = o2_start_send() ||
        o2_add_string(IS_BIG_ENDIAN ? "b" : "l") ||
        o2_add_string(o2_application_name) ||
        o2_add_string(o2_local_ip) ||
        o2_add_int32(o2_local_tcp_port) ||
        o2_add_int32(broadcast_recv_port);
    o2_message_ptr msg;
    if (err || !(msg = o2_finish_message(0.0, "!_o2/dy")))
        return O2_FAIL;
    int size = MESSAGE_SIZE_FROM_ALLOCATED(msg->length);
    if (!((o2_discovery_msg = (o2_message_ptr) o2_malloc(size)))) {
        return O2_FAIL;
    }
    memcpy(o2_discovery_msg, msg, size);
    o2_free_message(msg);
    O2_DB(printf("O2: in o2_initialize,\n    name is %s, local IP is %s, \n"
            "    udp receive port is %d,\n"
            "    tcp connection port is %d,\n    broadcast recv port is %d\n",
            o2_application_name, o2_local_ip, o2_process->proc.udp_port,
            o2_local_tcp_port, broadcast_recv_port));
    return O2_SUCCESS;
}
    

/// callback function that implements sending discovery messages
//    message args are: "b" or "l" for big- or little-endian,
//    o2_process_ip (as a string), udp port (int), tcp port (int)
//
int o2_discovery_send_handler(o2_message_ptr msg, const char *types,
                              o2_arg_ptr *argv, int argc, void *user_data)
{
    next_discovery_index = (next_discovery_index + 1) % PORT_MAX;
    o2_broadcast_message(o2_port_map[next_discovery_index],
            DA_GET(o2_fds, struct pollfd, next_discovery_index)->fd,
                         o2_discovery_msg);
    //printf("Discovery broadcasts %p to port %d\n",
    //       outmsg, o2_port_map[next_discovery_index]);
    o2_time next_time = o2_local_time() + o2_discovery_send_interval;
    // back off rate by 10% until we're sending every 4s:
    o2_discovery_send_interval *= 1.1;
    // TODO: make 4.0 configurable
    if (o2_discovery_send_interval > 4.0) {
        o2_discovery_send_interval = 4.0;
    }
    // want to schedule another call to send again. Do not use send()
    // because we are operating off of local time, not synchronized
    // global time. Instead, form a message and schedule it:
    RETURN_IF_ERROR(o2_start_send());
    o2_message_ptr ds_msg = o2_finish_message(next_time, "!_o2/ds");
    if (!ds_msg) return O2_FAIL;
    o2_schedule(&o2_ltsched, ds_msg);
    // printf("o2_discovery_send_handler next time %g\n", next_time);
    return O2_SUCCESS;
}


int o2_send_init(fds_info_ptr process)
{
    assert(o2_process->proc.udp_port);
    // send initial message to newly connected process
    int err = o2_start_send() ||
        o2_add_string(IS_BIG_ENDIAN ? "b" : "l") ||
        o2_add_string(o2_local_ip) ||
        o2_add_int32(o2_local_tcp_port) ||
        o2_add_int32(o2_process->proc.udp_port) ||
        o2_add_int32(o2_clock_is_synchronized);
    if (err) return err;
    char address[32];
    snprintf(address, 32, "!%s/in", process->proc.name);
    o2_message_ptr msg = o2_finish_message(0.0, address);
    if (!msg) return O2_FAIL;
    return send_by_tcp_to_process(process, msg);
}


int o2_send_services(fds_info_ptr process)
{
    // send services if any
    if (o2_process->proc.services.length <= 0) {
        return O2_SUCCESS;
    }
    o2_start_send();
    o2_add_string(o2_process->proc.name);
    for (int i = 0; i < o2_process->proc.services.length; i++) {
        char *service = *DA_GET(o2_process->proc.services, char *, i);
        // ugly, but just a fast test if service is _o2:
        if ((*((int32_t *) service) != *((int32_t *) "_o2"))) {
            o2_add_string(service);
            // printf("%s: sending service %s to %s\n", debug_prefix, service, process->name);
        }
    }
    char address[32];
    snprintf(address, 32, "!%s/sv", process->proc.name);
    return o2_finish_send_cmd(0.0, address);
}


// /o2_/dy handler, parameters are: big/little endian, application name, ip, tcp, and upd
// 
int o2_discovery_handler(o2_message_ptr msg, const char *types,
                         o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_arg_ptr endian_arg, app_arg, ip_arg, tcp_arg, udp_arg;
    // get the arguments: endian, application name, ip as string,
    //                    tcp port, discovery port
    o2_start_extract(msg);
    if (!(endian_arg = o2_get_next('s')) ||
        !(app_arg = o2_get_next('s')) ||
        !(ip_arg = o2_get_next('s')) ||
        !(tcp_arg = o2_get_next('i')) ||
        !(udp_arg = o2_get_next('i'))) { // the discovery port
        return O2_FAIL;
    }
    char *ip = ip_arg->s;
    int is_little_endian = (endian_arg->s[0] == 'l');
    int tcp = tcp_arg->i32;
    int udp = udp_arg->i32;
    if (is_little_endian != IS_LITTLE_ENDIAN) {
        tcp = swap32(tcp);
        udp = swap32(udp);
    }
    
    if (!streql(app_arg->s, o2_application_name))
        return O2_FAIL;
    
    char name[32];
    // ip:port + pad with zeros
    snprintf(name, 32, "%s:%d%c%c%c%c", ip, tcp_arg->i32, 0, 0, 0, 0);
    int index;
    // printf("%s: o2_discovery_handler: lookup %s\n", debug_prefix, name);
    if (lookup(&path_tree_table, name, &index)) {
        // printf("%s: discovery handler: %s exists\n", debug_prefix, name);
        return O2_SUCCESS;
    }
    int compare = strcmp(o2_process->proc.name, name);
    // printf("%s: o2_discovery_handler name %s local name %s\n", debug_prefix, name, o2_process.name);
    if (compare == 0) { // the "discovered process" is this one
        return O2_SUCCESS;
    } else if (compare > 0) { // we are server, the other party should connect
        // send a discover message back to sender
        // sender's IP and port are known, so we can send a UDP message
        // directly.
        // printf("+    sending discovery msg to %s to encourage connection\n", debug_prefix);
        local_to_addr.sin_port = htons(udp);
        if (sendto(local_send_sock, (char *) &o2_discovery_msg->data,
                   o2_discovery_msg->length, 0, 
                   (struct sockaddr *) &local_to_addr, 
                   sizeof(local_to_addr)) < 0) {
            perror("Error attepting to send discovery message directly");
        }
    } else {
        fds_info_ptr info;
        RETURN_IF_ERROR(make_tcp_connection(ip, tcp, &o2_tcp_initial_handler, &info));
        RETURN_IF_ERROR(o2_init_process(info, name, PROCESS_CONNECTED,
                                        is_little_endian));
        O2_DB(printf("O2: discovered and connecting to %s\n", name));
    }
    return O2_SUCCESS;
}


// After a TCP connection is made, processes exchange information,
// If this is the client, then we have the IP:port info for the remote
// process. Use this to create an entry in path_tree_table. Otherwise,
// this is a server that only accepted a connection but has no info,
// so the path_tree_table has no entry yet.
//
int o2_discovery_init_handler(o2_message_ptr msg, const char *types,
                              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_arg_ptr endian_arg, ip_arg, tcp_arg, udp_arg, clocksync_arg;
    // get the arguments: endian, application name, ip as string,
    //                    tcp port, udp port
    if (o2_start_extract(msg) != 5 ||
        !(endian_arg = o2_get_next('s')) ||
        !(ip_arg = o2_get_next('s')) ||
        !(tcp_arg = o2_get_next('i')) ||
        !(udp_arg = o2_get_next('i')) ||
        !(clocksync_arg = o2_get_next('i'))) {
        printf("**** error in o2_tcp_initial_handler -- code incomplete ****\n");
        return O2_FAIL;
    }
    int is_little_endian = (endian_arg->s[0] == 'l');
    int tcp_port = tcp_arg->i32;
    int udp_port = udp_arg->i32;
    if (is_little_endian != IS_LITTLE_ENDIAN) {
        tcp_port = swap32(tcp_port);
        udp_port = swap32(udp_port);
    }
    char *ip = ip_arg->s;
    // get process name
    char name[32];
    // ip:port + pad with zeros
    snprintf(name, 32, "%s:%d%c%c%c%c", ip, tcp_port, 0, 0, 0, 0);
    // no byte-swap check needed for clocksync because it is just zero/non-zero
    int status = (clocksync_arg->i32 ? PROCESS_OK : PROCESS_NO_CLOCK);
    
    // if path_tree_table entry does not exist, create it
    int index;
    fds_info_ptr info;
    generic_entry_ptr *entry = lookup(&path_tree_table, name, &index);
    // printf("%s: in o2_discovery_init_handler, user_data %p, entry is %p\n", debug_prefix, user_data, entry);
    if (!entry) {
        info = (fds_info_ptr) user_data;
        RETURN_IF_ERROR(o2_init_process(info, name, status, is_little_endian));
    } else {
        remote_service_entry_ptr service = (remote_service_entry_ptr) *entry;
        info = DA_GET(o2_fds_info, fds_info, service->process_index);
        info->proc.status = status;
    }
    info->proc.udp_sa.sin_family = AF_INET;
    info->proc.udp_port = udp_port;
    assert(udp_port != 0);

#ifndef WIN32
    info->proc.udp_sa.sin_len = sizeof(info->proc.udp_sa);
#endif

    inet_pton(AF_INET, ip, &(info->proc.udp_sa.sin_addr.s_addr));
    info->proc.udp_sa.sin_port = htons(udp_port);
    // printf("%s: finished /in for %s, status %d, udp_port %d\n", debug_prefix, name, status, udp_port);
    O2_DB(printf("O2: connected from %s (udp port %ld)\n   to local socket %ld fds_info %p\n",
                 name, (long) udp_port,
                 (long) INFO_TO_FD(info), info));
    return O2_SUCCESS;
}


// /ip:port/sv: called to announce services available. Arguments are
//     process name, service1, service2, ...
//
int o2_services_handler(o2_message_ptr msg, const char *types,
                        o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(msg);
    o2_arg_ptr arg = o2_get_next('s');
    if (!arg) return O2_FAIL;
    char *name = arg->s;
    int i;
    // note that name is padded with zeros to 32-bit boundary
    generic_entry_ptr *entry = lookup(&path_tree_table, name, &i);
    if (entry) {
        assert((*entry)->tag == O2_REMOTE_SERVICE);
        remote_service_entry_ptr service = (remote_service_entry_ptr) *entry;
        int process_index = service->process_index;
        fds_info_ptr info = DA_GET(o2_fds_info, fds_info, process_index);
        // insert the services
        while ((arg = o2_get_next('s'))) {
            char *service_name = o2_heapify(arg->s);
            O2_DB(printf("O2: found service /%s offered by /%s\n", service_name, info->proc.name));
            add_remote_service(info, service_name);
        }
    }
    return O2_SUCCESS;
}
        
