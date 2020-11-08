/* o2.c - manage o2 processes and their service lists */

/* Roger B. Dannenberg
 * April 2020
 */

/*
Design Notes
============
o2_ctx->fds
         o2_ctx->fds_info              +---------+ Not all sock nodes
+----+   +---------+    +----------+   |  sock   | are-service entries.
|    |   |        -+--->| o2n_info |<->|  info   | One sock node is the
|----|   |         |    +----------+   +---------+ local process.
|    |   |---------|     +----------+
|----|   |        -+---->| o2n_info |<-----------------------------------+
|    |   |         |     +----------+             OSC message forwarding |
+----+   |---------|      +----------+      (delegated service is listed |
         |        -+----->| o2n_info |            in services offered by |
         |         |      +----------+                   local process.) |
         +---------+            ^                                        |
                                |                   +-----------------+  |
 o2_ctx->                       V                +->| bridge_protocol |  |
  path_tree                   +---------+        |  +-----------------+  |
+----------+    +---------+   |proc_info|        |                       |
|         -+--->|services_+-->|         |        |                       |
+----------+    | entry   |   +---------+   +--------+                   |
|         -+->  |(contains+---------------->| bridge |                   |
+----------+    |service  |                 |  inst  |                   |
                |name as  |   +---------+   +--------+                   |
                |key)    -+-->|(local) -+---> etc                        |
                |         |   | hash_   |  +---------+                   |
                |        -+-+ | node   -+->| handler_|                   |
                +---------+ | +---------+  |  entry  |   +----------+    |
                |also con-| |              +---------+   |  sock    |    |
                |tains an | +--------------------------->|SOCK_OSC_ +<---+
                |array of |                              |TCP_CLIENT|
                |service_ |    +->tapper (o2string)      +----------+
                |taps     |    |
                |        =+====+->to o2n_info (tapper is specific
                +---------+           to that process)

(Edit with: stable.ascii-flow.appspot.com/#Draw)

Note: o2_ctx->path_tree is a hash_node (hash table)
hash_node (hash table) entries can be:
    hash_node - the next node in a path
    handler_entry - handler at end of path
    bridge_inst - messages are handled by passing to a
        function that can be installed to support different
        transports.
    services_entry - in o2_ctx->path_tree only, a list
        of services of the same name, the highest
        IP:Port string overrides any others.


o2_ctx->full_path_table
+----------+             +--------+
|         -+------------>| handler|
+----------+  +--------+ |  node  |
|         -+->| handler| +--------+
+----------+  | node   |
              +--------+

More detail on bridges and o2lite bridges:



      bridges            bridge_protocol
      +-------+          +------------------+
      |     --+--------->|protocol          |
      +-------+          |bridge_poll       |
      |     --+-------+  |bridge_send       |
      +-------+       |  |bridge_recv       |
      |       |       |  |bridge_inst_finish|
      +-------+       |  |bridge_finish     |
      |       |       |  +------------------+
      +-------+       |
                      |                bridge_protocol
   o2lite_bridges     |                +------------------+
      +-------+       +--------------->|protocol "o2lite" |
      |       |                   +--->|...               |
      +-------+                   | +->|                  |
      |       |                   | |  +------------------+
      +-------+                   | |
      |       |       bridge_inst | |    o2lite_inst
      +-------+       +---------+ | |    +------------+
      |     --+------>|proto  --+-+ |    |net_info  --+-----+
      +-------+       |info   --+------->|udp_address |     |
                   +->|         |   | +->|            |     |
  net_info for     |  +---------+   | |  +------------+     |
  accepted TCP     |                | |                     |
  connection       |                | |                     |
  +--------------+ |  bridge_inst   | | For each service,   |
  |...           | |  +---------+   | | the bridge_inst     |
  |application --+-+  |proto  --+---+ | pointed to by a     |
  |              |    |info   --+-----+ net_info (TCP sock) |
  +--------------+    |         |       is copied. Copy is  |
         ^            +---------+       owned by a service  |
         |                ^             entry. The o2lite_  |
         |                |             inst is shared with |
         |  services array entry        services and owned  |
         |    for some service          by the net_info's   |
         |   offered by a bridge        bridge_inst.        |
         |                                                  |
         +--------------------------------------------------+

Each process has:
o2_ctx->path_tree - a dictionary mapping service names to a
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
            If O2_NO_PATTERNS, there are no hash_nodes here; instead,
            an o2_node with tag NODE_HASH serves to redirect the
            search to the global hash table of full method addresses.
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
        bridge_inst: service is remote but reached by an alternate
            transport (not the O2 IP protocol, but could be IP)

    The first element in the array of entries in a service_entry 
    is the "active" service -- the one with the highest IP:port
    string. Other elements are not sorted, so when a service is 
    removed, a linear search to find the largest remaining offering
    (if any) is performed.

    The o2_ctx->path_tree also maps IP addresses + ports (as strings
    that begin with a digit and have the form 128.2.100.120:4000) 
    to a services_entry that contains one remote_service_entry.

o2_ctx->full_path_table is a dictionary for full paths, permitting a 
    single hash table lookup for addresses of the form !synth/lfo/freq.
    In practice an additional lookup of just the service name is 
    required to determine if the service is local and if there is a 
    single handler for all messages to that service.

Each handler object is referenced by some node in the o2_ctx->path_tree
    and by the o2_ctx->full_path_table dictionary, except for handlers
    that handle all service messages. These are only referenced by the 
    o2_ctx->path_tree.

o2_ctx->fds is a dynamic array of sockets for poll
o2_ctx->fds_info is a parallel dynamic array of pointers to o2n_info
 o2n_info includes a net_tag, and points to a proc_info or osc_info with
 a tag (see processes.h for more detail and connection life-cycles):
    NET_UDP_SERVER         -- a socket for receiving UDP
    NET_TCP_SERVER         -- the local process
    NET_TCP_CLIENT         -- tcp client
    NET_TCP_CONNECTED      -- accepted by tcp server
    NET_UDP_SERVER         -- UDP receive socket for this process
    All process info records contain an index into o2_ctx->fds (and
      o2_ctx->fds_info and the index must be updated if a socket is moved.)
    If the net_tag is NET_TCP_* or NET_UDP_*, the application pointer 
        is a proc_info_ptr
    If the tag is NET_OSC_*, the application pointer is an osc_info_ptr


Sockets
-------

o2_ctx->fds_info has state to receive messages. Since reads may not
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
assume, without loss of generality, that M >= N. Since the first
process sends to ports 1 through M, it will send to port N and 
discovery will happen. The second process may not send to port
M, but the discovery protocol works with either direction.

A possible optimization is the following: After discovery, processes
exchange local service information through !_o2/sv messages. We could
include remote process names as well since these are service names in
the form IP:Port, identifiable by a leading digit. For each of these,
the receiver could check to see if the service exists. If not, it
could treat this as a newly discovered process. On the down side, the
N^2 !_o2/sv messages now grow to contain O(N) services (each), making
the data exchanged through discovery grow to N^3.

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
based on whether the local host is the server or client. (The server is
the host with the greater ip:port string. The client connects to the
server.) If the server gets a discovery message from the client, it
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
    Client sends /cs/cs (clocksync) to server using the TCP socket if
        client has clock sync.
    Client sends /sv (services) to server using the TCP socket.
Server accepts connect request, creating TCP socket.
    Locally, the server creates an O2 service named "IP:port"
        representing the client so that if another /dy message
        arrives, the server will not respond.
    Server sends /cs/cs (clocksync) to client using the TCP socket if
        client has clock sync.
    Server sends /sv (services) to client using the TCP socket.
Since /dy messages are used in many ways, they carry a *dy* parameter
to help the receiver figure out how to interpret them. *dy* parameters
are shown below in the message flows.

Implementation using discovery (!o2_ctx->using_a_hub)
    o2_discovery_broadcast() - set up send/receive sockets, etc.
    o2_discovery_send_handler() - send next discovery message (/_o2/dy)
        o2_broadcast_message() - send one discovery message
            o2_make_dy_msg() - make the message to send
        o2_send_discovery_at() - resched. o2_discovery_send_handler() (/_o2/ds)
    o2_discovery_handler() - receives /o2/dy
        o2_discovered_a_remote_process()
            ignore message if we process is already discovered
            o2_create_tcp_proc(PROC_TEMP, ip, tcp); (null name,
                    might change tag later)
            IF THIS IS THE SERVER:
                o2_make_dy_msg() - make the /_o2_dy message to send
                send the /_o2/dy message via TCP using PROC_TEMP
            IF THIS IS THE CLIENT:
                send O2_DY_CONNECT reply
                o2_send_clocksync_proc()
                o2_send_services() - send /o2/sv
                o2n_address_init() to set up outgoing UDP address
            

Implementation using hub (o2_ctx->using_a_hub)
    o2_hub() - starts protocol
        o2_discovered_a_remote_process() -- see description above

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
         ---/dy,dy=O2_DY_HUB--------->
         ---cs,/sv------------------->
         <--/dy,dy=O2_DY_REPLY--------
         <--cs,/sv--------------------
         uses_hub=O2_HUB_REMOTE
         ---/hub--------------------->
                                uses_hub=O2_I_AM_HUB
         <--/dy,dy=O2_DY_INFO--------- (in response to /hub,
         <--/dy,dy=O2_DY_INFO--------- info for other processes)
         ...

  o2_hub() called, hub is the client:
    non-hub (server)            hub (client)
         (server connects to client)
         --/dy,dy=O2_DY_CALLBACK----->
         (client closes socket and connects to server)
                                uses_hub=O2_I_AM_HUB
         <--/dy,dy=O2_DY_CONNECT------
         <--cs,/sv--------------------
         ---cs,/sv------------------->
         uses_hub=O2_HUB_REMOTE
         ---/hub--------------------->
                                uses_hub=O2_I_AM_HUB
         <--/dy,dy=O2_DY_INFO--------- (in response to /hub,
         <--/dy,dy=O2_DY_INFO--------- info for other processes)
         ...

  With normal discovery:
    (discovery message received by server)
    non-hub server         non-hub client
         <--/dy,dy=O2_DY_INFO----- (UDP)
         (server connects to client)
         --/dy,dy=O2_DY_CALLBACK-> (BY TCP)
         (CLIENT closes socket and connects to server)
         <--/dy,dy=O2_DY_CONNECT-- (by TCP)
         <--cs,/sv----------------
         ---cs,/sv--------------->


    (discovery message received by client)
    non-hub client         non-hub server
         <--/dy,dy=O2_DY_INFO--- (UDP)
         (client connects to server)
         ---/dy,dy=O2_DY_CONNECT-> (by TCP)
         ---cs,/sv--------------->
         <--cs,/sv----------------


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

To facilitate sending multiple messages (discovery and
service information for example), o2_net code allows you
to queue up multiple messages to send. This feature is
only available to internal O2 functions and is only used
when a connection is created. If the user process tries
to send a message when there are already message(s) in
the queue, the messages are sent synchronously and the
user blocks until all pending messages are sent (or at 
least they are accepted by the kernel for delivery).

Services and Processes
----------------------
A process includes a list of services (strings). Each 
of these services is mapped by o2_ctx->path_tree to a 
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
The name is on the heap and is "owned" by the proc_info
record associated with the NET_TCP_* socket for remote
processes. Also, the local process has its name owned by
context->proc.

The process name is *copied* and used as the key for a
service_entry_ptr to represent the service in the 
o2_ctx->path_tree.

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
OSC_TCP_SERVER and copied to any OSC_TCP_CONNECTION
that was accepted from the server socket.  These sockets 
and their osc service name are freed when their sockets are
deleted.

NODE_HASH and NODE_HANDLER keys at top level
----------------------------------------------------
If a hash_node (tag == NODE_HASH) or handler_entry
(tag == NODE_HANDLER) exists as a service provider, i.e.
in the services list of a services_entry record, the key 
field is not really needed because the service lookup takes
you to the services list, and the first element there offers
the service. E.g. if there's a (local) handler for 
/service1/bar, then we lookup "service1" in the o2_ctx->path_tree,
which takes us to a services_entry, the first element on its
services list should be a hash_node. There, we do a hash lookup
of "bar" to get to a handler_entry. The key is set to NULL for
hash_node and handler_entry records that represent services and
are directly pointed to from a services list.

Bridge and O2lite: Allocation, References, Freeing
-------------------------------------------------
Structures can be deleted in many ways, creating a challenge
to avoid memory leaks. Each path for deletion is considered:

Service Deletion. (Specifically, the removal of a service provider
from a services list.) Since a bridge_inst is allocated per service,
just delete the service's bridge_inst. This done by o2_node_free,
which calls the bridge_protocol's bridge_inst_finish method. For
o2lite, this points to o2lite_inst_finish, which checks if this
bridge_inst is the one that belongs to the TCP socket. (Refer to the
diagram above, this is tricky.) Follow the bridge_inst->info pointer
to an o2lite_inst; follow the o2lite_inst->net_info to the TCP
socket's net_info. Follow net_info->application to a bridge_inst. If
we come back to the same bridge_inst, then this is the one owned by
the TCP connection. But since we are deleting a service provider, we
will NOT come back to the same bridge_inst, and we do nothing. Let
o2_node_free free the bridge_inst which is the servicde provider.

net_info Deletion. If a connection is lost, we must delete the
net_info->application which is a bridge_inst. This is performed by
net_info_free. The previous paragraph describes how, assuming an
o2lite bridge, this calls o2lite_inst_finish, and how it determines
this bridge_inst belongs to the net_info.  Every service provider with
a matching bridge_protocol (proto) and info is removed. The net_info represents the connection to the remote o2lite process, so the socket is closed,
the o2lite_inst is freed and the entry in o2lite_bridges is set to
NULL (but not compacted: the index of each o2lite_inst in
o2lite_bridges represent the o2lite unique ID number.)

bridge Deletion. An entire bridge protocol can be deleted. To do this,
we use o2_bridge_remove_services to remove every service offering by
this protocol.  Then call the protocol's bridge_finish to finalize any
structures in use by the bridge; e.g. o2lite must find and close TCP
ports with bridge connections by using o2lite_bridges. Finally, free
the bridge_protocol and remove it from bridges.

bridges Deletion. When o2_finish is called, we need to delete all
bridges. Delete each bridge_protocol in bridges. Then free the bridges
array.

Byte Order
----------
Messages are constructed and delivered in local host byte order.
When messages are sent to another process, the bytes are swapped
if necessary to be in network byte order. Messages arriving by
TCP or UDP are therefore in network byte order and are converted
to host byte order if necessary.

Memory Leaks
------------
Since the user can exit in the middle of message delivery, and
possibly even in the middle of nested message delivery because of
duplicate messages being sent to taps, we can have in-flight
messages that are not freed even when o2_finish() frees all the
data structures. To complete the cleanup, we keep all in-flight
messages not in some other structure in the list o2_ctx->msgs.

Since this is global, we often treat o2_ctx->msgs as the "active"
message and consider it to be an implied parameter for many delivery
functions.

*/


