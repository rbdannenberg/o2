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
                                    
o2_context->fds                  
         o2_context->fds_info    
+----+   +---------+  +--------------+  list of services
|    |   |        -+->| process info |  offered by process
|----|   |---------|  |              |   +-------+
|    |   |         |  |    services -+-->|      =+====---+
|----|   |---------|  +--------------+   |       |    |  |
|    |   |         |        ^            +-------+    |  v
+----+   +---------+        |                         |  property
                            |     to services entry <-+  string
o2_context->path_tree       |
+----------+    +--------+  |             +--------+
|         -+--->|services+--+             | bridge |
+----------+    | entry  +--------------->|  info  |
|         -+->  |        |   +--------+   +--------+
+----------+    |       -+-->|(local)-+---> etc
                +--------+   | node   |  +---------+
                             | entry  |->| handler |
                             +--------+  |  entry  |
                                         +---------+

Note: o2_context->path_tree is a hash_node (hash table)
hash_node (hash table) entries can be:
    hash_node - the next node in a path
    handler_entry - handler at end of path
    bridge_entry - messages are handled by passing to a
        function that can be installed to support different
        transports.
    services_entry - in o2_context->path_tree only, a list
        of services of the same name, the highest
        IP:Port string overrides any others.


o2_context->full_path_table
+----------+             +--------+
|         -+------------>| handler|
+----------+  +--------+ |  node  |
|         -+->| handler| +--------+
+----------+  | node   |
              +--------+



Each ensemble has:
o2_context->path_tree - a dictionary mapping service names to a
    services_entry, which keeps a list of who offers the service.
    Only the highest IP:port string (lexicographically) is valid.
    Generally, trying to offer identical service names from 
    multiple processes is a bad idea, and note that until the 
    true provider with the highest IP:port string is discovered,
    messages may be sent to a different service with the same name.

    Each services_entry has an array (not a hash table) of entries 
    of the following types:
        hash_node: a local service. This is the root of a tree
            of hash tables where leaves are handler_entry's.
        handler_entry: a local service. If there is a 
            handler_entry at this level, it is the single handler
            for all messages to this local service.
        remote_service_entry: includes index of the socket and 
            IP:port name of the service provider
        osc_entry: delegates to an OSC server. For the purposes of
            finding the highest IP:port string, this is considered
            to be a local service. A services_entry can have at 
            most one of hash_node, handler_entry, osc_entry, but
            any number of remote_service_entry's.
        bridge_entry: service is remote but reached by an alternate
            transport (not IP)

    The first element in the array of entries in a service_entry 
    is the "active" service -- the one with the highest IP:port
    string. Other elements are not sorted, so when a service is 
    removed, a linear search to find the largest remaining offering
    (if any) is performed.

    The o2_context->path_tree also maps IP addresses + ports (as strings
    that begin with a digit and have the form 128.2.100.120:4000) 
    to a services_entry that contains one remote_service_entry.

o2_context->full_path_table is a dictionary for full paths, permitting a single hash 
    table lookup for addresses of the form !synth/lfo/freq. In practice
    an additional lookup of just the service name is required to 
    determine if the service is local and if there is a single handler
    for all messages to that service.

Each handler object is referenced by some node in the o2_context->path_tree
    and by the o2_context->full_path_table dictionary, except for handlers that handle
    all service messages. These are only referenced by the 
    o2_context->path_tree.

o2_context->fds is a dynamic array of sockets for poll
o2_context->fds_info is a parallel dynamic array of pointers to process info
 process_info includes a tag:
    INFO_TCP_SERVER         -- the local process
    INFO_TCP_NOCLOCK        -- not-synced client or server remote proc
    INFO_TCP_SOCKET         -- clock-synced client or server remote proc
    INFO_UDP_SOCKET         -- UDP receive socket for this process
    INFO_OSC_UDP_SERVER     -- provides an OSC-over-UDP service
    INFO_OSC_TCP_SERVER     -- provides an OSC-over-TCP service
    INFO_OSC_TCP_CONNECTION -- an accepted OSC-over-TCP connection
    INFO_OSC_TCP_CONNECTING -- OSC client socket during connection
    INFO_OSC_TCP_CLIENT     -- client side OSC-over-TCP socket
    All process info records contain an index into o2_context->fds (and
      o2_context->fds_info and the index must be updated if a socket is moved.)
    If the tag is TCP_SOCKET or TCP_SERVER_SOCKET, fields are:
        name - the ip address:port number used as a service name
        status - PROCESS_DISCOVERED through PROCESS_OK
        udp_sa - a sockaddr_in structure for sending udp to this process
    If the tag is OSC_SOCKET or OSC_TCP_SERVER_SOCKET or OSC_TCP_SOCKET, 
        osc_service_name - name of the service to forward to


