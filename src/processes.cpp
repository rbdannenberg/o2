/* processes.c - info on all discovered processes */

/* Roger B. Dannenberg
 * April 2020
 */

#include "o2internal.h"
#include "services.h"
#include "msgsend.h"
#include "o2osc.h"
#include "discovery.h"


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
    o2_processes_initialize() calls
        create_tcp_proc(PROC_TCP_SERVER, ip, port) which
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
               create_tcp_proc(PROC_NOCLOCK, ip, port)
                   which creates proc and calls o2n_connect(ip, port, proc)
       Life cycle up to destruction is:
       tag                   net_tag               notes
       PROC (NO SYNC)    NET_TCP_CONNECTING    waiting for connection
       PROC (NO SYNC)    NET_TCP_CLIENT        waiting for init msg, 
                                                   proc->key is NULL
       PROC (NO SYNC)    NET_TCP_CLIENT        no clock sync yet, proc_name set
       PROC (SYNCED)     NET_TCP_CLIENT        connected and synchronized
    2. If we accept a connection request from the server port
       Creation of NET_TCP_CONNECTION starts in
           read_event_handler()
               socket_info_new(connection, NET_TCP_CONNECTION);
                   *o2n_accept_callout == o2_net_accepted()
                       creates a proc with tag PROC_NOCLOCK
       Life cycle up to destruction is:
       tag                   net_tag               notes
       PROC (NO SYNC)    NET_TCP_CONNECTION    waiting for init msg,
                                                   proc->key is NULL
       PROC (NO SYNC)    NET_TCP_CONNECTION    no clock sync yet, proc_name set
       PROC (SYNCED)     NET_TCP_CONNECTION    connected and synchronized
    3. If we discover remote process, but we are server, make a temporary
       TCP connection and send a dy message. (This is more overhead than UDP,
       but if UDP gets dropped, it seems simpler to let TCP retry the 
       connection attempts and faster than waiting for another round of O2
       discovery messages, which are relatively infrequent.)
       tag                   net_tag               notes
       PROC_TEMP         NET_TCP_CONNECTING    waiting for connection
       PROC_TEMP         NET_TCP_CLIENT        connected, send dy msg,
                                                   wait for remote to close

Destruction of PROC_TCP or PROC_TCP_TEMP proc
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
                    services or taps (and it will for the
                    @public:internal:port service), remove the services_entry
                frees proc->key
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
        create_tcp_proc(PROC_TEMP, ip, tcp) and 
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
Proc_info::~Proc_info()
{
    O2_DBc(printf("%s delete Proc_info tag %s name %s\n",
            o2_debug_prefix, Fds_info::tag_to_string(tag), key));
    O2_DBo(o2_fds_info_debug_predelete(fds_info));
    // remove the remote services provided by the proc
    // circularity is taken care of by removing each service,
    // services in turn remove the back pointer in proc->services
    if (ISA_REMOTE_PROC(this)) { // not for PROC_TEMP or PROC_TCP_SERVER
        Services_entry::remove_services_by(this);
        Services_entry::remove_taps_by(this);
    } else {
        O2_DBo(printf("%s: freeing local proc_info tag %s name %s\n",
                      o2_debug_prefix, o2_tag_to_string(tag),
                      key));
    }
    delete_fds_info();
}


O2err Proc_info::send(bool block) {
    O2err rslt;
    int tcp_flag;
    O2message_ptr msg = pre_send(&tcp_flag);
    if (!msg) {
        rslt = O2_NO_SERVICE;
    } else if (tcp_flag) {
        rslt = fds_info->send_tcp(block, (o2n_message_ptr) msg);
    } else {  // send via UDP
        rslt = o2n_send_udp(&udp_address, (o2n_message_ptr) msg);
        if (rslt != O2_SUCCESS) {
            O2_DBn(printf("Proxy_info::send error, port %d\n",
                          udp_address.get_port()));
        }
    }
    o2_message_source = NULL;  // clean up to help debugging
    return rslt;
}


#ifndef O2_NO_DEBUG
void o2_show_sockets()
{
    printf("----- sockets -----\n");
    for (int i = 0; i < o2n_fds_info.size(); i++) {
        Fds_info *info = o2n_fds_info[i];
        Proxy_info *proc = (Proxy_info *) info->owner;
        if (proc) {
            printf("    %s (%d) net_tag %x (%s) socket %d info %p "
                   "owner %p (%s%s)\n",
                   o2_debug_prefix, i, info->net_tag,
                   o2_tag_to_string(info->net_tag),
                   info->get_socket(), info,
                   proc, o2_tag_to_string(proc->tag),
                   (proc == o2_ctx->proc ? ", local proc" : ""));
        } else {
            printf("    %s (%d) net_tag %x (%s) socket %d info %p "
                   "owner NULL\n",
                   o2_debug_prefix, i, info->net_tag,
                   o2_tag_to_string(info->net_tag),
                   info->get_socket(), info);
        }
    }
}
#endif


// callback indicates that an accept() has completed
//
O2err Proc_info::accepted(Fds_info *conn)
{
    // accept can only be from OSC_TCP_SERVER or PROC_TCP_SERVER:
    assert(ISA_PROC_TCP_SERVER(this));
    // create a proc_info for the connection
    conn->owner = new Proc_info();
    conn->owner->fds_info = conn;
    // port and udp_sa are zero'd initially
    return O2_SUCCESS;
}


// a connect() call completed, we are now connected
O2err Proc_info::connected()
{
    return O2_SUCCESS;
}


#ifndef O2_NO_DEBUG
void Proc_info::show(int indent)
{
    O2node::show(indent);
    printf(" port=%d name=%s\n", udp_address.get_port(), key);
}
#endif


// create a proc. For local proc, tag is PROC_TCP_SERVER,
// to connect to a remote proc, tag is PROC_NOCLOCK,
// tag can also be PROC_TEMP, OSC_TCP_SERVER or OSC_TCP_CLIENT
// For tag PROC_NOCLOCK, ip is domain name, localhost, or dot format
Proc_info *Proc_info::create_tcp_proc(int tag, const char *ip, int port)
{
    // create proc_info to pass to network layer
    Proc_info *proc = new Proc_info();
    proc->tag = tag;
    if (tag == O2TAG_PROC_TCP_SERVER) {
        proc->fds_info = Fds_info::create_tcp_server(port);
    } else if (tag == O2TAG_PROC || tag == O2TAG_PROC_TEMP) {
        proc->fds_info = Fds_info::create_tcp_client(ip, port);
    } else {
        assert(false);
    }
    if (!proc->fds_info) { // failure, remove proc
        delete proc;
        return NULL;
    }
    proc->fds_info->owner = proc;
    return proc;
}


// - initialize network module
// - create UDP broadcast socket
// - create UDP send socket
// - create UDP recv socket
// - create TCP server socket
// assumes o2n_initialize() was called
void o2_processes_initialize()
{
    int port = o2_discovery_udp_server->port;
    assert(o2_ctx->proc);
    char name[O2_MAX_PROCNAME_LEN];
    snprintf(name, O2_MAX_PROCNAME_LEN,
             "@%s:%s:%x", o2n_public_ip, o2n_internal_ip, port);
    o2_ctx->proc->key = o2_heapify(name);
    O2_DBG(printf("%s Local Process Name is %s\n",
                  o2_debug_prefix, o2_ctx->proc->key));
    o2_discovery_udp_server->owner = o2_ctx->proc;
    o2_ctx->proc->udp_address.set_port(o2_discovery_udp_server->port);
}