/**
 *  TO DO:
 *  see each error return has the right error code
 *  tests that generate error messages
 */

// to define sig_int_handler and usleep, we need these macros:
#include "o2usleep.h"
#include <stdio.h>
#include <signal.h>
#include <ctype.h>
#include "unistd.h"

#include "o2internal.h"
#include "o2mem.h"
#include "services.h"
#include "o2osc.h"
#include "bridge.h"
#include "message.h"
#include "msgsend.h"
#include "o2sched.h"
#include "clock.h"
#include "discovery.h"
#include "properties.h"
#include "pathtree.h"

#ifndef WIN32
#include <sys/time.h>
#endif

const char *o2_ensemble_name = NULL;
o2_ctx_t main_context;
char o2_hub_addr[O2_MAX_PROCNAME_LEN];

// these times are set when poll is called to avoid the need to
//   call o2_time_get() repeatedly
o2_time o2_local_now = 0.0;
o2_time o2_global_now = 0.0;
o2_time o2_global_offset = 0.0;


void o2_ctx_init(o2_ctx_ptr context)
{
    o2_ctx = context;
    o2_argv_initialize();
    o2_node_initialize(&o2_ctx->full_path_table, NULL);
    // Initialize the hash tables
    o2_node_initialize(&o2_ctx->path_tree, NULL);
    o2_ctx->msgs = NULL;
    o2_ctx->warning = &o2_message_drop_warning;
}


