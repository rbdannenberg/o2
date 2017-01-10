//
//  O2.c
//  O2
//
//  Created by 弛张 on 1/26/16.
//  Copyright © 2016 弛张. All rights reserved.
//

/*
Design Notes
============
                         service
o2_fds   o2_fds_info      names
+----+   +---------+      ______
|    |   |        -+---->|______|
|----|   |---------|     |______|
|    |   |         |       
|----|   |---------|
|    |   |         |
+----+   +---------+

path_tree_table
+----------+             +--------+   +--------+
|         -+------------>| entry -+-->| handler|
+----------+  +-------+  | node   |   |  node  |
|         -+->| entry |  |        |   +--------+
+----------+  | node  |  |        |   +--------+
              +-------+  |       -+-->| entry -+-> etc
                         +--------+   |  node -+-> etc
                                      +--------+
master_table
+----------+             +--------+
|         -+------------>| handler|
+----------+  +--------+ |  node  |
|         -+->| handler| +--------+
+----------+  | node   |
              +--------+



Each application has:
path_tree_table - a dictionary mapping service names to services
    (handler_entry, node_entry, remote_service_entry, osc_entry). 
    Local services are represented by a handler_entry if there is
    a single handler for all messages to the service, or a
    node_entry which is the root dictionary for a tree of path 
    nodes. (A node is a slash-delimited element of an address, 
    e.g. /synth/lfo/freq has nodes synth, lfo and freq). Each node
    is represented by either a node_entry for internal nodes or
    a handler_entry for leaf nodes. The path_tree_table itself is
    a node_entry.  The path_tree_table also maps IP addresses + 
    ports (as strings that begin with a digit and have the form 
    128.2.100.120:4000) to a remote_service_entry.

master_table is a dictionary for full paths, permitting a single hash 
    table lookup for addresses of the form !synth/lfo/freq. In practice
    an additional lookup of just the service name is required to 
    determine if the service is local and if there is a single handler
    for all messages to that service.

Each handler object is referenced by some node in the path_tree_table
    and by the master_table dictionary, except for handlers that handle
    all service messages. These are only referenced by the 
    path_tree_table.

o2_fds is a dynamic array of sockets for poll
o2_fds_info is a parallel array of additional information
    includes a tag: UDP_SOCKET is for all incoming UDP messages
        TCP_SOCKET makes a TCP connection to a remote process
        DISCOVER_SOCKET is a UDP socket for incoming discovery messages
        TCP_SERVER_SOCKET is the server socket, representing local process
        OSC_SOCKET is for incoming OSC messages via UDP
        OSC_TCP_SERVER_SOCKET is a server socket for OSC TCP connections
        OSC_TCP_SOCKET is for incoming OSC messages via TCP
        OSC_TCP_CLIENT is for outgoing OSC messages via TCP

    If the tag is TCP_SOCKET or TCP_SERVER_SOCKET, fields are:
        name - the ip address:port number used as a service name
        status - PROCESS_DISCOVERED through PROCESS_OK
        services - an dynamic array of local service names
        udp_sa - a sockaddr_in structure for sending udp to this process
    If the tag is OSC_SOCKET or OSC_TCP_SERVER_SOCKET or OSC_TCP_SOCKET, 
        osc_service_name - name of the service to forward to


Sockets
-------

o2_fds_info has state to receive messages. When a message is received,
there is a handler function that is called to process the message.
    For outgoing O2 messages, we have an associated process to tell
where to send.
    For incoming O2 messages, no extra info is needed; just deliver 
the message.
    For incoming OSC messages, fds_info has the process with the
        osc_service_name where messages are redirected.

Discovery
---------

Discovery messages are sent to discovery ports. Discovery ports
come from a list of unassigned port numbers. Every process opens
the first port on the list that is available as a receive port. 
The list is the same for every process, so each processes knows
what ports to send to. The only question is how many of up to 16
discovery ports do we need to send discovery messages to in order
to reach everyone? The answer is that if we receive on the Nth 
port in the list, we transmit to ports 1 through N. For any pair
of processes that are receiving on ports M and N, respectively,
assume, without loss of generality, that M > N. Since the first
process sends to ports 1 through M, it will send to port N and 
discovery will happen. The second process will not send to port
M, but the discovery protocol only relies on discovery happening
in one direction. Once a discovery message is received, in either
direction, a TCP connection is established for two-way communication.

The address for discovery messages is !_o2/dy, and the arguments are:
    application name (string)
    local ip (string)
    tcp (int32)
    discovery port (int32)
When a discovery is made, a TCP connection is made.

When a TCP connection is connected or accepted, the process sends the
UDP port number and a list of services to !_o2/sv. 

Process Creation
----------------

o2_discovery_handler() receives !_o2/dy message. There are two cases
based on whether the local host is the server or client. The server is
the host with the greater IP:Port string. The client connects to the
server.

Info for each process is stored in fds_info, which has one entry per
socket. Most sockets are TCP sockets and the associated process info
in fds_info represents a remote process. However, the info associated
with the TCP server port (every process has one of these) represents
the local process and is created when O2 is initialized. There are a
few other sockets: discovery socket, UDP send socket, UDP receive
socket, and OSC sockets (if created by the user).

The message sequence is:
Client broadcasts /dy (discovery) to all, including server. This
    triggers the sending of the next message, but the next message
    is also sent periodically. Either way, the next message is sent
    either to the discovery port or to the UDP port.
Server sends /dy (discovery) to client's discovery or UDP port.
Client receives /dy and replies with:
    Client connect()'s to server, creating TCP socket.
    The server status is set to PROCESS_CONNECTED
    Client sends /in (initialization) to server using the TCP socket.
    Client sends /sv (services) to server using the TCP socket.
    Locally, the client creates a service named "IP:port" representing
        the server so that if another /dy message arrives, the client
        will not make another connection.
Server accepts connect request, creating TCP socket.
    The TCP socket is labeled with status PROCESS_CONNECTED.
    Server sends /in (initialization) to client using the TCP socket.
    Locally, the server creates an O2 service named "IP:port"
        representing the client so that if another /dy message
        arrives, the server will not make another connection.
    Server sends /sv (services) to client using the TCP socket.
The /in message updates the status of the remote process to
    PROCESS_NO_CLOCK or PROCESS_OK. The latter is obtained only when
    both processes have clock sync.

Services and Processes
----------------------
A process includes a list of services (strings). Each 
service is mapped by path_tree_table to a 
remote_service_entry, which has an integer index of the
corresponding remote process (both the socket in the fds 
array and the process info in the fds_info array).

When sockets are closed, the last socket in fds is moved
to fill the void and fds is shortened. The corresponding
fds_info element has to be copied as well, which means
that remote_service_entry's now index the wrong location.
However, each process has an array of service names, so 
we look up each service name, find the remote_service_entry,
and update the index to fds (and fds_info).

The list of service names is also used to remove services
when the socket connecting to the process is closed.

Remote Process Name: Allocation, References, Freeing
----------------------------------------------------
Each remote process has a name, e.g. ""128.2.1.100:55765"
that can be used as a service name in an O2 address, e.g.
to announce a new local service to that remote process.
The name is on the heap and is "owned" by the fds_info
record associated with the TCP_SOCKET socket for remote
processes. Also, the local process has its name owned by
the TCP_SERVER socket.

The process name is *copied* and used as the key for a
remote_service_entry_ptr to represent the process as a
service in the path_tree_table.

The process name is freed by o2_remove_remote_process().

OSC Service Name: Allocation, References, Freeing
-------------------------------------------------
o2_osc_port_new() creates a tcp service or incoming udp port
for OSC messages that are redirected to an O2 service. The
service name is copied to the heap and stored as
osc_service_name in the fds_info record associated with the
socket. For UDP, there are no other references, and the
osc_service_name is freed when the UDP socket is removed by
calling o2_osc_port_free(). For TCP, the osc_service_name
is shared between the OSC_TCP_SERVER_SOCKET and any 
OSC_TCP_SOCKET that was accepted from the server socket.
These sockets and their shared osc_service_name are also
removed by o2_osc_port_free().


Byte Order
----------
Messages are constructed and delivered in local host byte order.
When messages are sent to another process, the bytes are swapped
if necessary to be in network byte order. Messages arriving by
TCP or UDP are therefore in network byte order and are converted
to host byte order if necessary.

*/


