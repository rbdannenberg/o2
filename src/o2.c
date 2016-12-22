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
        key - the ip address
        services - an dynamic array of local service names
        little_endian - true if this is on a little endian host
        tcp_port number
        udp_port number
        udp_sa - a sockaddr_in structure for sending udp to this process


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

Discovery messages are sent to 5 discovery ports.
The address is !_o2/dy, and the arguments are:
    big-/littl-endian ("b" or "l"),
    application name (string)
    local ip (string)
    tcp (int32)
When a discovery is made, a TCP connection is made.
When a TCP connection is connected or accepted, the
process sends a list of services to !_o2/sv. Arguments 
are all strings. The first argument is the process name,
e.g. 128.2.100.50:4500; i.e. the ip and tcp server port
number. The remaining arguments are service names.

Process Creation
----------------

o2_discovery_handler() receives !_o2/dy message.
There are two cases based on whether the local host is
the server or not. The server (to which the other host
connects as client) is the host with the greater IP:Port
string.

Info for each process is stored in fds_info, which has
one entry per socket. Most sockets are TCP sockets and
the associated process info in fds_info represents a
remote process. The info associated with the TCP server
port represents the local process and is created when
O2 is initialized.

Process Creation: Remote Process As Server
-------------------------------------
Make a tcp socket.
Put IP:Port in path_tree_table with index of socket info.
Connect socket to the remote process.
Send init message with endian-ness, IP, port #s, and clock status.
Send list of local services.
Start clocksync protocol.

Process Creation: Local Process As Server
--------------------------------------
o2_discovery_handler() simply sends back a discovery message
to the remote process. This is not strictly necessary since 
discovery messages are broadcast periodically in case a
message is lost. However, if the local process is old and the
remote process is new, the remote process will send a
discovery message as soon as possible whereas the local 
process might wait several seconds. By answering the 
remote process discovery message with a discovery message
from the local process, we can eliminate the wait for the
local process's polling cycle.

One way or another, the remote process receives a discovery
message from the local process. It follows the "Remote 
Process As Server" sequence above, sending a connect request.

Accept the connect request, creating a new tcp port entry.
The handler is o2_tcp_initial_handler.