static void o2_int_handler(int s)
{
    // debugging statement: race condition, should be removed:
    printf("O2 Caught signal %d\n", s);
    o2_finish(); // clean up ports
    exit(1);
}


o2_err_t o2_initialize(const char *ensemble_name)
{
    o2_err_t err;
    o2_time almost_immediately;
    // this is a bit tricky: o2_mem_init depends upon o2_ctx, but
    // o2_ctx_init() calls on O2_MALLOC. The next line is enough to
    // allow o2_mem_init() to run, and we call o2_ctx_init() for
    // the full initialization later:
    o2_ctx = &main_context;
    // this does nothing if o2_memory() was called previously:
    o2_mem_init(NULL, 0);
    if (o2_ensemble_name) return O2_ALREADY_RUNNING;
    if (!ensemble_name) return O2_BAD_NAME;
    // Initialize the ensemble name.
    o2_ensemble_name = o2_heapify(ensemble_name);
    if (!o2_ensemble_name) {
        err = O2_NO_MEMORY;
        goto cleanup;
    }
    o2_ctx_init(&main_context);
    
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

    if ((err = o2n_initialize(o2_message_deliver, o2_net_accepted,
                              o2_net_connected, o2_net_info_remove))) {
        goto cleanup;
    }

    o2_clock_initialize();
    o2_sched_initialize();

    // Initialize discovery, which depends on clock and scheduler
    if ((err = o2_discovery_initialize())) {
        goto cleanup;
    }
    if ((err = o2_processes_initialize())) {
        goto cleanup;
    }
    // o2_service_new2(o2_ctx->proc->name);
    o2_service_new2("_o2\000\000");
    o2_discovery_initialize2();
    o2_clock_initialize2(); // install handlers for clock sync

    // a few things can be disabled after o2_initialize() and before
    // o2_poll()ing starts, so pick a time in the future and schedule them
    // They will then test to see if they should actually run or not.
    almost_immediately = o2_local_time() + 0.01;
    // clock sync messages startup, disabled by o2_clock_set()
    o2_clock_ping_at(almost_immediately);

    return O2_SUCCESS;
  cleanup:
    o2_finish();
    return err;
}