Sockets
-------

o2_context->fds_info has state to receive messages. Since reads may not
read the entire message, we collect incoming bytes into in_length for
the length count, and then in_message for the data. When a message is
completely received, there is a handler function that is called to 
process the message.
    For outgoing O2 messages, we have an associated process to tell
where to send.
    For incoming O2 messages, no extra info is needed; just deliver 
the message.
    For incoming OSC messages, fds_info has the process with the
        osc_service_name where messages are redirected.
Sockets are asynchronous, so writes may not complete immediately. For 
this reason, just as we keep byte counts and a buffer for incoming
messages, we keep a byte count and buffer for outgoing messages. In
fact, we allow a list of outgoing messages for cases where O2 wants
to send a sequence of messages. Users, on the other hand, are
allowed to have at most one pending message. After that, an attempt 
to send will block until the pending message is sent. Then, the new
message becomes pending. Users can detect the possibility of blocking
by calling o2_can_send(service).


Discovery
---------

Discovery messages are sent to discovery ports or TCP ports. 
Discovery ports come from a list of unassigned port numbers. Every 
process opens the first port on the list that is available as a receive 
port. The list is the same for every process, so each processes knows
what ports to send to. The only question is how many of up to 16
discovery ports do we need to send discovery messages to in order
to reach everyone? The answer is that if we receive on the Nth 
port in the list, we transmit to ports 1 through N. For any pair
of processes that are receiving on ports M and N, respectively,
assume, without loss of generality, that M > N. Since the first
process sends to ports 1 through M, it will send to port N and 
discovery will happen. The second process will not send to port
M, but the discovery protocol only relies on discovery happening
in one direction. 

The address for discovery messages is !_o2/dy, and the arguments are:
    hub flag (int32)
    ensemble name (string)
    local ip (string)
    tcp (int32)
    upd port (int32)
    sync (T or F)

Once a discovery message is received (usually via UDP), in either
direction, a TCP connection is established. Since the higher ip:port
string must be the server to prevent race conditions, the protocol is
a little more complicated if the server discovers the client. In that
case, the server makes a TCP connection and sends a discovery message
to the client. The client (acting temporarily as a server) then closes
the connection and makes a new connection (now acting as a client) 
back to the server. The client sends a discovery message again.

When a TCP connection is connected or accepted, the process sends the
UDP port number and a list of services to !_o2/sv. 

Process Creation
----------------

o2_discovery_handler() receives !_o2/dy message. There are two cases
based on whether the local host is the server or client. The server is
the host with the greater ip:port string. The client connects to the
server. If the server gets a discovery message from the client, it
can't connect because it's the server, so it merely generates an
!_o2/dy message to the client to prompt the client to connect. 

Info for each process is stored in fds_info, which has one entry per
socket. Most sockets are TCP sockets and the associated process info
pointed to by fds_info represents a remote process. However, the 
info associated with the TCP server port (every process has one of these)
represents the local process and is created when O2 is initialized. 
There are a few other sockets: UDP send socket, UDP 
receive socket, and OSC sockets (if created by the user).

The message sequence is:
    EITHER
Client broadcasts /dy (discovery) to all, including server.
Server sends /dy (discovery) to client via a TCP connection.
Client receives /dy over TCP, closes the TCP connection.
    OR
Server broadcasts /dy (discovery) to all, including client.
    THEN
Either way, the client now knows the server and connects to it:
    Locally, the client creates a service named "ip:port" representing
        the server by pointing to an o2n_info so that if another
        /dy message arrives, the client will not make another connection.
    Client connect()'s to server, creating TCP socket.
    Client sends /dy (discovery) message to server using the TCP socket.
    Client sends /cs (clocksync) to server using the TCP socket if
        client has clock sync.
    Client sends /sv (services) to server using the TCP socket.
Server accepts connect request, creating TCP socket.
    Locally, the server creates an O2 service named "IP:port"
        representing the client so that if another /dy message
        arrives, the server will not respond.
    Server sends /cs (clocksync) to client using the TCP socket if
        client has clock sync.
    Server sends /sv (services) to client using the TCP socket.
Since /dy messages are used in many ways, they carry a *dy* parameter
to help the receiver figure out how to interpret them. *dy* parameters
are shown below in the message flows.

Implementation using discovery (!o2_context->using_a_hub)
    o2_discovery_broadcast() - set up send/receive sockets, etc.
    o2_discovery_send_handler() - send next discovery message (/_o2/dy)
        o2_broadcast_message() - send one discovery message
            make_o2_dy_msg() - make the message to send
        o2_send_discovery_at() - resched. o2_discovery_send_handler() (/_o2/ds)
    o2_discovery_handler() - receives /o2/dy
        IF THIS IS THE SERVER:
            make_o2_dy_msg() - mkae the /_o2_dy message to send
            send the /_o2/dy message via UDP
        IF THIS IS THE CLIENT:
            o2_make_tcp_connection() - 
            o2_send_initialize() - send /o2/in
            o2_send_services() - send /o2/sv
            

