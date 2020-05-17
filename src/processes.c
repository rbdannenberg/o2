/* processes.c - info on all discovered processes */

/* Roger B. Dannenberg
 * April 2020
 */

#include "o2internal.h"
#include "services.h"
#include "msgsend.h"
#include "o2osc.h"


/*
There are some special sockets that 
UDP Broadcast Socket (just a socket for UDP, there is no o2n_info structure)
UDP Send Socket (another socket for UDP, there is no o2n_info structure)
UDP Receive Socket (net_tag = NET_UDP_SERVER)
    Socket is created initially and only closed by o2n_finish, used for both
    discovery messages and incoming O2 UDP messages. No proc_info for this.

Here are all the types of proc_info structures and their life-cycles:

Local Process (tag = PROC_TCP_SERVER, net_tag = PROC_TCP_SERVER)
----------------------------------------------------------------
Socket is created initially during initialization:
    o2_process_initialize() calls
        o2_create_tcp_proc(PROC_TCP_SERVER, ip, port) which
            creates a proc and calls
            o2n_tcp_server_new(port, proc) calls
                o2n_tcp_socket_new(NET_TCP_SERVER, port, application)
Destruction of NET_TCP_SERVER is by
    o2_finish() calls
        o2n_close_socket(info), then
        o2n_free_deleted_sockets() works as for remote processes 
            (see below)


Remote process (net_tag = NET_TCP_CLIENT or NET_TCP_CONNECTION or 
                          NET_TCP_CONNECTING)
----------------------------------------------------------------
    1. Upon discovery, if we are the client, issue a connect request.
       Creation of NET_TCP_CONNECTING is by
           o2_discovered_a_remote_process(ip, tcp port, ...) calls
               o2_create_tcp_proc(PROC_NOCLOCK, ip, port)
                   which creates proc and calls o2n_connect(ip, port, proc)
       Life cycle up to destruction is:
       tag                   net_tag               notes
       PROC_NOCLOCK      NET_TCP_CONNECTING    waiting for connection
       PROC_NOCLOCK      NET_TCP_CLIENT        waiting for init msg, 
                                                   proc->name is NULL
       PROC_NOCLOCK      NET_TCP_CLIENT        no clock sync yet, proc_name set
       PROC_SYNCED       NET_TCP_CLIENT        connected and synchronized
    2. If we accept a connection request from the server port
       Creation of NET_TCP_CONNECTION starts in
           read_event_handler()
               socket_info_new(connection, NET_TCP_CONNECTION);
                   *o2n_accept_callout == o2_net_accepted()
                       creates a proc with tag PROC_NOCLOCK
       Life cycle up to destruction is:
       tag                   net_tag               notes
       PROC_NOCLOCK      NET_TCP_CONNECTION    waiting for init msg,
                                                   proc->name is NULL
       PROC_NOCLOCK      NET_TCP_CONNECTION    no clock sync yet, proc_name set
       PROC_SYNCED       NET_TCP_CONNECTION    connected and synchronized
    3. If we discover remote process, but we are server, make a temporary
       TCP connection and send a dy message. (This is more overhead than UDP,
       but if UDP gets dropped, it seems simpler to let TCP retry the 
       connection attempts and faster than waiting for another round of O2
       discovery messages, which are relatively infrequent.)
       tag                   net_tag               notes
       PROC_TEMP         NET_TCP_CONNECTING    waiting for connection
       PROC_TEMP         NET_TCP_CLIENT        connected, send dy msg,
                                                   wait for remote to close

Destruction of PROC_TCP_SYNCED or PROC_TCP_NOCLOCK or PROC_TCP_TEMP proc
when connection is closed:
    o2n_recv() calls
        read_event_handler() returns O2_TCP_HUP and then
        o2n_close_socket(info) closes socket and sets info->delete_me; later
    o2n_free_deleted_sockets() calls
        o2n_socket_remove(info) calls
            *o2n_close_callout == o2_net_info_remove() calls
                o2_remove_services_by(proc)
                o2_remove_taps_by(proc)
                    if a services_entry becomes empty either by removing
                    services or taps (and it will for the ip:port service), 
                    remove the services_entry
                frees proc->name
                frees proc itself
            closes socket
            removes the entry in o2_fds and o2_fds_info arrays
            frees info


Temp "Process" tag = PROC_TEMP
----------------------------------------------------------------
Temp "process" is used for making a temporary connection to
reliably deliver a discovery message when a remote proc should
connect to the local proc. 

    o2_discovered_a_remote_process() calls
        o2_create_tcp_proc(PROC_TEMP, ip, tcp) and 
        sends O2_DY_CALLBACK

Destruction of PROC_TEMP
    when receiver receives O2_DY_CALLBACK it closes the socket
    this causes a TCP_HUP back at the sender.
    Destruction proceeds as described above.

 */

// always called to free a proc_info_ptr, if this proc is the
// local TCP server, freeing the proc_info does not free the
// services. Services are only freed when this is a remote proc
// because the services entries point to this proc_info and
// would become dangling pointers if we don't remove them.
// We also remove any taps for proc, because they also would
// become dangling pointers.
void o2_proc_info_free(proc_info_ptr proc)
{
    // remove the remote services provided by the proc
    // circularity is taken care of by removing each service,
    // services in turn remove the back pointer in proc->services
    if (PROC_IS_REMOTE(proc)) { // not for PROC_TEMP or PROC_TCP_SERVER
        o2_remove_services_by(proc);
        o2_remove_taps_by(proc);
    }
    if (proc->name) { // name is NULL if this is PROC_TEMP
        O2_FREE(proc->name);
    }
    O2_FREE(proc);

}