o2_err_t o2_get_address(const char **ipaddress, int *port)
{
    if (!o2n_found_network || !o2_ctx || !o2_ctx->proc)
        return O2_FAIL;
    *ipaddress = (const char *) o2n_local_ip;
    *port = o2_ctx->proc->net_info->port;
    return O2_SUCCESS;
}


const char *o2_get_ip_port_string()
{
    if (!o2n_found_network || !o2_ctx || !o2_ctx->proc)
        return NULL;
    return o2_ctx->proc->name;
}

static void send_one_sv_msg(proc_info_ptr proc, const char *service_name,
                   int added, const char *tapper, const char *properties)
{
    o2_send_start();
    o2_add_string(o2_ctx->proc->name);
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
    o2_message_ptr msg = o2_message_finish(0.0, "!_o2/sv", true);
    if (!msg) return; // must be out of memory, no error is reported
    o2_prepare_to_deliver(msg);
    o2_send_remote(proc, false);
    O2_DBd(printf("%s o2_notify_others sent %s to %s (%s) "
                  "tapper %s properties %s\n",
                  o2_debug_prefix, service_name, proc->name,
                  added ? "added" : "removed", tapper, properties));
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
    // o2_ctx->fds_info table since all but a few of the
    // entries are connections to processes
    int i = 0;
    o2n_info_ptr info;
    while ((info = o2n_get_info(i++))) {
        proc_info_ptr proc = (proc_info_ptr) (info->application);
        if (proc && IS_REMOTE_PROC(proc)) {
            send_one_sv_msg(proc, service_name, added, tapper, properties);
        }
    }
#ifndef O2_NO_MQTT
    for (i = 0; i < o2_mqtt_procs.length; i++) {
        send_one_sv_msg(DA_GET(o2_mqtt_procs, proc_info_ptr, i),
                        service_name, added, tapper, properties);
    }
#endif
}


