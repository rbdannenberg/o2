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
                |service_ |    +->tapper (O2string)      +----------+
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

Class and abstractions overview:
--------------------------------

* Base Layer:

o2base, o2obj, atomic provide common runtime functions and real-time memory
allocate and free functions, atomic lock-free queues and classes.

* Network Layer:

network provides an asynchronous socket library with abstractions for sending
and receiving messages. Defines O2netmsg and Net_interface class

* O2 Layer:

Most other files implement the core of O2.

O2node (an O2obj) is a node in an address path tree or hash table,
thus it has a hash key member. Every O2node also has a tag member that
can be used to identify the class and status. E.g. there are a
handfull of Proc_info tags depending on the clock sync status and type
of connection (TCP, UDP, etc.) O2node is subclassed to create:
    Services_entry - a list of services and taps associated with a 
        service name.
    Handler_entry - an O2 message handler
    Hash_node - a top-level dictionary of Services or an internal node
        of the path tree.

    Proxy_info - analagous to a Handler_entry, but this message
        handler is only found as a service provider (it is never an
        entry in a Hash_node) and forwards messages to a remote
        entity. If a Proxy_info instance provides multiple services,
        it can be pointed to by multiple Service_provider entries.
        Since Subclasses are:

        Proc_info - a remote O2 process
        MQTT_info - a remote O2 process reached by MQTT protocol
        OSC_info - forwards messages to an OSC server
        Bridge_info - forwards messages through a "bridge". A subclass is:
            O2lite_info - forwards messages using O2lite protocol
            SM_info - forwards message to a shared memory thread

        A Proxy_info inherits the interface class Net_interface, so
        it can also be designated as the "owner" of an Fds_info, which
        represents a socket and network connection or
        address. Remote_info's have "fds_info" pointers to the
        Fds_info object.  To handle network events, a Proxy_info, by
        inheriting Net_interface, implements:

        accepted() - called when a TCP server accepts a connection
        connected() - called when an (asynchronous) connect() completes
        deliver() - called when a full message arrives, or for "raw"
            connections, when any bytes arrive.
        remove() - called when a socket is closed. To allow closing to
            be initiated by either the O2 application or network
            errors, the deconstructor for Proxy_info, if fds_info is
            non-NULL, sets fds_info->owner = NULL and then deletes
            fds_info. The deconstructor for Fds_info, if non-NULL,
            sets owner->fds_info = NULL and deletes owner. Thus,
            deleting either a Proxy_info or an Fds_info will delete
            the partner as well.

The object initiating the closure first sets the fds_info and owner pointers
to NULL so the objects are not cross-linked. Then, the other object is
deleted.

* Bridge Layer

The shared-memory and o2lite bridge protocols are implemented above
the O2 layer and they are optional.


An O2 process has a thread-local context called o2_ctx.

o2_ctx->path_tree - a Hash_node mapping service names to a
    Services_entry, which keeps a list of who offers the service and
    what taps exist.  Only the highest IP:port string
    (lexicographically) is valid.  Generally, trying to offer
    identical service names from multiple processes is a bad idea, and
    note that until the true provider with the highest IP:port string
    is discovered, messages may be sent to a different service with
    the same name.

    Each Services_entry has an array (not a hash table) of entries 
    of class O2node (not including the subclass Services_entry):
        Hash_node: a local service. This is the root of a tree
            of hash tables where leaves are Handler_entry's.
            If O2_NO_PATTERNS, there are no Hash_nodes here; instead,
            an empty Hash_node serves to redirect the
            search to the global hash table of full method addresses.
        Handler_entry: a local service. If there is a 
            handler_entry at this level, it is the single handler
            for all messages to this local service.
        Proc_info: includes index of the socket and 
            pip:iip:port name of the service provider
        MQTT_info: includes pip:iip:port name of the service provider
        OSC_info: delegates to an OSC server. For the purposes of
            finding the highest pip:iip:port string, this is considered
            to be a local service. A services_entry can have at 
            most one of Hash_node, Handler_entry, Osc_entry, but
            any number of Remote_info's.
        Bridge_info: service is remote but reached by an alternate
            transport (not the O2 IP protocol, but could still be IP)

    The first element in the array of entries in a Services_info
    is the "active" service -- the one with the highest pip:iip:port
    string. Other elements are not sorted, so when a service is 
    removed, a linear search to find the largest remaining offering
    (if any) is performed and the new active service is moved to the top.

    The o2_ctx->path_tree also maps IP addresses + ports (as strings
    that begin with '@' and have the form @7f026472:7f026472:4000) 
    to a Services_entry that contains one Proxy_info.

