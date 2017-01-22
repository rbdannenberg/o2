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
o2_fds   o2_fds_info                 names
+----+   +---------+  +---------+    ______
|    |   |        -+->| process +-->|______|
|----|   |---------|  |  info   |   |______|
|    |   |         |  +-----^---+
|----|   |---------|        |
|    |   |         |        |
+----+   +---------+        |
                            | 
o2_path_tree             |
+----------+    +--------+  |
|         -+--->|services+--+
+----------+    | entry  |
|         -+->  |        |   +--------+
+----------+    |       -+-->|(local)-+---> etc
                +--------+   | node   |  +---------+
                             | entry  |->| handler |
                             +--------+  |  entry  |
                                         +---------+

Note: o2_path_tree is a node_entry (hash table)
node_entry (hash table) entries can be:
    node_entry - the next node in a path
    handler_entry - handler at end of path
    services_entry - in o2_path_tree only, a list
        of services of the same name, the highest
        IP:Port string overrides any others.

o2_full_path_table
+----------+             +--------+
|         -+------------>| handler|
+----------+  +--------+ |  node  |
|         -+->| handler| +--------+
+----------+  | node   |
              +--------+



Each application has:
o2_path_tree - a dictionary mapping service names to a
    services_entry, which keeps a list of who offers the service.
    Only the highest IP:port string (lexicographically) is valid.
    Generally, trying to offer identical service names from 
    multiple processes is a bad idea, and note that until the 
    true provider with the highest IP:port string is discovered,
    messages may be sent to a different service with the same name.

    Each services_entry has an array (not a hash table) of entries 
    of the following types:
        node_entry: a local service. This is the root of a tree
            of hash tables where leaves are handler_entry's.
        handler_entry: a local service. If there is a 
            handler_entry at this level, it is the single handler
            for all messages to this local service.
        remote_service_entry: includes index of the socket and 
            IP:port name of the service provider
        osc_entry: delegates to an OSC server. For the purposes of
            finding the highest IP:port string, this is considered
            to be a local service. A services_entry can have at 
            most one of node_entry, handler_entry, osc_entry, but
            any number of remote_service_entry's.

    The first element in the array of entries in a service_entry 
    is the "active" service -- the one with the highest IP:port
    string. Other elements are not sorted, so when a service is 
    removed, a linear search to find the largest remaining offering
    (if any) is performed.

    The o2_path_tree also maps IP addresses + ports (as strings
    that begin with a digit and have the form 128.2.100.120:4000) 
    to a services_entry that contains one remote_service_entry.

o2_full_path_table is a dictionary for full paths, permitting a single hash 
    table lookup for addresses of the form !synth/lfo/freq. In practice
    an additional lookup of just the service name is required to 
    determine if the service is local and if there is a single handler
    for all messages to that service.

Each handler object is referenced by some node in the o2_path_tree
    and by the o2_full_path_table dictionary, except for handlers that handle
    all service messages. These are only referenced by the 
    o2_path_tree.

o2_fds is a dynamic array of sockets for poll
o2_fds_info is a parallel dynamic array of pointers to process info
process_info includes a tag: 
        UDP_SOCKET is for all incoming UDP messages
        TCP_SOCKET makes a TCP connection to a remote process
        DISCOVER_SOCKET is a UDP socket for incoming discovery messages
        TCP_SERVER_SOCKET is the server socket, representing local process
        OSC_SOCKET is for incoming OSC messages via UDP
        OSC_TCP_SERVER_SOCKET is a server socket for OSC TCP connections
        OSC_TCP_SOCKET is for incoming OSC messages via TCP
        OSC_TCP_CLIENT is for outgoing OSC messages via TCP
    All process info records contain an index into o2_fds (and
        o2_fds_info and the index must be updated if a socket is moved.)
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
server. If the server gets a discovery message from the client, it
can't connect because it's the server, so it merely generates an
!_o2/dy message to the client to prompt the client to connect. 