/* adds a tap -
 */
o2_err_t o2_tap_new(o2string tappee, proc_info_ptr proc, const char *tapper)
{
    O2_DBd(printf("%s o2_tap_new adding tapper %s in %s to %s\n",
                  o2_debug_prefix, tapper, proc->name, tappee));
    services_entry_ptr ss = o2_must_get_services(tappee);

    // services exists, does the tap already exist?
    // search the service's list of taps:
    int i;
    for (i = 0; i < ss->taps.length; i++) {
        service_tap_ptr tap = GET_TAP_PTR(ss->taps, i);
        if (streql(tap->tapper, tapper) &&
            tap->proc == proc) {
            return O2_SERVICE_EXISTS;
        }
    }

    // no matching tap found, so we should create one; taps are unordered
    tapper = o2_heapify(tapper);
    return o2_services_insert_tap(ss, tapper, proc);
}


// search services list of tappee for matching tap info and remove it.
//
o2_err_t o2_tap_remove(o2string tappee, proc_info_ptr proc,
                         const char *tapper)
{
    O2_DBd(printf("%s o2_tap_remove tapper %s in %s tappee %s\n",
                  o2_debug_prefix, tapper, proc->name, tappee));

    services_entry_ptr ss = (services_entry_ptr)
            *o2_lookup(&o2_ctx->path_tree, tappee);
    if (!ss) return O2_FAIL;

    return o2_tap_remove_from(ss, proc, tapper);
}