const char *o2_node_to_ipport(o2_node_ptr node)
{
    return (ISA_PROC(node) ? TO_PROC_INFO(node)->name :
                             o2_context->proc->name);
}


int o2_net_info_remove(o2n_info_ptr info)
{
    proc_info_ptr proc = (proc_info_ptr) info->application; // could also be osc_info
    printf("o2_net_info_remove info tag %d name %s\n", info->net_tag,
           ISA_PROC(proc) ? proc->name : TO_OSC_INFO(proc)->service_name);
    if (proc) {
        if (ISA_PROC(proc)) {
            o2_proc_info_free(proc);
        } else if (ISA_OSC(proc)) {
            o2_osc_info_free(TO_OSC_INFO(proc));
        } else {
            printf("Unexpected call to o2_net_info_remove. net_tag %d %s\n",
                   info->net_tag, o2n_tag_to_string(info->net_tag));
        }
    }
    return O2_SUCCESS;
}


// callback indicates that a new connection has been made
//
int o2_net_accepted(o2n_info_ptr info, o2n_info_ptr conn)
{
    proc_info_ptr server = (proc_info_ptr) (info->application);
    // figure out if server is OSC or O2
    if (server->tag == OSC_TCP_SERVER) {
        return o2_osc_accepted(TO_OSC_INFO(server), conn);
    }
    // accept can only be from OSC_TCP_SERVER or PROC_TCP_SERVER:
    assert(server->tag == PROC_TCP_SERVER);
    // create a proc_info for the connection
    proc_info_ptr proc = (proc_info_ptr) O2_CALLOC(1, sizeof(proc_info));
    proc->tag = PROC_NOCLOCK;
    proc->net_info = conn;
    conn->application = (void *) proc;
    proc->name = NULL; // connected process will send their name
    proc->uses_hub = O2_NO_HUB; // initially default to no
    // port and udp_sa are zero'd initially
    return O2_SUCCESS;
}


int o2_net_connected(o2n_info_ptr info)
{
    proc_info_ptr proc = (proc_info_ptr) (info->application);
    // figure out if server is OSC or O2
    if (proc->tag == OSC_TCP_SERVER) {
        return o2_osc_connected(TO_OSC_INFO(proc));
    }
    assert(proc->name); // if we called connect, then we should have
    // assigned proc->name to "IPaddress:port"
    return o2_send_cmd("!_o2/si", 0.0, "sis", proc->name,
                       O2_REMOTE_NOTIME, proc->name);
}


// helper function for o2_status() and finding status.
// 
int o2_status_from_proc(o2_node_ptr service, const char **process)
{
    if (!service) return O2_FAIL;
    switch (service->tag) {
        case PROC_NOCLOCK:
        case PROC_SYNCED: {
            proc_info_ptr proc = TO_PROC_INFO(service);
            o2n_info_ptr info = proc->net_info;
            if (info->net_tag == NET_TCP_CONNECTING) {
                if (process) *process = NULL;
                return O2_FAIL;
            }
            if (process) {
                *process = proc->name;
            }
            return (o2_clock_is_synchronized && service->tag == PROC_SYNCED ?
                    O2_REMOTE : O2_REMOTE_NOTIME);
        }
        case NODE_HASH:
        case NODE_HANDLER:
            if (process)
                *process = o2_context->proc->name;
            return (o2_clock_is_synchronized ? O2_LOCAL : O2_LOCAL_NOTIME);
        case OSC_TCP_CLIENT: // no timestamp synchronization with OSC
        case OSC_UDP_CLIENT:
            if (process)
                *process = o2_context->proc->name;
            return o2_clock_is_synchronized? O2_TO_OSC : O2_TO_OSC_NOTIME;
        case NODE_BRIDGE_SERVICE:
        default: // not implemented yet or it's a NODE_TAP or not connected
            if (process)
                *process = NULL;
            return O2_FAIL;
    }
}


#ifndef O2_NO_DEBUG
void o2_proc_info_show(proc_info_ptr proc)
{
    printf(" port=%d name=%s\n", proc->udp_address.port, proc->name);
}
#endif


// create a proc. For local proc, tag is PROC_TCP_SERVER,
// to connect to a remote proc, tag is PROC_NOCLOCK,
// tag can also be OSC_TCP_SERVER or OSC_TCP_CLIENT
proc_info_ptr o2_create_tcp_proc(int tag, const char *ip, int port)
{
    // create proc_info to pass to network layer
    proc_info_ptr proc = (proc_info_ptr) O2_CALLOC(1, sizeof(proc_info));
    proc->tag = tag;
    if (tag == PROC_TCP_SERVER) {
        proc->net_info = o2n_tcp_server_new(port, (void *) proc);
    } else if (tag == PROC_NOCLOCK) {
        proc->net_info = o2n_connect(ip, port, (void *) proc);
    } else {
        assert(FALSE);
    }
    if (!proc->net_info) { // failure, remove proc
        O2_FREE(proc);
        proc = NULL;
    }
    return proc;
}


// - initialize network module
// - create UDP broadcast socket
// - create UDP send socket
// - create UDP recv socket
// - create TCP server socket
// assumes o2n_initialize() was called
int o2_processes_initialize()
{
    o2_context->proc = o2_create_tcp_proc(PROC_TCP_SERVER, NULL, 0);
    if (!o2_context->proc) {
        o2n_finish();
        return O2_FAIL;
    }
    // note that there might not be a network connection here. We can
    // still use O2 locally without an IP address.
    o2_context->proc->name =
            o2_heapify(o2n_get_local_process_name(o2_context->proc->net_info->port));
    return O2_SUCCESS;
}