Info for each process is stored in fds_info, which has one entry per
socket. Most sockets are TCP sockets and the associated process info
pointed to by fds_info represents a remote process. However, the 
info associated with the TCP server port (every process has one of these)
represents the local process and is created when O2 is initialized. 
There are a few other sockets: discovery socket, UDP send socket, UDP 
receive socket, and OSC sockets (if created by the user).

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
of these services is mapped by o2_path_tree to a 
services_entry that contains a remote_service_entry,
which has an integer index of the corresponding 
remote process (both the socket in the fds 
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
Each remote process has a name, e.g. "128.2.1.100:55765"
that can be used as a service name in an O2 address, e.g.
to announce a new local service to that remote process.
The name is on the heap and is "owned" by the process_info
record associated with the TCP_SOCKET socket for remote
processes. Also, the local process has its name owned by
the TCP_SERVER socket.

The process name is *copied* and used as the key for a
service_entry_ptr to represent the service in the 
o2_path_tree.

The process name is freed by o2_remove_remote_process().

OSC Service Name: Allocation, References, Freeing
-------------------------------------------------
o2_osc_port_new() creates a tcp service or incoming udp port
for OSC messages that are redirected to an O2 service. The
service_entry record "owns" the servier name which is on the 
heap. This name is shared by osc.service_name in the osc_info
record.  For UDP, there are no other references, and the
osc.service_name is not freed when the UDP socket is removed
(unless this is the last provider of the service, in which
case the service_entry record and is removed and its key is
freed). For TCP, the osc.service_name is *copied* to the 
OSC_TCP_SERVER_SOCKET and shared with any OSC_TCP_SOCKET 
that was accepted from the server socket.  These sockets 
and their shared osc service name are freed when the OSC
service is removed.

PATTERN_NODE and PATTERN_HANDLER keys at top level
----------------------------------------------------
If a node_entry (tag == PATTERN_NODE) or handler_entry
(tag == PATTERN_HANDLER) exists as a service provider, i.e.
in the services list of a services_entry record, the key 
field is not really needed because the service lookup takes
you to the services list, and the first element there offers
the service. E.g. if there's a (local) handler for 
/service1/bar, then we lookup "service1" in the o2_path_tree,
which takes us to a services_entry, the first element on its
services list should be a node_entry. There, we do a hash lookup
of "bar" to get to a handler_entry. The key is set to NULL for
node_entry and handler_entry records that represent services and
are directly pointed to from a services list.


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
 *  see each error return has the right error code
 *  tests that generate error messages
 */

#include <stdio.h>
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
    if (strchr(flags, 'c')) o2_debug |= O2_DBc_FLAG;
    if (strchr(flags, 'r')) o2_debug |= O2_DBr_FLAG;
    if (strchr(flags, 's')) o2_debug |= O2_DBs_FLAG;
    if (strchr(flags, 'R')) o2_debug |= O2_DBR_FLAG;
    if (strchr(flags, 'S')) o2_debug |= O2_DBS_FLAG;
    if (strchr(flags, 'k')) o2_debug |= O2_DBk_FLAG;
    if (strchr(flags, 'd')) o2_debug |= O2_DBd_FLAG;
    if (strchr(flags, 't')) o2_debug |= O2_DBt_FLAG;
    if (strchr(flags, 'T')) o2_debug |= O2_DBT_FLAG;
    if (strchr(flags, 'm')) o2_debug |= O2_DBm_FLAG;
    if (strchr(flags, 'o')) o2_debug |= O2_DBo_FLAG;
    if (strchr(flags, 'O')) o2_debug |= O2_DBO_FLAG;
    if (strchr(flags, 'g')) o2_debug |= O2_DBg_FLAGS;
    if (strchr(flags, 'a')) o2_debug |= O2_DBa_FLAGS;
}

void o2_dbg_msg(const char *src, o2_msg_data_ptr msg,
                const char *extra_label, const char *extra_data)
{
    printf("%s %s at %gs (local %gs)", o2_debug_prefix,
           src, o2_time_get(), o2_local_time());
    if (extra_label)
        printf(" %s: %s ", extra_label, extra_data);
    printf("\n    ");
    o2_msg_data_print(msg);
    printf("\n");
}
#endif

void *((*o2_malloc)(size_t size)) = &malloc;
void ((*o2_free)(void *)) = &free;
// also used to detect initialization:
const char *o2_application_name = NULL;

// these times are set when poll is called to avoid the need to
//   call o2_time_get() repeatedly
o2_time o2_local_now = 0.0;
o2_time o2_global_now = 0.0;


#ifndef O2_NO_DEBUG
void *o2_dbg_malloc(size_t size, char *file, int line)
{
    O2_DBm(printf("%s malloc %lld in %s:%d", o2_debug_prefix, (long long) size, file, line));
    fflush(stdout);
    void *obj = (*o2_malloc)(size);
    O2_DBm(printf(" -> %p\n", obj));
    return obj;
}

void o2_dbg_free(void *obj, char *file, int line)
{
    O2_DBm(printf("%s free in %s:%d <- %p\n", o2_debug_prefix, file, line, obj));
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
    o2_node_initialize(&o2_full_path_table, NULL);
    o2_node_initialize(&o2_path_tree, NULL);
    
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
        process_info_ptr info = GET_PROCESS(i);
        if (info->tag == TCP_SOCKET) {
            char address[32];
            snprintf(address, 32, "!%s/sv", info->proc.name);
            o2_send_cmd(address, 0.0, "ssB", o2_process->proc.name, service_name, added);
            O2_DBd(printf("%s o2_notify_others sent %s to %s (%s)\n", o2_debug_prefix,
                          service_name, info->proc.name, added ? "added" : "removed"));
        }
    }
}