o2_ctx->full_path_table is a dictionary for full paths, permitting a
    single hash table lookup for addresses of the form
    !synth/lfo/freq.  In practice, an additional lookup of just the
    service name is required to determine if the service is remote or
    if there is a single handler for all messages to that service. If
    not, then a second hash lookup on the entire path is used to find
    the handler in full_path_table.

Each handler object is referenced by some node in the o2_ctx->path_tree
    and by the o2_ctx->full_path_table dictionary, except for handlers
    that handle all service messages. These are only referenced by the 
    o2_ctx->path_tree.

o2_ctx->fds is a Vec (dynamic array) of sockets for poll
o2_ctx->fds_info is a parallel Vec of pointers to Fds_info.
    An Fds_info includes a net_tag (see processes.h for more detail 
    and connection life-cycles):

    NET_UDP_SERVER         -- a socket for receiving UDP
    NET_TCP_SERVER         -- the local process
    NET_TCP_CLIENT         -- tcp client
    NET_TCP_CONNECTING     -- client side TCP socket during async connect
    NET_TCP_CONNECTION     -- accepted by tcp server
    NET_TCP_CLOSED         -- socket has been closed, about to be deleted
    NET_UDP_SERVER         -- UDP receive socket for this process
    All Proxy_info records contain an index into o2_ctx->fds (and
      o2_ctx->fds_info and the index must be updated if a socket is moved.)

    An Fds_info has a pointer called owner that points to a
    Proxy_info. Incoming messages, connections, accept requests, and
    socket close events are delegated to the owner.

O2 Process Names
----------------
A full process name has the form @<public_ip>:<internal_ip>:<local_port_num>,
e.g. @76543210:a2010011:64541 (The port will always be one of the 16 
"discovery" ports defined by O2.) This gives a unique name to every
process (assuming two LANS do not share the same public IP, which is
possible but unusual).

The public IP is obtained via a STUN server which is an asynchronous 
query. Before discovery begins, we wait for the public IP. The discovery
protocol is started by the handler for the STUN reply message.

If no STUN reply is received in 5 tries (which takes 10 seconds), we 
assume there is no public IP connection and zero is used (00000000)
to signify no Internet connection.

If there is no network at all, the internal ip is 7f000001 (127.0.0.1
or localhost), and discovery still works to connect to other processes
on the same host.

Also note that if the local host is using VPN, the internal IP address
detected by O2 may not actually be reachable.

On startup, the public ip is not known, and the local process has no
name (other than the alias "_o2"). When the public ip is resolved, the
name is installed as o2_ctx->proc->key.  ("key" refers to hash key. The
key gives the "name" of processes.)

Initialization
--------------
Since we have to wait for a public ip address, initialization takes place
in multiple phases:

PHASE 1. Started when o2_initialize() is called.
    - o2_ctx -- the O2 thread context, needed for the heap
    - o2_mem_init() -- memory allocation, create the heap
    - o2_clock_initialize() -- start the local clock
    - o2_sched_initialize() -- start the local scheduler
    - o2n_initialize() -- initialize network services,
    - o2_discovery_initialize() -- grab an O2 port so we can fail early
         when no port is available
    - start protocol to get public_ip from STUN server