Implementation using hub (o2_context->using_a_hub)
    o2_hub() - starts protocol
        o2_discovery_by_tcp() - connect to hub and start protocol
            IF THIS IS CLIENT:
                o2_send_initialize() - send /o2/in
                o2_send_services() - send /o2/sv
            IF THIS IS SERVER (requests HUB to contact us)
                make_o2_dy_msg() - make the /_o2/dy message to send
                o2_send_by_tcp() - send it by tcp
                o2_discovery_handler() - receives /_o2/dy
                    o2_discovery_by_tcp() - connect to requester
                        o2_send_initialize() - send /o2/in
                        o2_send_services() - send /o2/sv

Hubs
----
Discovery can also take place using the "hub" protocol. The idea is
that a process connects to another process's TCP port to start the 
discovery process. This requires at least one IP address and port
number to be shared by some means outside of O2, but it avoids
broadcasting, which might be disabled in the local network's
router. 

First consider what happens when o2_hub() is called. There are two
cases:

If the hub has the greater IP:Port string, the hub is the server.
In this case, the client can connect directly to the hub in the 
normal way with a /dy message. Normally, the server will not reply
with a /dy message because obviously the client has info on the
server already. In this case, however, the client does not know the
servers's UDP port number, so the client uses the *dy* flag to 
request a /dy message from the server. The server replies with
a /dy message that includes the server's UDP port number, 
and the number is saved by the client.

The o2n_info's uses_hub flag is set to O2_HUB_REMOTE. Then, the 
client sends to /_o2/hub (no parameters) which requests for the 
server to act as the client's hub. The hub sets it's uses_hub flag
for the client process to O2_I_AM_HUB and sends a discovery message
for every known process (except the hub and client) to the client. 

The client handles these discovery messages as if they arrived by 
UDP, thus discovering every known process. (Of course, processes  
discovered in the future are announced to the client via additional 
/dy messages.)

If the hub has the smaller IP:Port string, the hub is the client.
In this case, the non-hub connects to the hub, but only to deliver
a /dy (discovery) message. The client now has to wait for the hub
to drop the connection and make a new one from client (hub) to server.
Therefore, the client saves the hub IP:port string as pending. When
the hub connects to the server, the pending hub string matches and
the server sends to /_o2/hub on the client (the hub) and sets 
uses_hub to O2_HUB_REMOTE. The hub sets it's uses_hub flag for the 
server to O2_I_AM_HUB.

*dy* flags:
    O2_DY_INFO - I am hub, here is info on other processes OR
                 I am a process and I'm broadcasting my info to you
    O2_DY_HUB - I am client, you are server, send me a /dy
    O2_DY_REPLY - I am server, you are a connected client, here's my info
    O2_DY_CALLBACK - I am server, you are client, close this connection
                     and send back a /dy
    O2_DY_CONNECT - I am client, you are server, here's my info, do
                    not reply with another /dy
    

Some message flows are:
    
  o2_hub() called, hub is the server:
    non-hub (client)            hub (server)
         (client connects to server)
         ---/dy,dy=hub--------------->
         ---cs,/sv------------------->
         <--/dy,dy=reply--------------
         <--cs,/sv--------------------
         uses_hub=O2_HUB_REMOTE
         ---/hub--------------------->
                                uses_hub=O2_I_AM_HUB
         <--/dy,dy=info--------------- (in response to /hub,
         <--/dy,dy=info--------------- info for other processes)
         ...

  o2_hub() called, hub is the client:
    non-hub (server)            hub (client)
         (server connects to client)
         --/dy,dy=callback----------->
         (client closes socket and connects to server)
                                uses_hub=O2_I_AM_HUB
         <--/dy,dy=connect------------
         <--cs,/sv--------------------
         ---cs,/sv------------------->
         uses_hub=O2_HUB_REMOTE
         ---/hub--------------------->
                                uses_hub=O2_I_AM_HUB
         <--/dy,dy=info--------------- (in response to /hub,
         <--/dy,dy=info--------------- info for other processes)
         ...

  With normal discovery:
    (discovery message received by server)
    non-hub server         non-hub client
         <--/dy,dy=info------- (UDP)
         (server connects to client)
         ---/dy,dy=callback--> (by TCP)
         (client closes socket and connects to server)
         <--/dy,connect------- (by TCP)
         <--cs,/sv------------
         ---cs,/sv----------->


    (discovery message received by client)
    non-hub client         non-hub server
         <--/dy,dy=info------- (UDP)
         (client connects to server)
         ---/dy,dy=connect---> (by TCP)
         ---cs,/sv----------->
         <--cs,/sv------------