o2_info_ptr o2_local_service_find(services_entry_ptr *services)
{
    // search for local service
    if (!*services) return FALSE;
    for (int i = 0; i < (*services)->services.length; i++) {
        o2_info_ptr service = GET_SERVICE((*services)->services, i);
        if (service->tag != TCP_SOCKET) {
            return service; // local service already exists
        }
    }
    return NULL;
}

/* adds a service provider - a service is added to the list of services in 
 *    a service_entry struct.
 * 1) create the service_entry struct if none exists
 * 2) put the service onto process's list of service names
 * 3) add new service to the list
 */
int o2_service_provider_new(o2string service_name, o2_info_ptr service, process_info_ptr process)
{
    services_entry_ptr *services = (services_entry_ptr *) o2_lookup(&o2_path_tree, service_name);
    services_entry_ptr s;
    // 1) if no entry, create an empty one
    if (!*services) {
        s = O2_CALLOC(1, sizeof(services_entry));
        s->tag = SERVICES;
        s->key = o2_heapify(service_name);
        s->next = NULL;
        DA_INIT(s->services, o2_entry_ptr, 1);
        o2_add_entry_at(&o2_path_tree, (o2_entry_ptr *) services, 
                        (o2_entry_ptr) s);
    } else { // if this is local and a local service exists already, fail
        if (process == o2_process && (o2_local_service_find(services) != NULL)) {
            return O2_SERVICE_EXISTS;
        }
        s = *services;
    }
    // Now we know it's safe to add a local service and we have a
    // place to put it
    // 2) add the service name to the process so we can enumerate
    //    local services
    DA_APPEND(process->proc.services, o2string, s->key);
    // 3) find insert location: either at front or at back of services->services
    DA_EXPAND(s->services, o2_entry_ptr);
    int index = s->services.length - 1;
    if (index > 0) { // see if we should go first
        o2string our_ip_port = process->proc.name;
        // find the top entry
        o2_info_ptr top_entry = GET_SERVICE(s->services, 0);
        o2string top_ip_port = (top_entry->tag == TCP_SOCKET ?
                                ((process_info_ptr) top_entry)->proc.name :
                                o2_process->proc.name);
        if (strcmp(our_ip_port, top_ip_port) > 0) {
            DA_SET(s->services, o2_info_ptr, index, top_entry);
            index = 0; // put new service at the top of the list
        }
    }
    DA_SET(s->services, o2_info_ptr, index, service);
    // special case for osc: need service name
    if (service->tag == OSC_REMOTE_SERVICE) {
        ((osc_info_ptr) service)->service_name = (*services)->key;
    }
    return O2_SUCCESS;
}


int o2_service_new(const char *service_name)
{    
    // find services_node if any
    char padded_name[NAME_BUF_LEN];
    if (strchr(service_name, "/")) return O2_BAD_SERVICE_NAME;
    o2_string_pad(padded_name, service_name);
    node_entry_ptr node = o2_node_new(NULL);
    if (!node) return O2_FAIL;
    int rslt = o2_service_provider_new(padded_name, (o2_info_ptr) node, o2_process);
    if (rslt != O2_SUCCESS) {
        O2_FREE(node);
        return rslt;
    }
    o2_notify_others(padded_name, TRUE);
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
    o2_recv(); // receive and dispatch messages
    o2_deliver_pending();
    return O2_SUCCESS;
}


int o2_stop_flag = FALSE;

#ifdef WIN32
#define usleep(x) Sleep((x)/1000)
#endif

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
    if (!service || strchr(service, '/'))
        return O2_BAD_SERVICE_NAME;
    o2_info_ptr entry = o2_service_find(service);
    if (!entry) return O2_FAIL;
    switch (entry->tag) {
        case TCP_SOCKET: {
            process_info_ptr info = (process_info_ptr) entry;
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
        process_info_ptr info = GET_PROCESS(i);
        if (info->message) O2_FREE(info->message);
        if (info->tag == TCP_SOCKET) {
            DA_FINISH(info->proc.services);
        } else if (info->tag == OSC_SOCKET ||
                   info->tag == OSC_TCP_SERVER_SOCKET) {
            // free the osc service name; this is shared
            // by any OSC_TCP_SOCKET, so we can ignore
            // OSC_TCP_SOCKETs.
            assert(info->osc.service_name);
            O2_FREE((void *) info->osc.service_name);
        }
        O2_FREE(info);
    }
    DA_FINISH(o2_fds);
    DA_FINISH(o2_fds_info);
    
    o2_node_finish(&o2_path_tree);
    o2_node_finish(&o2_full_path_table);
    
    o2_argv_finish();
    o2_sched_finish(&o2_gtsched);
    o2_sched_finish(&o2_ltsched);
    o2_discovery_finish();

    if (o2_application_name) O2_FREE((void *) o2_application_name);
    o2_application_name = NULL;
    return O2_SUCCESS;
}