o2_tcp_initial_handler() is called when the init message
is sent (see "Send init message..." in "Remote Process As
Server" above). This handler calls o2_discovery_init_handler()
directly (without a normal O2 message dispatch), and passes
the socket's fds_info record as user_data. This allows us to
initialize the fds_info and create an entry in path_tree_table.

Send init message with endian-ness, IP, port #s, and clock status.
Send list of local services.
Start clocksync protocol.

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

char *debug_prefix = NULL;
int o2_debug = 0;

void *((*o2_malloc)(size_t size)) = &malloc;
void ((*o2_free)(void *)) = &free;
// also used to detect initialization:
char *o2_application_name = NULL;

// these times are set when poll is called to avoid the need to
//   call o2_get_time() repeatedly
o2_time o2_local_now = 0.0;
o2_time o2_global_now = 0.0;


/**
 * Similar to calloc, but this uses the malloc and free functions
 * provided to O2 through a call to o2_memory().
 * @param[in] n     The number of objects to allocate.
 * @param[in] size  The size of each object in bytes.
 *
 * @return The address of newly allocated and zeroed memory, or NULL.
 */
void *o2_calloc(size_t n, size_t size)
{
    void *loc = O2_MALLOC(n * size);
    if (loc) {
        memset(loc, 0, n * size);
    }
    return loc;
}


int o2_initialize(char *application_name)
{
    int err;
    if (o2_application_name) return O2_RUNNING;
    if (!application_name) return O2_BAD_NAME;

    o2_initialize_argv();
    
    // Initialize the hash tables
    initialize_node(&master_table, "");
    initialize_node(&path_tree_table, "");
    
    // Initialize the application name.
    o2_application_name = o2_heapify(application_name);
    if (!o2_application_name) {
        err = O2_NO_MEMORY;
        goto cleanup;
    }
    
    // Initialize discovery, tcp, and udp sockets.
    if ((err = init_sockets())) goto cleanup;
    
    o2_add_service("_o2");
    o2_add_method("/_o2/dy", "sssii", &o2_discovery_handler, NULL, FALSE, FALSE);
    o2_add_method("/_o2/in", "ssiii", &o2_discovery_init_handler,
                  NULL, FALSE, FALSE);
    // "/sv/" service messages are sent by tcp as ordinary O2 messages, so they
    // are addressed by full name (IP:PORT). We cannot call them /_o2/sv:
    char address[32];
    snprintf(address, 32, "/%s/sv", o2_process->proc.name);
    o2_add_method(address, NULL, &o2_services_handler, NULL, FALSE, FALSE);
    snprintf(address, 32, "/%s/cs/cs", o2_process->proc.name);
    o2_add_method(address, "s", &o2_clocksynced_handler, NULL, FALSE, FALSE);
    o2_add_method("/_o2/ds", NULL, &o2_discovery_send_handler,
                  NULL, FALSE, FALSE);
    o2_time_init();
    o2_sched_init();
    o2_clock_init();
    
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


int o2_add_service(char *service_name)
{
    // Calloc memory for the o2_service_ptr.
    
#if defined(WIN32) || defined(_MSC_VER)
    /* Windows Server 2003 or later (Vista, 7, etc.) must join the
     * multicast group before bind(), but Windows XP must join
     * after bind(). */
    // int wins2003_or_later = detect_windows_server_2003_or_later();
#endif
    
    // Add a o2_local_service structure for the o2 service.
    DA_EXPAND(o2_process->proc.services, service_table);
    service_name = o2_heapify(service_name);
    DA_LAST(o2_process->proc.services, service_table)->name = service_name;
    
    if (!tree_insert_node(&path_tree_table, service_name)) {
        return O2_FAIL;
    }

    // when we add a service to this process, we must tell all other
    // processes about it. To find all other processes, use the o2_fds_info
    // table since all but a few of the entries are connections to processes
    for (int i = 0; i < o2_fds_info.length; i++) {
        fds_info_ptr info = DA_GET(o2_fds_info, fds_info, i);
        if (info->tag == TCP_SOCKET) {
            char address[32];
            snprintf(address, 32, "!%s/sv", info->proc.name);
            o2_send_cmd(address, 0.0, "ss", o2_process->proc.name, service_name);
        }
    }
    
    return O2_SUCCESS;
}


int o2_poll()
{
    o2_local_now = o2_local_time();
    if (o2_gtsched_started) {
        o2_global_now = o2_local_to_global(o2_local_now);
    } else {
        o2_global_now = -1.0;
    }
    o2_sched_poll(); // deal with the timestamped message
    o2_deliver_pending();
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
    generic_entry_ptr entry = o2_find_service(service);
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
            return (o2_clock_is_synchronized ? O2_LOCAL : O2_LOCAL_NOTIME);
        case O2_BRIDGE_SERVICE:
        default:
            return O2_FAIL; // not implemented yet
        case OSC_REMOTE_SERVICE: // no timestamp synchronization with OSC
            return O2_TO_OSC_NOTIME;
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
const char *o2_get_error(int i)
{
    sprintf(o2_error_msg, "O2 error, code is %d", i);
    return o2_error_msg;
}



int o2_finish()
{
    // Close all the sockets.
    for (int i = 0 ; i < o2_fds.length; i++) {
        closesocket(DA_GET(o2_fds, struct pollfd, i)->fd);
        fds_info_ptr info = DA_GET(o2_fds_info, fds_info, i);
        if (info->message) O2_FREE(info->message);
        if (info->tag == TCP_SOCKET) {
            O2_FREE(info->proc.services.array);
        }
    }
    DA_FINISH(o2_fds);
    DA_FINISH(o2_fds_info);
    
    o2_finalize_node(&path_tree_table);
    o2_finalize_node(&master_table);
    
    o2_finish_argv();

    if (o2_application_name) O2_FREE(o2_application_name);
    o2_application_name = NULL;
    return O2_SUCCESS;
}