Non-blocking Behaviors
----------------------
We assume UDP does not block but rather drops packets. 
TCP sockets are set not to block so that send can fail
with EWOULDBLOCK (message not sent). When that happens,
the message is linked to the proc structure's pending_msg
field. It is sent as soon as the socket becomes writable.
While a message is pending, o2_can_send() will return
O2_BLOCKED. If the user does not check o2_can_send() and
attempts to send a message while another message is 
already pending, then o2_send() performs a blocking
send of the pending message, then performs a non-blocking
send of the current message.

In discovery, when connecting to a newly discovered
process, a non-blocking connect is performed. The order of
events is:
1. this is the server: send !_o2/dy message by UDP
2. this is the client side:
   A. non-blocking connect
      if connect returns success, raise an error
      normally connect returns EINPROGRESS
   B. socket becomes writable
   B. otherwise: send initialize, send services <- YOU ARE HERE



Services and Processes
----------------------
A process includes a list of services (strings). Each 
of these services is mapped by o2_context->path_tree to a 
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

Taps
----
A tap is similar to a service and a service_tap object
appears in the taps list for the tappee. The
service_tap contains the tapper's string name and the 
process (a o2n_info_ptr) of the tapper. Note that a
tapper is described by both process and service name, so
messages are delivered to that process, EVEN IF the service
is not the active one. For example, if the tap directs message
copies to "s1" of process P1, message copies will be sent there
even if a message directly to service "s1" would go to process
P2 (because "s1" is offered by both P1 and P2, but the IP:port
name of P2 is greater than that of P1. The main reason for this 
policy is that it allows taps to be removed when the process dies.
Without the process specification, it would be ambiguous who the
tap belongs to.


Remote Process Name: Allocation, References, Freeing
----------------------------------------------------
Each remote process has a name, e.g. "128.2.1.100:55765"
that can be used as a service name in an O2 address, e.g.
to announce a new local service to that remote process.
The name is on the heap and is "owned" by the process_info
record associated with the INFO_TCP_SOCKET socket for remote
processes. Also, the local process has its name owned by
the INFO_TCP_SERVER socket.

The process name is *copied* and used as the key for a
service_entry_ptr to represent the service in the 
o2_context->path_tree.

The process name is freed by o2_info_remove().

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

NODE_HASH and NODE_HANDLER keys at top level
----------------------------------------------------
If a hash_node (tag == NODE_HASH) or handler_entry
(tag == NODE_HANDLER) exists as a service provider, i.e.
in the services list of a services_entry record, the key 
field is not really needed because the service lookup takes
you to the services list, and the first element there offers
the service. E.g. if there's a (local) handler for 
/service1/bar, then we lookup "service1" in the o2_context->path_tree,
which takes us to a services_entry, the first element on its
services list should be a hash_node. There, we do a hash lookup
of "bar" to get to a handler_entry. The key is set to NULL for
hash_node and handler_entry records that represent services and
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
#include <signal.h>
#include <ctype.h>
#include "o2_internal.h"
#include "o2_search.h"
#include "o2_discovery.h"
#include "o2_message.h"
#include "o2_send.h"
#include "o2_sched.h"
#include "o2_clock.h"

#ifndef WIN32
#include <sys/time.h>
#endif

const char *o2_ensemble_name = NULL;
o2_context_t main_context;

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
    if (strchr(flags, 'h')) o2_debug |= O2_DBh_FLAG;
    if (strchr(flags, 't')) o2_debug |= O2_DBt_FLAG;
    if (strchr(flags, 'T')) o2_debug |= O2_DBT_FLAG;
    if (strchr(flags, 'm')) o2_debug |= O2_DBm_FLAG;
    if (strchr(flags, 'o')) o2_debug |= O2_DBo_FLAG;
    if (strchr(flags, 'O')) o2_debug |= O2_DBO_FLAG;
    if (strchr(flags, 'g')) o2_debug |= O2_DBg_FLAGS;
    if (strchr(flags, 'a')) o2_debug |= O2_DBa_FLAGS;
    if (strchr(flags, 'A')) o2_debug |= O2_DBA_FLAGS;
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

// these times are set when poll is called to avoid the need to
//   call o2_time_get() repeatedly
o2_time o2_local_now = 0.0;
o2_time o2_global_now = 0.0;

#ifndef O2_NO_DEBUG
void *o2_dbg_malloc(size_t size, const char *file, int line)
{
    O2_DBm(printf("%s malloc %lld in %s:%d", o2_debug_prefix, 
                  (long long) size, file, line));
    fflush(stdout);
    void *obj = (*o2_malloc)(size);
    O2_DBm(printf(" -> %p\n", obj));
    assert(obj);
    return obj;
}

void o2_dbg_free(const void *obj, const char *file, int line)
{
    O2_DBm(printf("%s free in %s:%d <- %p\n", 
                  o2_debug_prefix, file, line, obj));
    // bug in C. free should take a const void * but it doesn't
    (*free)((void *) obj);
}

/**
 * Similar to calloc, but this uses the malloc and free functions
 * provided to O2 through a call to o2_memory().
 * @param[in] n     The number of objects to allocate.
 * @param[in] size  The size of each object in bytes.
 *
 * @return The address of newly allocated and zeroed memory, or NULL.
 */
void *o2_dbg_calloc(size_t n, size_t s, const char *file, int line)
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
    memset(loc, 0, n * s);
    return loc;
}
#endif