/**
 *  TO DO:
 *  1. OSC - done
 *  1A. test compatibility with liblo receiving
 *  1B. test compatibility with liblo sending
 *  2. Bundles
 *  2A. test compatibility with liblo receiving
 *  2B. test compatibility with liblo sending
 *  3. IPv6
 *  3A. test compatibility with liblo receiving
 *  3B. test compatibility with liblo sending
 */

#include "o2.h"
#include <stdio.h>
#include "o2_dynamic.h"
#include "o2_socket.h"
#include "o2_search.h"
#include "o2_internal.h"
#include "o2_discovery.h"
#include "o2_message.h"
#include "o2_send.h"
#include "o2_sched.h"
#include "o2_clock.h"

#ifndef WIN32
#include <sys/time.h>
#endif

#ifndef O2_NO_DEBUG
char *o2_debug_prefix = "O2:";
int o2_debug = 0;

void o2_debug_flags(const char *flags)
{
    o2_debug = 0;
    if (strchr(flags, 'c')) o2_debug |= O2_DBC_FLAG;
    if (strchr(flags, 'r')) o2_debug |= O2_DBr_FLAG;
    if (strchr(flags, 's')) o2_debug |= O2_DBs_FLAG;
    if (strchr(flags, 'R')) o2_debug |= O2_DBR_FLAG;
    if (strchr(flags, 'S')) o2_debug |= O2_DBS_FLAG;
    if (strchr(flags, 'k')) o2_debug |= O2_DBK_FLAG;
    if (strchr(flags, 'd')) o2_debug |= O2_DBD_FLAG;
    if (strchr(flags, 't')) o2_debug |= O2_DBt_FLAG;
    if (strchr(flags, 'T')) o2_debug |= O2_DBT_FLAG;
    if (strchr(flags, 'm')) o2_debug |= O2_DBM_FLAG;
    if (strchr(flags, 'o')) o2_debug |= O2_DBo_FLAG;
    if (strchr(flags, 'O')) o2_debug |= O2_DBO_FLAG;
    if (strchr(flags, 'g')) o2_debug |= O2_DBG_FLAG;
    if (strchr(flags, 'a')) o2_debug |= O2_DBA_FLAGS;
}