PHASE 2. Started when public_ip is obtained (could be a 10s delay if
there is no Internet connection. Initialization continues with a call
to o2_init_phase2().
    - create our local name, process info, TCP server port and UDP port
    - install system services: /_o2/dy, /_o2/cs/??, etc.
    - start clock synchronization
    - set up mqtt connection and subscriptions

PHASE 3. Eventually, we discover other processes and establish clock sync.
There is not a specific event that marks Phase 3 since the discovery and 
status of other processes happens asynchronously.

Circular Dependencies or Not
----------------------------

To create a service, normally, we provide the proc->key for that
service.  There can be multiple processes (called service providers in
this context) for a service, and messages are sent to the provider
with the highest proc->key (according to strcmp). To create the _o2
service, we need the local proc->key, which depends on the public_ip,
which depends on running the stun protocol, which depends scheduling
retries, which requires a handler, which needs to be installed at
/_o2/ipq, which requires the _o2 service. Thus, we have a circularity.

Furthermore, the proc_info object for the local process is basically a
network connection, so we cannot create the proc_info without
initializing the network, but we normally need an associated proc_info
to create a service.

We can either give the task of getting the public IP address to the O2
layer and start this after network initialization, or we can break the
circularity somewhere to allow the network to start using the
scheduler before the local process's proc_info is available and
without having a proc->key.

Since network.c is already fairly isolated from higher levels of
discovery and O2 messages, we avoid making network.c depend upon
scheduling and O2 functions. Instead, the layers that use network.c
can either
    1. set o2n_network_enabled to false in which case the
       o2n_public_ip is set to "00000000" to signify "no Internet" and
       o2n_internal_ip is set to localhost ("7f000001") or
    2. the o2n_public_ip can be left as the empty string, signifying 
       "public IP is unknown" or
    3. the o2n_public_ip can be changed some time after initialization
       from empty to the actual public IP address.
So, it is up to the O2 layer to run the stun protocol and set the
public IP address.

Another problem exists with discovery. We want to determine early if
we can bind UDP and TCP sockets to one of 16 pre-determined "O2" 
ports. If not, initialization should fail "early." Searching for
ports is also a fairly non-real-time operation, so it is good to 
get it over with during initialization. The problem is that when
discovery creates a TCP socket which is represented by a proc_info,
we would like to have the full @public:internal:port name for the
process, but that depends on the public IP address which becomes
known only after running the STUN protocol.

Therefore, we run initialization and discovery in two phases as 
outlined above. Phase 1 determines the O2 port after setting up the
network, clocks, and scheduling. Phase 2 runs after we have the
public IP port and full local process name, at which time we can
begin broadcasting our name to other processes as part of the 
discovery protocol.

There is a slight problem because we do not want to hold up the
application while O2 finishes initialization. As long as the
application does not need the full process name, there should not be a
problem. The application must also wait for services to be discovered,
so some amount of asynchrony and synchronization is a given. There are
some careful internal checks so that generally the local process is
named by "_o2" instead of the full @public:internal:port name. In some
places, before using the key field of a proc_info structure, we
compare the proc_info address to o2_ctx->proc (the local process's
proc_info_ptr). If equal, we use "_o2" instead of proc->key.


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
the form @IP:IP:Port, identifiable by a leading '@'. For each of these,
the receiver could check to see if the service exists. If not, it
could treat this as a newly discovered process. On the down side, the
N^2 !_o2/sv messages now grow to contain O(N) services (each), making
the data exchanged through discovery grow to N^3. Bonjour apparently
uses this scheme of sending known services in messages, but these are
broadcast messages so sending N services to N processes only costs
1 message with N services.

The address for discovery messages is !_o2/dy, and the arguments are:
    ensemble name (string)
    public ip (string)
    internal ip (string)
    tcp port (int32)
    upd port (int32)
    dy flag (int32) (see below)

Once a discovery message is received (usually via UDP), in either
direction, a TCP connection is established. Since the higher
public:internal:port
string must be the server to prevent race conditions, the protocol is
a little more complicated if the server discovers the client. In that
case, the server makes a TCP connection and sends a discovery message
to the client. The client (acting temporarily as a server) then closes
the connection and makes a new connection (now acting as a client) 
back to the server. The client sends a discovery message again.

When a TCP connection is connected or accepted, the process sends
a list of services to !_o2/sv at the remote process.

Distributed Services Database
-----------------------------

A key design element is that every process can perform a local lookup
to map a service given in a message address to the process that offers
the service. Therefore, every process has a complete index mapping all
services to their providers. When two providers offer the same service
(name), *both* providers are known to all other processes, and
messages are sent to the process with the highest address.

Similarly, every tap of every service is known to every process. It
may seem that this is overkill, since only the actual service provider
sends messages to the service tappers. However, the actual service
provider can change if a service is created, thus any process can
become the tappee by becoming the service provider for a tapped
service.  (Note: an alternative design would be when the service
provider changes, any tappers for the service would send tap request
messages to the new service provider. This would create a window in
which tap messages could be missed.)

This distributed service index is subject to race conditions where
changes take time to propagate and during that time, messages may be
misdirected. The only guarantee is that if there are no new changes,
all processes will *eventually* acquire consistent information. In
practice, changes propagate quickly (tens of ms or less) except
possibly in the case where a crashed process might not be noticed
until a TCP timeout occurs.

Consistency is guaranteed as follows: First, all processes form a
fully connected graph. When a process joins the ensemble, discovery
guarantees that it connects directly to every other process. When
connection occurs, *all* local service information is exchanged. Once
connected, any change to the local services is transmitted to *all*
other processes. When a connection is lost to a process, *all*
services offered by that process are deleted. If the deleted process
was the active provider of a service, another provider of the service
can be immediatly determined locally by every surviving process.

Taps are treated similarly. The "owner" of the tap is the tapper, so
the tapper sends information to other processes about the taps it has
asserted. The "tappees" simply keep track of the taps. (They are
stored with services so when a message is delivered locally to the
service, a tap message is also sent to each tapper.) When a remote
process is deleted, the local process removes all records of taps
asserted by that process. (Taps are always associated with a single
process. A higher provider address does not override a tap; i.e. two
processes can create local services named "mytap" and use calls to
`o2_tap` to tap a service named "spied-upon." Any message delivered
locally to "spied-upon" will then be sent to *both* "mytap" services.)

Process Creation
----------------

o2_discovery_handler() receives !_o2/dy message. There are two cases
based on whether the local host is the server or client. (The server is
the host with the greater @public:internal:port string. The client
connects to the
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
    Locally, the client creates a service named "@public:internal:port"
        representing
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

The hub does not have to be on the local area network. In that case,
ALL processes using the hub must not be behind NAT because the TCP
connection can be hub-to-client or client-to-hub depending on which
has the higher name (IP:Port string).

Alternatively, the hub can be on the local area network. In that case,
all clients using the hub should be on the same LAN.

Because of these two options, the hub must be identified with a full
name specification, meaning public_ip, internal_ip, and local port number.

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
We assume UDP does not block but rather drops packets.  TCP sockets
are set not to block so that send can fail with EWOULDBLOCK (message
not sent). When that happens, the message is linked to the proc
structure's pending_msg field. It is sent as soon as the socket
becomes writable.  While a message is pending, o2_can_send() will
return O2_BLOCKED. If the user does not check o2_can_send() and
attempts to send a message while another message is already pending,
then o2_send() performs a blocking send of the pending message, then
performs a non-blocking send of the current message.

To facilitate sending multiple messages (discovery and service
information for example), o2_net code allows you to queue up multiple
messages to send. This feature is only available to internal O2
functions and is only used when a connection is created. If the user
process tries to send a message when there are already message(s) in
the queue, the messages are sent synchronously and the user blocks
until all pending messages are sent (or at least they are accepted by
the kernel for delivery).

Taps
----
A tap is similar to a service and a service_tap object appears in the
taps list for the tappee. The service_tap contains the tapper's string
name and the process (a o2n_info_ptr) of the tapper. Note that a
tapper is described by both process and service name, so messages are
delivered to that process, EVEN IF the service is not the active
one. For example, if the tap directs message copies to "s1" of process
P1, message copies will be sent there even if a message directly to
service "s1" would go to process P2 (because "s1" is offered by both
P1 and P2, but the pip:iip:port name of P2 is greater than that of
P1.) The main reason for this policy is that it allows taps to be
removed when the process dies.  Without the process specification, it
would be ambiguous who the tap belongs to. (See also "Distributed
Service Database")


Remote Process Name: Allocation, References, Freeing
---------------------------------------------------- 
Each remote process has a name (key), e.g. "@ac640032:80020164:55765"
that can be used as a service name in an O2 address, e.g.  to announce
a new local service to that remote process.  The name is on the heap
and is "owned" by the Proc_info record associated with the NET_TCP_*
socket for remote processes. Also, the local process has its name
owned by o2_ctx->proc.

The process name is *copied* and used as the key for a
Service_info to represent the service in the o2_ctx->path_tree.

The process name is freed by o2_info_remove().


OSC Service Name: Allocation, References, Freeing
-------------------------------------------------
o2_osc_port_new() creates a tcp service or incoming udp port for OSC
messages that are redirected to an O2 service. The Services_entry key
"owns" the service name which is on the heap. This name is *copied* to
the osc.key in the Osc_info record.  For TCP, this name is also
*copied* to each OSC_TCP_CONNECTION that is created when an OSC client
connects to an OSC_TCP_SERVER.  These sockets and their osc service
name are freed when their sockets are deleted. Notice that services
can be removed without closing referencing OSC_info's and OSC_info's
can be removed without removing the service they reference.

On the other hand, OSC_TCP_CLIENTs and OSC_UDP_CLIENTs are similar to
Hash_nodes and Handler_entries in that they do not need a key field --
the service name that redirects to OSC is owned by a Services_entry
and used to find the Osc_node. The Osc_node for clients has a NULL
key field.

O2TAG_HASH and O2TAG_HANDLER keys are NULL at top level
----------------------------------------------------
If a Hash_node (tag & O2TAG_HASH) or Handler_entry (tag ==
O2TAG_HANDLER) exists as a service provider, i.e.  in the services list
of a Services_entry record, the key field is not really needed because
the service lookup takes you to the services list, and the first
element there offers the service. E.g. if there's a (local) handler
for /service1/bar, then we lookup "service1" in the o2_ctx->path_tree,
which takes us to a Services_entry, the first element on its services
list should be a hash_node. There, we do a hash lookup of "bar" to get
to a Handler_entry. The key is set to NULL for Hash_node and
Handler_entry records that represent services and are directly pointed
to from a services list.

Bridge and O2lite: Allocation, References, Freeing
-------------------------------------------------
Structures can be deleted in many ways, creating a challenge to avoid
memory leaks. Each path for deletion is considered:

Service Deletion. (Specifically, the removal of a service provider
from a services list.) For O2lite_info, there is one Bridge_info per
connection which is shared across services provided by the Bridge_info
connection, so do not delete the Bridge_info. Even if no services
reference the Bridge_info, the connection can stay open.

Fds_info Deletion. If a connection is lost, we must delete the
owner. If owner is a Bridge_info, set owner->net_info to NULL and
delete owner. The Bridge_info destructor is invoked but it does not
recursively delete the Fds_info because the back-pointer is NULL.

O2lite_protocol Deletion. An entire O2lite protocol can be deleted. To
do this, we use #remove_services to remove every service offering by
this protocol.  Then call the protocol's bridge_finish to finalize any
structures in use by the bridge. Finally, free the bridge_protocol and
remove it from bridges.

bridges Deletion. When o2_finish is called, we need to delete all
bridges. Delete each bridge_protocol in bridges. Then free the bridges
array.

Byte Order
----------
Messages are constructed and delivered in local host byte order.  When
messages are sent to another process, the bytes are swapped if
necessary to be in network byte order. Messages arriving by TCP or UDP
are therefore in network byte order and are converted to host byte
order if necessary.

Memory Leaks
------------
Since the user can exit in the middle of message delivery, and
possibly even in the middle of nested message delivery because of
duplicate messages being sent to taps, we can have in-flight messages
that are not freed even when o2_finish() frees all the data
structures. To complete the cleanup, we keep all in-flight messages
not in some other structure in the list o2_ctx->msgs.

Since this is global, we often treat o2_ctx->msgs as the "active"
message and consider it to be an implied parameter for many delivery
functions.

O2lite Implementation
---------------------
O2lite is implemented as a specialization of the Bridge protocol.
See ../doc/o2lite.txt for details.

*/


/**
 *  TO DO:
 *  see each error return has the right error code
 *  tests that generate error messages
 */

#include <stdio.h>
#include <signal.h>
#include <ctype.h>

#include "o2internal.h"
#include "o2mem.h"
#include "services.h"
#include "o2osc.h"
#include "message.h"
#include "msgsend.h"
#include "o2sched.h"
#include "clock.h"
#include "discovery.h"
#include "properties.h"
#include "pathtree.h"
#include "o2zcdisc.h"

const char *o2_ensemble_name = NULL;
char o2_hub_addr[O2_MAX_PROCNAME_LEN];

// these times are set when poll is called to avoid the need to
//   call o2_time_get() repeatedly
O2time o2_local_now = 0.0;
O2time o2_global_now = 0.0;
O2time o2_global_offset = 0.0;


#ifdef WIN32
// ctrl-C handler for windows (referenced below)
static BOOL WINAPI console_ctrl_handler(DWORD dw_ctrl_type)
{
    o2_finish();
    // Return TRUE if handled this message, further handler functions 
    // won't be called.
    // Return FALSE to pass this message to further handlers until
    // default handler calls ExitProcess().
    return FALSE;
}
#else
// ctrl-c handler for Unix (Mac OS X, Linux)
// 
static void o2_int_handler(int s)
{
    // debugging statement: race condition, should be removed:
    printf("O2 Caught signal %d\n", s);
    o2_finish(); // clean up ports
    exit(1);
}
#endif


O2err o2_network_enable(bool enable)
{
    if (o2_ensemble_name) {
        return O2_ALREADY_RUNNING;
    }
    o2n_network_enabled = enable;
    return O2_SUCCESS;
}


O2err o2_initialize(const char *ensemble_name)
{
    static O2_context main_context;
    O2err err;
    if (o2_ensemble_name) return O2_ALREADY_RUNNING;

    // if the compiler does not lay out the message structure according
    // to the actual message bytes, we are in big trouble, so this is a
    // basic sanity check on the compiler (and we assume the optimizing
    // compiler will follow suit):
    assert(offsetof(O2netmsg, payload) == offsetof(O2netmsg, length) + 4);
    assert(offsetof(O2msg_data, misc) == offsetof(O2msg_data, length) + 4);
    assert(offsetof(O2msg_data, timestamp) ==
           offsetof(O2msg_data, length) + 8);
    assert(offsetof(O2msg_data, address) ==
           offsetof(O2msg_data, length) + 16);
    assert(offsetof(O2netmsg, length) == offsetof(O2message, data));
    
    o2_stun_query_running = false;
    
    // note: new O2_context would require O2MALLOC, which we don't have yet,
    // so we use a static variable and carefully design O2_context so that
    // it does no allocation.
    o2_ctx = &main_context;
    o2_mem_init(NULL, 0);
    if (!ensemble_name) return O2_BAD_NAME;
    if (strlen(ensemble_name) > O2_MAX_NAME_LEN) {
        return O2_BAD_NAME;
    }
    // Initialize the ensemble name.
    o2_ensemble_name = o2_heapify(ensemble_name);
    if (!o2_ensemble_name) {
        err = O2_NO_MEMORY;
        goto cleanup;
    }
    
#ifdef WIN32
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#else
    // before sockets, set up signal handler to try to clean up ports
    // in the event of a Control-C shutdown. This seems to leave ports
    // locked up forever in OS X, eventually requiring a reboot, so
    // anything we can to to free them is helpful.
    struct sigaction sig_int_handler;
    sig_int_handler.sa_handler = o2_int_handler;
    sigemptyset(&sig_int_handler.sa_mask);
    sig_int_handler.sa_flags = 0;
    sigaction(SIGINT, &sig_int_handler, NULL);
#endif
    // atexit will ignore the return value of o2_finish:
    atexit((void (*)(void)) &o2_finish);

    if ((err = o2n_initialize())) {
        goto cleanup;
    }
    // Initialize discovery, which depends on clock and scheduler
    if ((err = o2_discovery_initialize())) {
        goto cleanup;
    }

    o2_clock_initialize();
    o2_sched_initialize();
    Services_entry::service_new("_o2");

     if (o2n_public_ip[0]) {  // we already have a (pseudo) public ip
        assert(streql(o2n_public_ip, "00000000"));
        o2_init_phase2();    // continue with Phase 2
    } else if ((err = o2_get_public_ip())) {
        goto cleanup;
    }
    return O2_SUCCESS;
  cleanup:
    o2_finish();
    return err;
}


void o2_init_phase2()
{
    o2_processes_initialize();
    o2_clock_init_phase2(); // install handlers for clock sync
    o2_discovery_init_phase2();
    // start the discovery and MQTT setup
#ifndef O2_NO_O2DISCOVERY
    o2_send_discovery_at(o2_local_time());
#endif
#ifndef O2_NO_ZEROCONF
    o2_zcdisc_initialize();
#endif
#ifndef O2_NO_MQTT
    if (o2_mqtt_waiting_for_public_ip) {
        o2_mqtt_initialize();
    }
#endif
}


O2err o2_get_addresses(const char **public_ip, const char **internal_ip,
                          int *port)
{
    if (!o2_ctx || !o2_ctx->proc) {
        return O2_FAIL;
    }
    *public_ip = (const char *) o2n_public_ip;
    *internal_ip = (const char *) o2n_internal_ip;
    *port = o2_ctx->proc->fds_info->port;
    return O2_SUCCESS;
}


const char *o2_get_proc_name()
{
    if (!o2_ctx || !o2_ctx->proc)
        return NULL;
    return o2_ctx->proc->key;
}

static void send_one_sv_msg(Proxy_info *proc, const char *service_name,
                            int added, const char *tapper,
                            const char *properties, int send_mode)
{
    o2_send_start();
    assert(o2_ctx->proc->key);
    o2_add_string(o2_ctx->proc->key);
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
    o2_add_int32(send_mode);
    O2message_ptr msg = o2_message_finish(0.0, "!_o2/sv", true);
    if (!msg) return; // must be out of memory, no error is reported
    o2_prepare_to_deliver(msg);
    proc->send(false);
    O2_DBd(printf("%s o2_notify_others sent %s to %s (%s) "
                  "tapper %s properties %s\n",
                  o2_debug_prefix, service_name, proc->key,
                  added ? "added" : "removed", tapper, properties));
}


/** notify all known processes that a service has been added or
 * deleted. If adding a service and tapper is not empty or null,
 * then the new service is tapper, which is tapping service_name.
 * Notices go to remote processes, but not to bridges.
 */
void o2_notify_others(const char *service_name, bool added,
                      const char *tapper, const char *properties,
                      int send_mode)
{
    if (!o2_ctx->proc->key) {
        return;  // no notifications until we have a name
    }
    if (!tapper) tapper = ""; // Make sure we have a string to send.
    if (!properties) properties = "";
    // when we add or remove a service, we must tell all other
    // processes about it. To find all other processes, use the
    // o2_ctx->fds_info table since all but a few of the
    // entries are connections to processes
    //TODO: debugging code
    if (!added && streql(service_name, "publish0") && streql(tapper, "copy0"))
        printf("publish0: remove tapped by copy0\n");
    //TODO: above is debugging code to be removed
    for (int i = 0; i < o2n_fds_info.size(); i++) {
        Fds_info *info = o2n_fds_info[i];
        Proxy_info *proc = (Proxy_info *) (info->owner);
        if (proc && ISA_REMOTE_PROC(proc)) {
            send_one_sv_msg(proc, service_name, added, tapper,
                            properties, send_mode);
        }
    }
#ifndef O2_NO_MQTT
    for (int i = 0; i < o2_mqtt_procs.size(); i++) {
        send_one_sv_msg(o2_mqtt_procs[i],
                        service_name, added, tapper, properties, send_mode);
    }
#endif
}


/* adds a tap - implements o2_tap()
 */
O2err o2_tap_new(O2string tappee, Proxy_info *proxy, const char *tapper,
                 O2tap_send_mode send_mode)
{
    O2_DBd(printf("%s o2_tap_new adding tapper %s in %s to %s\n",
                  o2_debug_prefix, tapper, proxy->key, tappee));
    Services_entry *ss = Services_entry::must_get_services(tappee);

    // services exists, does the tap already exist?
    // search the service's list of taps:
    int i;
    for (i = 0; i < ss->taps.size(); i++) {
        Service_tap *tap = &ss->taps[i];
        if (streql(tap->tapper, tapper) &&
            tap->proc == proxy) {
            return O2_SERVICE_EXISTS;
        }
    }

    // no matching tap found, so we should create one; taps are unordered
    tapper = o2_heapify(tapper);
    return ss->insert_tap(tapper, proxy, send_mode);
}


// search services list of tappee for matching tap info and remove it.
//
O2err o2_tap_remove(O2string tappee, Proxy_info *proc,
                       const char *tapper)
{
    O2_DBd(printf("%s o2_tap_remove tapper %s in %s tappee %s\n",
                  o2_debug_prefix, tapper, proc->key, tappee));

    Services_entry *ss = (Services_entry *) *o2_ctx->path_tree.lookup(tappee);
    if (!ss) return O2_FAIL;

    return ss->tap_remove(proc, tapper);
}


O2err o2_service_new(const char *service_name)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    if (!service_name || !isalpha(service_name[0]) ||
        strchr(service_name, '/') || strlen(service_name) > O2_MAX_NAME_LEN) {
        return O2_BAD_NAME;
    }
    char padded_name[NAME_BUF_LEN];
    o2_string_pad(padded_name, service_name);
    return Services_entry::service_new(padded_name);
}


void o2_message_drop_warning(const char *warn, o2_msg_data_ptr msg)
{
    printf("Warning: %s,\n    message is ", warn);
#ifdef O2_NO_DEBUG
    printf("%s (%s)", msg->address, o2_msg_data_types(msg));
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


O2err o2_method_new(const char *path, const char *typespec,
                       O2method_handler h, const void *user_data,
                       bool coerce, bool parse)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    if (!path || path[0] == 0 || path[1] == 0 || path[0] != '/' ||
        (!isalpha(path[1]) && !streql(path, "/_o2/si"))) {
        return O2_BAD_NAME;
    }
    return o2_method_new_internal(path, typespec, h, user_data, coerce, parse);
}


O2err o2_tap(const char *tappee, const char *tapper, O2tap_send_mode send_mode)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    char padded_tappee[NAME_BUF_LEN];
    o2_string_pad(padded_tappee, tappee);
    O2err err = o2_tap_new(padded_tappee, o2_ctx->proc, tapper, send_mode);
    return err;
}


O2err o2_untap(const char *tappee, const char *tapper)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    char padded_tappee[NAME_BUF_LEN];
    o2_string_pad(padded_tappee, tappee);
    return o2_tap_remove(padded_tappee, o2_ctx->proc, tapper);
}