void o2_context_init(o2_context_ptr context)
{
    o2_context = context;
    o2_context->hub[0] = 0; // set default condition, empty string
    o2_argv_initialize();
    o2_node_initialize(&o2_context->full_path_table, NULL);
    
}


static void o2_int_handler(int s)
{
    // debugging statement: race condition, should be removed:
    printf("O2 Caught signal %d\n", s);
    o2_finish(); // clean up ports
    exit(1);
}


int o2_initialize(const char *ensemble_name)
{
    int err;
    if (o2_ensemble_name) return O2_ALREADY_RUNNING;
    if (!ensemble_name) return O2_BAD_NAME;
    // Initialize the ensemble name.
    o2_ensemble_name = o2_heapify(ensemble_name);
    if (!o2_ensemble_name) {
        err = O2_NO_MEMORY;
        goto cleanup;
    }
    o2_context_init(&main_context);
    // Initialize the hash tables
    o2_node_initialize(&o2_context->path_tree, NULL);
    
    // before sockets, set up signal handler to try to clean up ports
    // in the event of a Control-C shutdown. This seems to leave ports
    // locked up forever in OS X, eventually requiring a reboot, so
    // anything we can to to free them is helpful.
    struct sigaction sig_int_handler;
    sig_int_handler.sa_handler = o2_int_handler;
    sigemptyset(&sig_int_handler.sa_mask);
    sig_int_handler.sa_flags = 0;
    sigaction(SIGINT, &sig_int_handler, NULL);
    // atexit will ignore the return value of o2_finish:
    atexit((void (*)(void)) &o2_finish);

    if ((err = o2n_initialize())) goto cleanup;
    
    o2_service_new2("_o2\000\000");
    o2_method_new("/_o2/dy", "ssiii", &o2_discovery_handler, 
                  NULL, FALSE, FALSE);
    o2_method_new("/_o2/hub", "", &o2_hub_handler, NULL, FALSE, FALSE);
    o2_method_new("/_o2/sv", NULL, &o2_services_handler, NULL, FALSE, FALSE);
    o2_method_new("/_o2/cs", "", &o2_clocksynced_handler, NULL, FALSE, FALSE);
    o2_method_new("/_o2/cs/rt", "s", &o2_clockrt_handler, NULL, FALSE, FALSE);
    o2_method_new("/_o2/ds", NULL, &o2_discovery_send_handler,
                  NULL, FALSE, FALSE);
    o2_clock_initialize();
    o2_sched_initialize();

    // Initialize discovery, which depends on clock and scheduler
    if ((err = o2_discovery_initialize())) goto cleanup;
    
    // a few things can be disabled after o2_initialize() and before
    // o2_poll()ing starts, so pick a time in the future and schedule them
    // They will then test to see if they should actually run or not.
    o2_time almost_immediately = o2_local_time() + 0.01;
    // clock sync messages startup, disabled by o2_clock_set()
    o2_clock_ping_at(almost_immediately);

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


// o2_hub() - this should be like a discovery message handler that 
//     just discovered a remote process, except we want to tell the
//     remote process that it is designated as our hub.
//
int o2_hub(const char *ipaddress, int port)
{
    // end broadcasting: see o2_discovery.c
    if (!ipaddress) {
        strncpy(o2_context->hub, ".", 32);
        return O2_SUCCESS; // NULL address -> just disable broadcasting
    }
    snprintf(o2_context->hub, 32, "%s:%d%c%c%c%c", ipaddress, port, 0, 0, 0, 0);
    o2_message_source = NULL; // not a real discovery message
    return o2_discovered_a_remote_process(ipaddress, port, 0, TRUE);
}


int o2_get_address(const char **ipaddress, int *port)
{
    if (!o2_found_network) 
        return O2_FAIL;
    *ipaddress = (const char *) o2_local_ip;
    *port = o2_local_tcp_port;
    return O2_SUCCESS;
}


/** notify all known processes that a service has been added or
 * deleted. If adding a service and tapper is not empty or null,
 * then the new service is tapper, which is tapping service_name.
 */
void o2_notify_others(const char *service_name, int added,
                      const char *tapper, const char *properties)
{
    if (!tapper) tapper = ""; // Make sure we have a strings to send.
    if (!properties) properties = "";
    // when we add or remove a service, we must tell all other
    // processes about it. To find all other processes, use the
    // o2_context->fds_info table since all but a few of the
    // entries are connections to processes
    for (int i = 0; i < o2_context->fds_info.length; i++) {
        o2n_info_ptr proc = GET_PROCESS(i);
        if (proc->tag == INFO_TCP_SOCKET) {
            o2_send_start();
            o2_add_string(o2_context->info->proc.name);
            o2_add_string(service_name);
            o2_add_tf(added);
            // last field in message is either the tapper or properties
            if (added && *tapper == 0) {
                o2_add_true();
                o2_add_string(properties);
            } else {
                o2_add_false();
                o2_add_string(tapper);
            }
            o2_message_ptr msg = o2_message_finish(0.0, "!_o2/sv", TRUE);
            if (!msg) return; // must be out of memory, no error is reported
            o2_send_by_tcp(proc, FALSE, msg);
            O2_DBd(printf("%s o2_notify_others sent %s to %s (%s) "
                          "tapper %s properties %s\n",
                          o2_debug_prefix, service_name, proc->proc.name, 
                          added ? "added" : "removed", tapper, properties));
        }
    }
}

/** find service in services offered by proc, if any */
o2_node_ptr o2_proc_service_find(o2n_info_ptr proc,
                                  services_entry_ptr services)
{
    if (!services) return FALSE;
    for (int i = 0; i < services->services.length; i++) {
        o2_node_ptr service = GET_SERVICE(services->services, i);
        if (TAG_IS_REMOTE(service->tag)) {
            if ((o2n_info_ptr) service == proc) {
                return service;
            }
        } else if (service->tag == INFO_OSC_TCP_CLIENT || service->tag == INFO_OSC_TCP_CONNECTING) {
            if (o2_context->info == proc) {
                return service;
            }
        } else if (service->tag == NODE_HASH ||
                   service->tag == NODE_HANDLER) { // must be local
            if (o2_context->info == proc) {
                return service; // local service already exists
            }
        }
    }
    return NULL;
}


/* adds a tap
 */
int o2_tap_new(o2string tappee, o2n_info_ptr process, o2string tapper)
{
    O2_DBd(printf("%s o2_tap_new adding tapper %s in %s to %s\n",
                  o2_debug_prefix, tapper, process->proc.name, tappee));
    services_entry_ptr ss = o2_must_get_services(tappee);

    // services exists, does the tap already exist? We could search
    // either the process's taps list or search the tappee's taps
    // for the process. Generally, a process will have few taps and
    // tappees will have few tappers, but a debugger or monitor might
    // have many taps (on the order of the total number of services
    // offered), and a service might have many tappers (on the order
    // of the number of processes if this is a central hub using a
    // taps to implement publish/subscribe). There's no clear winning
    // strategy. We'll search the service's list of taps:
    int i;
    for (i = 0; i < ss->taps.length; i++) {
        service_tap_ptr tap = GET_TAP(ss->taps, i);
        if (streql(tap->tapper, tapper) &&
            tap->proc == process) {
            return O2_SERVICE_EXISTS;
        }
    }

    // no matching tap found, so we should create one; taps are unordered
    DA_EXPAND(ss->taps, service_tap);
    service_tap_ptr tap = DA_LAST(ss->taps, service_tap);
    tap->tapper = o2_heapify(tapper);
    tap->proc = process;

    // install tap in the processs's list of taps:
    DA_EXPAND(process->proc.taps, proc_tap_data);
    proc_tap_data_ptr ptdp = DA_LAST(process->proc.taps, proc_tap_data);
    ptdp->services = ss;
    ptdp->tapper = tap->tapper;
    return O2_SUCCESS;
}


// search services list of tappee for matching tap info and remove it.
//
int o2_tap_remove(o2string tappee, o2n_info_ptr process, o2string tapper)
{
    O2_DBd(printf("%s o2_tap_remove tapper %s in %s tappee %s\n",
                  o2_debug_prefix, tapper, process->proc.name, tappee));

    services_entry_ptr ss = (services_entry_ptr)
            *o2_lookup(&o2_context->path_tree, tappee);
    if (!ss) return O2_FAIL;

    return o2_tap_remove_from(ss, process, tapper);
}


/* adds a service provider - a service is added to the list of services in 
 *    a service_entry struct.
 * 1) create the service_entry struct if none exists
 * 2) put the service onto process's list of service names
 * 3) add new service to the list
 *
 * CASE 1: this is a new local service
 *
 * CASE 2: this is the installation of /ip:port for a newly discovered
 *         remote process. service == process
 *
 * CASE 3: this is creating a service that delegates to OSC. service is
 *         an osc_info_ptr, process is the local process
 *
 * CASE 4: handling /ip:port/sv: service is a o2n_info_ptr equal to
 *         process.
 */
int o2_service_provider_new(o2string service_name, const char *properties,
                            o2_node_ptr service, o2n_info_ptr process)
{
    O2_DBd(printf("%s %s o2_service_provider_new adding %s to %s\n",
                  o2_debug_prefix,
                  // highlight when proc.name is our IP:Port info:
                  (streql(service_name, "_o2") ? "****" : ""),
                  service_name, process->proc.name));
    services_entry_ptr ss = o2_must_get_services(service_name);
    // services exists, does this service already exist?
    if (o2_proc_service_find(process, ss) != NULL) {
        O2_DBd(printf("%s o2_service_provider_new service exists %s\n",
                      o2_debug_prefix, service_name));
        return O2_SERVICE_EXISTS;
    }

    // Now we know it's safe to add a service and we have a place to put it

    // add the service name and properties to the process service list
    DA_EXPAND(process->proc.services, proc_service_data);
    proc_service_data_ptr psdp =
            DA_LAST(process->proc.services, proc_service_data);
    psdp->services = ss;
    if (properties && *properties) { // not NULL, not empty
        char *p = O2_MALLOC(strlen(properties) + 2);
        p[0] = ';';
        strcpy(p + 1, properties);
        psdp->properties = p;
    } else {
        psdp->properties = NULL;
    }
    // find insert location: either at front or at back of services->services
    DA_EXPAND(ss->services, o2_node_ptr);
    int index = ss->services.length - 1;
    // new service will go into services at index
    if (index > 0) { // see if we should go first
        o2string our_ip_port = process->proc.name;
        // find the top entry
        o2_node_ptr top_entry = GET_SERVICE(ss->services, 0);
        o2string top_ip_port = (top_entry->tag == INFO_TCP_SERVER ?
                                o2_context->info->proc.name :
                                ((o2n_info_ptr) top_entry)->proc.name);
        if (strcmp(our_ip_port, top_ip_port) > 0) {
            // move top entry from location 0 to end of array at index
            DA_SET(ss->services, o2_node_ptr, index, top_entry);
            index = 0; // put new service at the top of the list
        }
    }
    // index is now indexing the first or last of services
    DA_SET(ss->services, o2_node_ptr, index, service);
    // special case for osc: need service name
    if (service->tag == NODE_OSC_REMOTE_SERVICE) {
        ((osc_info_ptr) service)->service_name = ss->key;
    }
    if (index == 0) {
        // we have a new service, so report it to the local process
        // /si msg needs: *service_name* *status* *process-name*
        const char *process_name;
        int status = o2_status_from_info(service, &process_name);
        // process_name can be NULL if this is a new process connection,
        // so use the service_name which is the ip:port string we want
        if (!process_name) {
            process_name = service_name;
        }
        o2_send_cmd("!_o2/si", 0.0, "sis", service_name, status, process_name);
    }
    return O2_SUCCESS;
}


int o2_service_new(const char *service_name)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    if (!isalpha(service_name[0])) {
        return O2_BAD_NAME;
    }
    if (strchr(service_name, '/')) {
        return O2_BAD_SERVICE_NAME;
    }
    char padded_name[NAME_BUF_LEN];
    o2_string_pad(padded_name, service_name);
    return o2_service_new2(padded_name);
}


