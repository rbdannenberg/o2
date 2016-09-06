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

Each application has:
path_tree_table - a dictionary mapping service names to services
    (service_entry, remote_service_entry, osc_entry). Local services
    have subtrees of path nodes (child_node) points to a dictionary.
    Also maps IP addresses + ports (as strings that begin with a digit
    and have the form 128.2.100.120:4000) to remote_service_entry.
Nodes below the service names can be internal nodes (node_entry),
    which point via child_node to another dictionary (the next node
    in the path. Alternatively, there can be a handler_entry that
    means this is a leaf node with a handler function.
master_table - a dictionary mapping full address strings to handlers
 

Each handler object is referenced by some node in the path_tree_table
    and by the master_table dictionary. 

o2_fds is a dynamic array of sockets for poll
o2_fds_info is a parallel array of additional information
    includes a tag: what kind of fd is this?
        UDP input (all incoming UDP messages to one port)
        TCP (one for each O2 host) 
        TCP connect port (for O2 hosts to make connection)
        UDP OSC server (input) (there can be more than one)
        TCP OSC server port to accept connection requests (there can be more than one)
        TCP OSC server (accepted connection) (input) (there can be more than one connection for each TCP OSC server port (see above)
        TCP OSC client (output) (there can be more than one)
        Discovery port (UDP)
    OSC service name (if this is an OSC server socket)
    Remote process pointer (if this is a TCP connection to a remote process)

o2_process: a process descriptor with
    key - the ip address
    services - an dynamic array of local service names
    little_endian - true if this is on a little endian host
    tcp_port
    udp_port
    tcp_fd_index - index of tcp port in fds array
    udp_sa - a sockaddr_in structure for sending udp to this process

Sockets
-------

o2_fds_info has state to receive messages. When a message is received,
there is a handler function that is called to process the message.
    For outgoing O2 messages, we have an associated process to tell
where to send.
    For incoming O2 messages, no extra info is needed; just deliver 
the message.
    For incoming OSC messages, fds_info has the osc_service_name where
messages are redirected.

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

*/


/**
 *  TO DO:
 *  1. TCP socket initialize and set up.
 *  2. Use hash table to deliver "!" messages
 *  3. Clock sync
 *  4. IPv6
 *  5. Pattern matching has to be done one node at a time using a tree structure. At the top level, find the service, then match on the next field, then the next, etc. Matching a full pattern against every full address is too embarrassingly slow and simple.
 *  6. Reimplement discovery using "real" o2 messages instead of our IP/port/port/port/service/service/... format.
 *  7. Make o2_getvtime() be a linear mapping (speed and offset control from local clock)
 *  8. OSC ports
 *  9. Bundles
 *  10.Sometime the program stop at the tcp connect function. Need to find out the reason.
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
process_info o2_process;

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

    // Initialize the hash tables
    initialize_node(&master_table, "");
    initialize_node(&path_tree_table, "");
    
    // Initialize the application name.
    o2_application_name = o2_heapify(application_name);
    if (!o2_application_name) {
        err = O2_NO_MEMORY;
        goto cleanup;
    }
    o2_init_process(&o2_process, PROCESS_NO_CLOCK, IS_LITTLE_ENDIAN);
    
    // Initialize discovery, tcp, and udp sockets.
    if ((err = init_sockets())) goto cleanup;
    
    o2_add_service("_o2");
    o2_add_method("/_o2/dy", "sssii", &o2_discovery_handler, NULL, FALSE, FALSE);
    o2_add_method("/_o2/in", "ssiii", &o2_discovery_init_handler,
                  NULL, FALSE, FALSE);
    // "/sv/" service messages are sent by tcp as ordinary O2 messages, so they
    // are addressed by full name (IP:PORT). We cannot call them /_o2/sv:
    char address[32];
    snprintf(address, 32, "/%s/sv", o2_process.name);
    o2_add_method(address, NULL, &o2_services_handler, NULL, FALSE, FALSE);
	snprintf(address, 32, "/%s/cs/cs", o2_process.name);
    o2_add_method(address, "s", &o2_clocksynced_handler, NULL, FALSE, FALSE);
    o2_add_method("/_o2/ds", NULL, &o2_discovery_send_handler, NULL, FALSE, FALSE);
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
    DA_EXPAND(o2_process.services, service_table);
    service_name = o2_heapify(service_name);
    DA_LAST(o2_process.services, service_table)->name = service_name;
    
    if (!tree_insert_node(&path_tree_table, service_name)) {
        return O2_FAIL;
    }

    // when we add a service to this process, we must tell all other
    // processes about it. To find all other processes, use the o2_fds_info
    // table since all but a few of the entries are connections to processes
    for (int i = 0; i < o2_fds_info.length; i++) {
        fds_info_ptr info = DA_GET(o2_fds_info, fds_info, i);
        if (info->tag == TCP_SOCKET) {
            process_info_ptr process = info->u.process_info;
            char address[32];
			snprintf(address, 32, "!%s/sv", process->name);
            o2_send_cmd(address, 0.0, "ss", o2_process.name, service_name);
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
        case O2_REMOTE_SERVICE:
            if (o2_clock_is_synchronized &&
                ((remote_service_entry_ptr) entry)->parent->status ==
                PROCESS_OK) {
                return O2_REMOTE;
            } else {
                return O2_REMOTE_NOTIME;
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
            O2_FREE(info->u.process_info->services.array);
        }
    }
    DA_FINISH(o2_fds);
    DA_FINISH(o2_fds_info);
    
    free_node(&path_tree_table);
    free_node(&master_table);
    
    if (o2_application_name) O2_FREE(o2_application_name);
    o2_application_name = NULL;
    return O2_SUCCESS;
}