o2_err_t o2_service_new(const char *service_name)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    if (!service_name || !isalpha(service_name[0]) ||
        strchr(service_name, '/')) {
        return O2_BAD_SERVICE_NAME;
    }
    char padded_name[NAME_BUF_LEN];
    o2_string_pad(padded_name, service_name);
    return o2_service_new2(padded_name);
}


void o2_message_drop_warning(const char *warn, o2_msg_data_ptr msg)
{
    printf("Warning: %s,\n    message is ", warn);
#ifdef O2_NO_DEBUG
    printf("%s (%s)", msg->address, O2_MSGDATA_TYPES(msg));
#else
    o2_msg_data_print(msg);
#endif
    printf("\n");
}


void o2_message_warnings(
        void (*warning)(const char *warn, o2_msg_data_ptr msg))
{
    o2_ctx->warning = warning;
}


o2_err_t o2_method_new(const char *path, const char *typespec,
                       o2_method_handler h, const void *user_data,
                       bool coerce, bool parse)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    if (!path || path[0] == 0 || path[1] == 0 || path[0] != '/' ||
        (!isalpha(path[1]) && !streql(path, "/_o2/si"))) {
        return O2_BAD_SERVICE_NAME;
    }
    return o2_method_new_internal(path, typespec, h, user_data, coerce, parse);
}


o2_err_t o2_tap(const char *tappee, const char *tapper)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    char padded_tappee[NAME_BUF_LEN];
    o2_string_pad(padded_tappee, tappee);
    o2_err_t err = o2_tap_new(padded_tappee, o2_ctx->proc, tapper);
    if (err == O2_SUCCESS) {
        o2_notify_others(padded_tappee, true, tapper, NULL);
    }
    return err;
}


o2_err_t o2_untap(const char *tappee, const char *tapper)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    char padded_tappee[NAME_BUF_LEN];
    o2_string_pad(padded_tappee, tappee);
    o2_err_t err = o2_tap_remove(padded_tappee, o2_ctx->proc, tapper);
    if (err == O2_SUCCESS) {
        o2_notify_others(padded_tappee, false, tapper, NULL);
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

o2_err_t o2_poll()
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    // DEBUGGING: check_messages();
    o2_local_now = o2_local_time();
    if (o2_gtsched_started) {
        o2_global_now = o2_local_to_global(o2_local_now);
        // offset can be used by a shared memory process
        o2_global_offset = o2_global_now - o2_local_now;
    } else {
        o2_global_now = -1.0;
    }
    o2_sched_poll(); // deal with the timestamped message
    o2n_recv(); // receive and dispatch messages
#ifndef O2_NO_BRIDGES
    o2_poll_bridges();
#endif
    o2_deliver_pending();
    return O2_SUCCESS;
}


bool o2_stop_flag = false;

int o2_run(int rate)
{
    if (rate <= 0) rate = 1000; // poll about every ms
    int sleep_usec = 1000000 / rate;
    o2_stop_flag = false;
    while (!o2_stop_flag) {
        o2_poll();
        usleep(sleep_usec);
    }
    return O2_SUCCESS;
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
    return o2_status_from_proc(entry, NULL);
}