// internal implementation of o2_service_new, assumes valid
// service name with zero padding
int o2_service_new2(o2string padded_name)
{
    // find services_node if any
    hash_node_ptr node = o2_hash_node_new(NULL);
    if (!node) return O2_FAIL;

    // this will send /_o2/si message to local process:
    int rslt = o2_service_provider_new(padded_name, NULL, (o2_node_ptr) node,
                                       o2_context->info);
    if (rslt != O2_SUCCESS) {
        O2_FREE(node);
        return rslt;
    }
    o2_notify_others(padded_name, TRUE, NULL, NULL);

    return O2_SUCCESS;
}


int o2_tap(const char *tappee, const char *tapper)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    char padded_tappee[NAME_BUF_LEN];
    char padded_tapper[NAME_BUF_LEN];
    o2_string_pad(padded_tappee, tappee);
    o2_string_pad(padded_tapper, tapper);
    int err = o2_tap_new(padded_tappee, o2_context->info, padded_tapper);
    if (err == O2_SUCCESS) {
        o2_notify_others(padded_tappee, TRUE, padded_tapper, NULL);
    }
    return err;
}


int o2_untap(const char *tappee, const char *tapper)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    int err = o2_tap_remove(tappee, o2_context->info, tapper);
    if (err == O2_SUCCESS) {
        o2_notify_others(tappee, FALSE, tapper, NULL);
    }
    return err;
}