void o2_dbg_msg(const char *src, o2_msg_data_ptr msg,
                const char *extra_label, const char *extra_data)
{
    printf("%s %s at %gs (local %gs)", o2_debug_prefix,
           src, o2_time_get(), o2_local_time());
    if (extra_label)
        printf(" %s: %s", extra_label, extra_data);
    printf("\n    ");
    o2_msg_data_print(msg);
    printf("\n");
}
#endif

void *((*o2_malloc)(size_t size)) = &malloc;
void ((*o2_free)(void *)) = &free;
// also used to detect initialization:
char *o2_application_name = NULL;

// these times are set when poll is called to avoid the need to
//   call o2_time_get() repeatedly
o2_time o2_local_now = 0.0;
o2_time o2_global_now = 0.0;


#ifndef O2_NO_DEBUG
void *o2_dbg_malloc(size_t size, char *file, int line)
{
    O2_DBM(printf("%s malloc %ld in %s:%d", o2_debug_prefix, size, file, line));
    fflush(stdout);
    void *obj = (*o2_malloc)(size);
    O2_DBM(printf(" -> %p\n", obj));
    return obj;
}

void o2_dbg_free(void *obj, char *file, int line)
{
    O2_DBM(printf("%s free in %s:%d <- %p\n", o2_debug_prefix, file, line, obj));
    (*free)(obj);
}

/**
 * Similar to calloc, but this uses the malloc and free functions
 * provided to O2 through a call to o2_memory().
 * @param[in] n     The number of objects to allocate.
 * @param[in] size  The size of each object in bytes.
 *
 * @return The address of newly allocated and zeroed memory, or NULL.
 */
void *o2_dbg_calloc(size_t n, size_t s, char *file, int line)
{
    void *loc = o2_dbg_malloc(n * s, file, line);
    if (loc) {
        memset(loc, 0, n * s);
    }
    return loc;
}
#else
void *o2_calloc(size_t n, size_t s)
{
    void *loc = O2_MALLOC(n * s);
    if (loc) {
        memset(loc, 0, n * s);
    }
    return loc;
}
#endif