/* DEBUGGING:
static void check_messages()
{
    for (int i = 0; i < O2_SCHED_TABLE_LEN; i++) {
        for (O2message_ptr msg = o2_ltsched.table[i]; msg; msg = msg->next) {
            assert(msg->allocated >= msg->length);
        }
    }
}
*/

O2err o2_poll()
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
    int sleep_ms = 1000 / rate;
    // we use a ms timer, so don't go below 1ms:
    if (sleep_ms < 1) sleep_ms = 1;
    o2_stop_flag = false;
    while (!o2_stop_flag) {
        o2_poll();
        o2_sleep(sleep_ms);
    }
    return O2_SUCCESS;
}


int o2_status(const char *service)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    if (!service || !*service || strchr(service, '/') || strchr(service, '!'))
        return O2_BAD_NAME;
    Services_entry *services;
    O2node *entry = Services_entry::service_find(service, &services);
    return (entry ? entry->status(NULL) : O2_UNKNOWN);
}


O2err o2_can_send(const char *service)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    if (!service || !*service || strchr(service, '/') || strchr(service, '!'))
        return O2_BAD_NAME;
    Services_entry *services;
    O2node *entry = Services_entry::service_find(service, &services);
    if (entry && ISA_PROXY(entry)) {
        Fds_info *fds = ((Proxy_info *) entry)->fds_info;
        if (fds) {
            return fds->can_send();
        } else {
            return O2_SUCCESS; // because this is a UDP connection
        }
    } else if (entry) {
        return O2_SUCCESS; // because this is a local service
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
    "O2_NO_NETWORK"
};
    