/* DEBUGGING:
static void check_messages()
{
    for (int i = 0; i < O2_SCHED_TABLE_LEN; i++) {
        for (o2_message_ptr msg = o2_ltsched.table[i]; msg; msg = msg->next) {
            assert(msg->allocated >= msg->length);
        }
    }
}
*/

int o2_poll()
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    // DEBUGGING: check_messages();
    o2_local_now = o2_local_time();
    if (o2_gtsched_started) {
        o2_global_now = o2_local_to_global(o2_local_now);
    } else {
        o2_global_now = -1.0;
    }
    o2_sched_poll(); // deal with the timestamped message
    o2n_recv(); // receive and dispatch messages
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


// helper function for o2_status() and finding status.
// 
int o2_status_from_info(o2_node_ptr entry, const char **process)
{
    if (!entry) return O2_FAIL;
    switch (entry->tag) {
        case INFO_TCP_NOCLOCK:
        case INFO_TCP_SOCKET: {
            o2n_info_ptr info = (o2n_info_ptr) entry;
            if (info->net_tag == NET_TCP_CONNECTING) {
                if (process) *process = NULL;
                return O2_FAIL;
            }
            if (process) {
                *process = info->proc.name;
            }
            if (o2_clock_is_synchronized &&
                info->tag == INFO_TCP_SOCKET) {
                return O2_REMOTE;
            } else {
                return O2_REMOTE_NOTIME;
            }
        }
        case NODE_HASH:
        case NODE_HANDLER:
            if (process)
                *process = o2_context->info->proc.name;
            return (o2_clock_is_synchronized ? O2_LOCAL : O2_LOCAL_NOTIME);
        case NODE_BRIDGE_SERVICE:
        default:
            if (process)
                *process = NULL;
            return O2_FAIL; // not implemented yet or it's a NODE_TAP or not connected
        case NODE_OSC_REMOTE_SERVICE: // no timestamp synchronization with OSC
            if (process)
                *process = o2_context->info->proc.name;
            if (o2_clock_is_synchronized) {
                return O2_TO_OSC;
            } else {
                return O2_TO_OSC_NOTIME;
            }
    }
}