int o2_initialize(const char *application_name)
{
    int err;
    if (o2_application_name) return O2_ALREADY_RUNNING;
    if (!application_name) return O2_BAD_NAME;

    o2_argv_initialize();
    
    // Initialize the hash tables
    o2_node_initialize(&master_table, "");
    o2_node_initialize(&path_tree_table, "");
    
    // Initialize the application name.
    o2_application_name = o2_heapify(application_name);
    if (!o2_application_name) {
        err = O2_NO_MEMORY;
        goto cleanup;
    }
    
    // Initialize discovery, tcp, and udp sockets.
    if ((err = o2_sockets_initialize())) goto cleanup;
    
    o2_service_new("_o2");
    o2_method_new("/_o2/dy", "ssii", &o2_discovery_handler, NULL, FALSE, FALSE);
    // TODO: DELETE     o2_method_new("/_o2/in", "siii", &o2_discovery_init_handler,
    //                                NULL, FALSE, FALSE);
    // "/sv/" service messages are sent by tcp as ordinary O2 messages, so they
    // are addressed by full name (IP:PORT). We cannot call them /_o2/sv:
    char address[32];
    snprintf(address, 32, "/%s/sv", o2_process->proc.name);
    o2_method_new(address, NULL, &o2_services_handler, NULL, FALSE, FALSE);
    snprintf(address, 32, "/%s/cs/cs", o2_process->proc.name);
    o2_method_new(address, "s", &o2_clocksynced_handler, NULL, FALSE, FALSE);
    o2_method_new("/_o2/ds", NULL, &o2_discovery_send_handler,
                  NULL, FALSE, FALSE);
    o2_time_initialize();
    o2_sched_initialize();
    o2_clock_initialize();
    
    o2_discovery_send_handler(NULL, "", NULL, 0, NULL); // start sending discovery messages
    o2_ping_send_handler(NULL, "", NULL, 0, NULL); // start sending clock sync messages
    
    return O2_SUCCESS;
  cleanup:
    o2_finish();
    return err;
}


int o2_memory(void *((*malloc)(size_t size)), void ((*free)(void *)))
{
    o2_malloc = malloc;
    o2_free = free;
    return O2_SUCCESS;
}


o2_time o2_set_discovery_period(o2_time period)
{
    o2_time old = o2_discovery_period;
    if (period < 0.1) period = 0.1;
    o2_discovery_period = period;
    return old;
}


void o2_notify_others(const char *service_name, int added)
{
    // when we add or remove a service, we must tell all other
    // processes about it. To find all other processes, use the o2_fds_info
    // table since all but a few of the entries are connections to processes
    for (int i = 0; i < o2_fds_info.length; i++) {
        fds_info_ptr info = DA_GET(o2_fds_info, fds_info, i);
        if (info->tag == TCP_SOCKET) {
            char address[32];
            snprintf(address, 32, "!%s/sv", info->proc.name);
            o2_send_cmd(address, 0.0, "ssB", o2_process->proc.name, service_name, added);
            O2_DBD(printf("%s o2_notify_others sent %s to %s (%s)\n", o2_debug_prefix,
                          service_name, info->proc.name, added ? "added" : "removed"));
        }
    }
}


int o2_service_new(const char *service_name)
{    
//#if defined(WIN32) || defined(_MSC_VER)
    /* Windows Server 2003 or later (Vista, 7, etc.) must join the
     * multicast group before bind(), but Windows XP must join
     * after bind(). */
    // int wins2003_or_later = detect_windows_server_2003_or_later();
//#endif

    // make sure service does not already exist
    generic_entry_ptr entry = *o2_service_find(service_name);
    if (entry) return O2_FAIL;
    
    // Add a o2_local_service structure for the o2 service.
    DA_EXPAND(o2_process->proc.services, service_table);
    service_name = o2_heapify(service_name);
    DA_LAST(o2_process->proc.services, service_table)->name = service_name;
    
    if (!o2_tree_insert_node(&path_tree_table, service_name)) {
        O2_DBD(printf("%s o2_service_new failed at o2_tree_insert_node (%s)\n",
                      o2_debug_prefix, service_name));
        return O2_FAIL;
    }
    o2_notify_others(service_name, TRUE);
    return O2_SUCCESS;
}


static void check_messages()
{
    for (int i = 0; i < O2_SCHED_TABLE_LEN; i++) {
        for (o2_message_ptr msg = o2_ltsched.table[i]; msg; msg = msg->next) {
            assert(msg->allocated >= msg->length);
        }
    }
}


int o2_poll()
{
    check_messages();
    o2_local_now = o2_local_time();
    if (o2_gtsched_started) {
        o2_global_now = o2_local_to_global(o2_local_now);
    } else {
        o2_global_now = -1.0;
    }
    o2_sched_poll(); // deal with the timestamped message
    o2_recv(); // recieve and dispatch messages
    o2_deliver_pending();
    return O2_SUCCESS;
}


int o2_stop_flag = FALSE;

int o2_run(int rate)
{
    if (rate <= 0) rate = 1000; // poll about every ms
    int sleep_usec = 1000000 / rate;
    o2_stop_flag = FALSE;
    while (!o2_stop_flag) {
        o2_poll();
        usleep(sleep_usec);
    }
    return O2_SUCCESS;
}