o2_err_t o2_can_send(const char *service)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    if (!service || !*service || strchr(service, '/') || strchr(service, '!'))
        return O2_BAD_SERVICE_NAME;
    services_entry_ptr services;
    o2_node_ptr entry = o2_service_find(service, &services);
    if (entry) {
        if (IS_REMOTE_PROC(entry)) {
            o2n_info_ptr net_info = TO_PROC_INFO(entry)->net_info;
            if (net_info) {
                return (net_info->out_message ? O2_BLOCKED : O2_SUCCESS);
            }
#ifndef O2_NO_OSC
        } else if (ISA_OSC(entry)) {
            o2n_info_ptr net_info = TO_OSC_INFO(entry)->net_info;
            if (net_info) {
                return (net_info->out_message ? O2_BLOCKED : O2_SUCCESS);
            }
#endif
        } else {
            return O2_SUCCESS;
        }
    }
    return O2_FAIL;
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
static const char *error_strings[] = {
    "O2_SUCCESS",
    "O2_FAIL",
    "O2_SERVICE_EXISTS",
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
    "O2_SEND_FAIL",
    "O2_BAD_SERVICE_NAME",
    "O2_SOCKET_ERROR",
    "O2_NOT_INITIALIZED",
    "O2_BLOCKED",
    "O2_NO_PORT",
};
    

const char *o2_error_to_string(o2_err_t i)
{
    if (i < 1 && i >= O2_SEND_FAIL) {
        sprintf(o2_error_msg, "O2 error %s", error_strings[-i]);
    } else {
        sprintf(o2_error_msg, "O2 error, code is %d", i);
    }
    return o2_error_msg;
}



o2_err_t o2_finish()
{
    if (!o2_ensemble_name) { // see if we're running
        return O2_NOT_INITIALIZED;
    }
    o2n_free_deleted_sockets();

#ifndef O2_NO_BRIDGES
    o2_bridges_finish();
#endif

    // Close all the sockets.
    if (o2_ctx) {
        int i = 0;
        o2n_info_ptr info;
        while ((info = o2n_get_info(i))) {
            proc_info_ptr proc = (proc_info_ptr) (info->application);
            O2_DBo(if (proc) \
                       printf("%s o2_finish calls o2n_close_socket at "     \
                              "index %d tag %d %s net_tag %d %s port %d\n", \
                              o2_debug_prefix, i, proc->tag,                \
                              o2_tag_to_string(proc->tag), info->net_tag,   \
                              o2n_tag_to_string(info->net_tag), info->port);\
                   else \
                       printf("%s o2_finish calls o2n_close_socket at index " \
                              "%d net_tag %d %s port %d no application\n",  \
                              o2_debug_prefix, i, info->net_tag,            \
                              o2n_tag_to_string(info->net_tag), info->port));
            o2n_close_socket(info);
            i++;
        }
        o2n_free_deleted_sockets(); // deletes process_info structs
        // now that there are no more sockets, we can free local process,
        // which multiple sockets had a reference to
        o2_proc_info_free(o2_ctx->proc);
        o2_ctx->proc = NULL;

        O2_DBG(printf("before o2_hash_node_finish of path_tree:\n");\
               o2_node_show((o2_node_ptr) (&o2_ctx->path_tree), 2);)

        o2_hash_node_finish(&o2_ctx->path_tree);

        // printf("after o2_hash_node_finish of path_tree:\n");
        // o2_node_show((o2_node_ptr) (&o2_ctx->path_tree), 2);

        o2_hash_node_finish(&o2_ctx->full_path_table);
        o2_argv_finish();
    }
    o2n_finish();

    o2_sched_finish(&o2_gtsched);
    o2_sched_finish(&o2_ltsched);
    o2_discovery_finish();
    o2_clock_finish();
    o2_services_list_finish();
    o2_free_pending_msgs(); // free any undelivered messages

    O2_FREE((void *) o2_ensemble_name);
    o2_ensemble_name = NULL;
    o2_mem_finish();
    // we assume that o2_ctx is statically allocated, not on heap
    o2_ctx = NULL;
    return O2_SUCCESS;
}