int o2_status(const char *service)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    if (!service || !*service || strchr(service, '/') || strchr(service, '!'))
        return O2_BAD_SERVICE_NAME;
    services_entry_ptr services;
    o2_node_ptr entry = o2_service_find(service, &services);
    return o2_status_from_info(entry, NULL);
}


int o2_can_send(const char *service)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    if (!service || !*service || strchr(service, '/') || strchr(service, '!'))
        return O2_BAD_SERVICE_NAME;
    services_entry_ptr services;
    o2_node_ptr entry = o2_service_find(service, &services);
    if (!entry) {
        return O2_FAIL;
    }
    if (entry->tag == INFO_TCP_SOCKET) {
        o2n_info_ptr proc = (o2n_info_ptr) entry;
        return (proc->out_message ? O2_BLOCKED : O2_SUCCESS);
    } else if (entry->tag == NODE_OSC_REMOTE_SERVICE) {
        osc_info_ptr oip = (osc_info_ptr) entry;
        if (oip->tcp_socket_info) {
            return (oip->tcp_socket_info->out_message ? O2_BLOCKED : O2_SUCCESS);
        }
    }
    return O2_SUCCESS;
}


#ifdef WIN32
int gettimeofday(struct timeval * tp, struct timezone * tzp)
{
    // Note: some broken versions only have 8 trailing zero's, the correct 
    //       epoch has 9 trailing zero's
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
    if (!o2_ensemble_name) { // see if we're running
        return O2_NOT_INITIALIZED;
    }
    if (o2n_socket_delete_flag) {
        // we were counting on o2_recv() to clean up some sockets, but
        // it hasn't been called
        o2n_free_deleted_sockets();
    }
    // Close all the sockets.
    if (o2_context) {
        for (int i = 0 ; i < o2_context->fds.length; i++) {
            printf("o2_finish calling o2_info_remove on %d\n", i);
            o2_info_remove(GET_PROCESS(i));
        }
        o2n_free_deleted_sockets(); // deletes process_info structs
        o2_node_finish(&o2_context->path_tree);
        o2_node_finish(&o2_context->full_path_table);
        o2_argv_finish();
    }
    o2n_finish();

    o2_sched_finish(&o2_gtsched);
    o2_sched_finish(&o2_ltsched);
    o2_discovery_finish();
    o2_clock_finish();

    O2_FREE((void *) o2_ensemble_name);
    o2_ensemble_name = NULL;
    // we assume that o2_context is statically allocated, not on heap
    o2_context = NULL;
    return O2_SUCCESS;
}