int o2_status(const char *service)
{
    generic_entry_ptr entry = *o2_service_find(service);
    if (!entry) return O2_FAIL;
    switch (entry->tag) {
        case O2_REMOTE_SERVICE: {
            int i = ((remote_service_entry_ptr) entry)->process_index;
            fds_info_ptr info = DA_GET(o2_fds_info, fds_info, i);
            if (o2_clock_is_synchronized &&
                info->proc.status == PROCESS_OK) {
                return O2_REMOTE;
            } else {
                return O2_REMOTE_NOTIME;
            }
        }
        case PATTERN_NODE:
        case PATTERN_HANDLER:
            return (o2_clock_is_synchronized ? O2_LOCAL : O2_LOCAL_NOTIME);
        case O2_BRIDGE_SERVICE:
        default:
            return O2_FAIL; // not implemented yet
        case OSC_REMOTE_SERVICE: // no timestamp synchronization with OSC
            if (o2_clock_is_synchronized) {
                return O2_TO_OSC;
            } else {
                return O2_TO_OSC_NOTIME;
            }
    }
}


#ifdef WIN32
int gettimeofday(struct timeval * tp, struct timezone * tzp)
{
    // Note: some broken versions only have 8 trailing zero's, the correct epoch has 9 trailing zero's
    static const uint64_t EPOCH = ((uint64_t)116444736000000000ULL);
    
    SYSTEMTIME  system_time;
    FILETIME    file_time;
    uint64_t    time;
    
    GetSystemTime(&system_time);
    SystemTimeToFileTime(&system_time, &file_time);
    time = ((uint64_t)file_time.dwLowDateTime);
    time += ((uint64_t)file_time.dwHighDateTime) << 32;
    
    tp->tv_sec = (long)((time - EPOCH) / 10000000L);
    tp->tv_usec = (long)(system_time.wMilliseconds * 1000);
    return 0;
}
#endif


static char o2_error_msg[100];
static char *error_strings[] = {
    "O2_SUCCESS",
    "O2_FAIL",
    "O2_SERVICE_CONFLICT",
    "O2_NO_SERVICE",
    "O2_NO_MEMORY",
    "O2_ALREADY_RUNNING",
    "O2_BAD_NAME",
    "O2_BAD_TYPE",
    "O2_BAD_ARGS",
    "O2_TCP_HUP",
    "O2_HOSTNAME_TO_NETADDR_FAIL",
    "O2_TCP_CONNECT_FAIL",
    "O2_NO_CLOCK",
    "O2_NO_HANDLER",
    "O2_INVALID_MSG",
    "O2_SEND_FAIL" };
    

const char *o2_error_to_string(int i)
{
    if (i < 1 && i >= O2_SEND_FAIL) {
        sprintf(o2_error_msg, "O2 error %s", error_strings[-i]);
    } else {
        sprintf(o2_error_msg, "O2 error, code is %d", i);
    }
    return o2_error_msg;
}



int o2_finish()
{
    if (o2_socket_delete_flag) {
        // we were counting on o2_recv() to clean up some sockets, but
        // it hasn't been called
        o2_free_deleted_sockets();
    }
    // Close all the sockets.
    for (int i = 0 ; i < o2_fds.length; i++) {
        SOCKET sock = DA_GET(o2_fds, struct pollfd, i)->fd;
#ifdef SHUT_WR
        shutdown(sock, SHUT_WR);
#endif
        if (closesocket(sock)) perror("closing socket");
        O2_DBo(printf("%s In o2_finish, close socket %ld\n", o2_debug_prefix,
                      (long) (DA_GET(o2_fds, struct pollfd, i)->fd)));
        fds_info_ptr info = DA_GET(o2_fds_info, fds_info, i);
        if (info->message) O2_FREE(info->message);
        if (info->tag == TCP_SOCKET) {
            O2_FREE(info->proc.services.array);
        } else if (info->tag == OSC_SOCKET ||
                   info->tag == OSC_TCP_SERVER_SOCKET) {
            // free the osc service name; this is shared
            // by any OSC_TCP_SOCKET, so we can ignore
            // OSC_TCP_SOCKETs.
            assert(info->osc_service_name);
            O2_FREE(info->osc_service_name);
        }
    }
    DA_FINISH(o2_fds);
    DA_FINISH(o2_fds_info);
    
    o2_node_finish(&path_tree_table);
    o2_node_finish(&master_table);
    
    o2_argv_finish();
    o2_sched_finish(&o2_gtsched);
    o2_sched_finish(&o2_ltsched);
    o2_discovery_finish();

    if (o2_application_name) O2_FREE(o2_application_name);
    o2_application_name = NULL;
    return O2_SUCCESS;
}