const char *o2_error_to_string(O2err i)
{
    if (i < 1 && i >= O2_NO_NETWORK) {
        sprintf(o2_error_msg, "O2 error %s", error_strings[-i]);
    } else {
        sprintf(o2_error_msg, "O2 error, code is %d", i);
    }
    return o2_error_msg;
}


static const char *status_strings[] = {
    "O2_UNKNOWN",
    "O2_LOCAL_NOTIME",
    "O2_REMOTE_NOTIME",
    "O2_BRIDGE_NOTIME",
    "O2_TO_OSC_NOTIME",
    "O2_LOCAL",
    "O2_REMOTE",
    "O2_BRIDGE",
    "O2_TO_OSC" };

const char *o2_status_to_string(int status)
{
    if (status >= O2_UNKNOWN && status <= O2_TO_OSC) {
        return status_strings[status + 1];
    } else {
        return o2_error_to_string((O2err) status);
    }
}


O2err o2_finish()
{
    if (!o2_ensemble_name) { // see if we're running
        return O2_NOT_INITIALIZED;
    }
    o2n_free_deleted_sockets();

#ifndef O2_NO_BRIDGES
    o2_bridges_finish();
#endif
#ifndef O2_NO_MQTT
    o2_mqtt_finish();
#endif
    // before closing sockets, one special case is the main udp server
    // socket AND the tcp server sockets point to o2_ctx->proc, and both
    // will try to delete o2_ctx->proc. There's no reference counting, so
    // remove one reference before proceeding:
    o2_udp_server->owner = NULL;

    o2_discovery_finish();
    // Close all the sockets.
    if (o2_ctx) {
        for (int i = 0; i < o2n_fds_info.size(); i++) {
            Fds_info *info = o2n_fds_info[i];
            Proxy_info *proxy = (Proxy_info *) (info->owner);
            O2_DBo(if (proxy) \
                       printf("%s o2_finish calls o2n_close_socket at "     \
                              "index %d tag %d %s net_tag %x (%s) port %d\n", \
                              o2_debug_prefix, i, proxy->tag,                \
                              o2_tag_to_string(proxy->tag), info->net_tag,   \
                              Fds_info::tag_to_string(info->net_tag), \
                              info->port);\
                   else \
                       printf("%s o2_finish calls o2n_close_socket at index " \
                              "%d net_tag %x (%s) port %d no application\n",  \
                              o2_debug_prefix, i, info->net_tag,            \
                              Fds_info::tag_to_string(info->net_tag), \
                              info->port));
            info->close_socket(true);
        }
        o2n_free_deleted_sockets(); // deletes process_info structs
        // now that there are no more sockets, we can free local process,
        // which multiple sockets had a reference to
        o2_ctx->proc = NULL;
    }
    o2n_finish();

    o2_sched_finish(&o2_gtsched);
    o2_sched_finish(&o2_ltsched);
    //o2_discovery_finish();
    o2_clock_finish();
    o2_services_list_finish();
    o2_free_pending_msgs(); // free any undelivered messages

    O2_FREE((void *) o2_ensemble_name);
    o2_ensemble_name = NULL;
    // we assume that o2_ctx is statically allocated, not on heap
    o2_ctx->finish();
    o2_mem_finish();  // free memory heap before removing o2_ctx
    o2_ctx = NULL;
    return O2_SUCCESS;
}


#ifndef __APPLE__
void o2_strcpy(char *__restrict dst, const char *__restrict src,
              size_t dstsize)
{
    strncpy(dst, src, dstsize);
    // make sure dst is terminated:
    dst[dstsize - 1] = 0;
}
#endif

