// sharedmem.c -- a brige to shared memory O2 service
//
// Roger B. Dannenberg
// August 2020

/*
Supports multiple connections to shared memory processes.
All shared memory processes use the same heap, and O2_MALLOC
is lock-free and thread-safe.

Therefore, o2_message types can be transferred directly to
shared memory queues without byte-swapping, copying, or 
changing format.

The implementation is based on o2lite. Instead of o2lite_inst
containing a net_info_ptr (for the TCP connection) and the
udp_address, the o2sm_inst contains an outgoing message queue.

Services provided by a shared memory process appear locally in
the services array entry as a bridge_inst_ptr whose info points
to an o2sm_inst, where messages can be directly enqueued, making
delivery quite fast and simple.

Received messages are all enqueued on a global o2sm_incoming
queue, which is checked by o2sm_poll. If messages are found, the
entire queue is atomically copied to a delivery queue, reversed,
and then messages are delivered to O2 in the correct order.

Clock local time can be used from shared memory processes except
during a narrow window during o2_clock_set(), but this should 
only be called when the main process is initializing and selecting
a clock source (if any), and only if a non-default clock is set.
If a non-default callback is provided, it must be reentrant for
shared memory processes.

o2_time_get() is more of a problem: If a shared memory process calls
o2_time_get() while clock synchronization is updating local_time_base
global_time_base, and clock_rate, an inconsistent time could be
computed. I'm not very positive on memory barriers because of
portability, cost, and the difficulty to get them right. Another
solution is we can store the offset from local to global time in a
single atomic long or long long, and check for ATOMIC_LONG_LOCK_FREE
or ATOMIC_LLONG_LOCK_FREE to make sure simple reads and writes are
atomic. This will not compute exactly the right clock value when
clock_rate is not 1, but since it is close to 1 and if the offset
is updated at o2_poll() rate, the error will be tiny. In fact, we
could dispense with computing time completely and just use 
global values o2_local_now and o2_global_now, but since o2_poll() 
may not be called as frequently as needed, it's better to recompute
as needed in each shared memory process.

Timing in shared memory process is simpler and more limited that in
O2. In coming messages with timestamps must arrive in time order.  A
timestamp out of order will be considered to be at the time just after
the previous timestamped message. Messages without timestamps,
however, are considered to be in a separate stream and their
processing is not delayed by timestamped messages.

The algorithm for message processing is:
First, move the entire incoming list atomically to a local list.
Run through the list, reversing the pointers (because the list is 
LIFO). Then traverse the list in the new order (the actual message
arrival order), appending each message to either the timestamped
queue or the immediate queue. These lists can have head and tail
pointers to become efficient FIFO queues because there is no
concurrent access.

Next, deliver all messages in the timestamped queue that are ready.
These get priority because, presumably, the timestamps are there
to optimize timing accuracy. Next deliver all non-timestamped
messages. An option, if message delivery is expected to be time
consuming, is to check the timestamped queue after each immediate
message in case enough time has elapsed for the next timestamped
message to become ready.

After deliverying all immediate messages, return from o2sm_poll().

MEMORY, INITIALIZATION, FINALIZATION

We will call the main O2 thread just that. The shared memory process
will be called the O2SM thread in this section. The steps below are
marked with either "(O2 thread)" or "(O2SM thread)" to indicate which
thread should run the operation.

o2_shmem_initialize() - Initially, an array of bridge_inst_ptr is
    created, a new bridge protocol for "o2sm" is created, and a
    handler is created for /_o2/o2sm/sv and /_o2/o2sm/fin. (O2 thread)

o2_shmem_inst_new() - creates a new bridge_inst that points to a new
    o2sm_inst. The bridge_inst must be passed to the O2SM thread. It
    is also stored in the o2sm_bridges array. (O2 thread)

o2sm_initialize() - installs an o2_ctx_t for the O2SM thread and
    retains the bridge_inst_ptr which contains a message queue for
    messages from O2SM to O2. The o2_ctx_t contains mappings from
    addresses to handlers in path_tree and full_path_table.  (O2SM
    thread)

o2sm_service_new() - creates handlers on the O2 side via /_o2/o2sm/sv
    messages. (O2SM thread)

o2sm_method_new() - inserts handlers into the o2_ctx_t mappings.
    (O2SM thread)

o2sm_finish() - To shut down cleanly, first the O2SM thread should
    stop calling o2sm_poll() and call o2sm_finish(), which frees the
    O2SM o2_ctx_t structures (but not the bridge_inst), and calls
    /_o2/o2sm/fin with the id as parameter.

o2_shmem_inst_finish() - called by /_o2/o2sm/fin handler (and also a
    callback for deleting a bridge_inst). Removes outgoing messages
    from o2sm_inst. Similar to o2lite_inst_finish, this removes every
    service that delegates to this bridge if this is the "master"
    instance (each service has a non-master copy of this instance).
    The instance is removed from the o2sm_bridges array.

When the O2 thread shuts down, o2_bridges_finish is called. It is the
    application's responsibility to shut down the O2SM thread
    first. Note that the O2SM thread uses O2 memory allocation, and
    the O2 heap will be shut down as part of o2_finish, so the
    potential problems extend beyond the bounds of the bridge
    API. Assuming the O2SM thread(s) are shut down cleanly when they
    call o2sm_finish(), there will be no more shared memory process
    bridge instances. However, the protocol still exists, so at least
    o2_bridges_finish() will call o2_shmem_finish(), and it may call
    o2_shmem_inst_finish() for any surviving instance.

o2_shmem_finish() - shuts down the entire "o2sm" protocol. First, 
    o2sm_bridges is searched and any instance there is deleted by
    calling o2_shmem_inst_finish(). Then o2sm_bridges is freed.

Typical shared memory process organization is as follows:

#include "o2internal.h"  // o2_ctx_t is not defined in o2.h, so use this
#include "sharedmem.h"
bridge_inst_ptr smbridge = NULL; // global variable accessed by both threads

int main()
{
    ...
    // create the shared memory process bridge (execute this in O2 thread):
    int err = o2_shmem_initialize(); assert(err == O2_SUCCESS);
    smbridge = o2_shmem_inst_new();
    // create shared memory thread
    err = pthread_create(&pt_thread_pid, NULL, &shared_memory_thread, NULL);
    ... run concurrently with the shared memory thread ...
    ... after shared memory thread shuts down, consider calling o2_poll()
    ... in case any "last dying words" were posted as incoming messages
    o2_finish(); // closes the bridge and frees all memory, including
                 // chunks allocated by shared_memory_thread
}

#include "sharedmemclient.h"

void *shared_memory_thread(void *ignore) // the thread entry point
{
    o2_ctx_t ctx;
    o2sm_initialize(&ctx, smbridge); // connects us to bridge
    ... run the thread ...
    o2sm_finish();
    return NULL;
}
*/

#ifndef O2_NO_SHAREDMEM
#include "o2internal.h"
#include "atomic.h"
#include "services.h"
#include "message.h"
#include "msgsend.h"
#include "pathtree.h"
#include "o2mem.h"
#include "bridge.h"
#include "sharedmem.h"

#define O2SM_BRIDGE(i) DA_GET(o2sm_bridges, bridge_inst_ptr, i)

// we do 16-byte alignment explicitly because I don't think the compiler
// can do it since our structures are allocated on 8-byte boundaries
#define OUTGOING(o2sm) ((o2_queue_ptr) \
        (((intptr_t) &(o2sm)->outgoing) & ~0xF))

bridge_protocol_ptr o2sm_bridge = NULL;
static dyn_array o2sm_bridges;

static o2_queue o2sm_incoming;

// o2sm_inst is allocated once and stored on the bridge_inst that is
// referenced by the o2sm_bridges dynamic array. It is also
// referenced by every bridge_inst created as a service.
//
// If clock sync is obtained by O2, we have to search all services 
// provided by a shared memory process and change their tags to
// BRIDGE_SYNCED.
//
typedef struct o2sm_inst {
    int64_t padding; // outgoing must be on a 16-byte boundary, but our
    // memory allocation aligns to 8-byte boundaries, so we always "AND"
    // the outgoing address with ~0xF to force 16-btye alignment. This
    // might locate it 8-bytes lower, so we insert 8 bytes of padding to
    // make sure the shifted address is allocated and usable.
    o2_queue outgoing;
    int bridged_process_index;
} o2sm_inst, *o2sm_inst_ptr;


// Call to establish a connection from a shared memory process to 
// O2. This runs in the O2 thread.
// 
bridge_inst_ptr o2_shmem_inst_new()
{
    o2sm_inst_ptr o2sm;
    // assumes o2sm is initialized
    int id = 0;
    // find place to put it and sequence number
    while (id < o2sm_bridges.length) {
        if (!O2SM_BRIDGE(id)) goto got_id;
        id++;
    }
    DA_EXPAND(o2sm_bridges, bridge_inst_ptr);
  got_id:
    o2sm = O2_CALLOCT(o2sm_inst);
    o2sm->bridged_process_index = id;
    bridge_inst_ptr bridge = o2_bridge_inst_new(o2sm_bridge, o2sm);
    DA_SET(o2sm_bridges, bridge_inst_ptr, id, bridge);
    return bridge;
}


// retrieve all messages from head atomically. Then reverse the list.
//
static o2_message_ptr get_messages_reversed(o2_queue_ptr head)
{
    // store a zero if nothing has changed
    o2_message_ptr all = (o2_message_ptr) o2_queue_grab(head);
    
    o2_message_ptr msgs = NULL;
    o2_message_ptr next = NULL;
    while (all) {
        next = all->next;
        all->next = msgs;
        msgs = all;
        all = next;
    }
    return msgs;
}


// poll callback from O2 to look for incoming messages
o2_err_t shmem_bridge_poll(bridge_inst_ptr inst)
{
    o2_err_t rslt = O2_SUCCESS;
    o2_message_ptr msgs = get_messages_reversed(&o2sm_incoming);
    while (msgs) {
        o2_message_ptr next = msgs->next;
        msgs->next = NULL; // remove pointer before it becomes dangling
        o2_err_t err = o2_message_send(msgs);
        // return the first non-success error code if any
        if (rslt == O2_SUCCESS) rslt = err;
        msgs = next;
    }
    return O2_SUCCESS;
}


// callback from bridge code to send from O2 to shared mem process
o2_err_t shmem_bridge_send(bridge_inst_ptr inst)
{
    if (!inst) {
        inst = o2_ctx->binst;
    }
    o2_message_ptr msg = o2_postpone_delivery();
    // we have a message to send to shmmthe service via shared memory -- find
    // queue and add the message there atomically
    o2sm_inst_ptr o2sm = (o2sm_inst_ptr) inst->info;
    o2_queue_push(OUTGOING(o2sm), (o2_obj_ptr) msg);
    return O2_SUCCESS;
}


static void free_outgoing(o2sm_inst_ptr o2sm)
{
    o2_message_ptr outgoing = (o2_message_ptr) o2_queue_pop(OUTGOING(o2sm));
    while (outgoing) {
        o2_message_ptr msg = outgoing;
        outgoing = msg->next;
        O2_FREE(msg);
    }
}


// o2_shmem_inst_finish -- finalize a shared memory bridge_inst_ptr
//
// inst could be an o2sm_bridge pointer from the o2sm_bridges array or
//      from a service. If it's from the o2sm_bridges array, there must
//      be no reference from the shared process -- if it tries to check
//      for messages and the memory is reallocated as something else, 
//      O2 will likely crash. We must also search all services
//      to remove every service entry that uses this bridge.
//
o2_err_t o2_shmem_inst_finish(bridge_inst_ptr inst)
{
    o2sm_inst_ptr o2sm = (o2sm_inst_ptr) (inst->info);
    if (o2sm) {
        // are we in o2sm_bridges? If so, bridged process is ending
        // so we need to search all services using the connection and
        // remove them.
        if (O2SM_BRIDGE(o2sm->bridged_process_index) == inst) {
            // remove all services offered by this bridged process
            o2_bridge_remove_services(inst->proto, o2sm);
            inst->info = NULL; // prevent from calling again
            DA_SET(o2sm_bridges, bridge_inst_ptr,
                   o2sm->bridged_process_index, NULL);
            free_outgoing(o2sm);
            O2_FREE(o2sm);
        } // otherwise, this is just the removal of a service,
    }     // so we just need to free inst, which is done by caller
    return O2_SUCCESS;
}


o2_err_t o2_shmem_finish(bridge_inst_ptr inst)
{
    // first free all o2sm_inst using o2sm_bridges to find them
    for (int i = 0; i < o2sm_bridges.length; i++) {
        bridge_inst_ptr bi = O2SM_BRIDGE(i);
        if (!bi) {
            continue;  // connections may be closed already
        }
        o2sm_inst_ptr o2sm = (o2sm_inst_ptr) (bi->info);
        // services using this bridge have already been removed because
        // this is a callback that is called after o2_bridge_remove_services
        assert(o2sm);
        free_outgoing(o2sm);
        O2_FREE(o2sm);
        DA_SET(o2sm_bridges, bridge_inst_ptr, i, NULL); // just to be safe
        O2_FREE(bi);
    }
    o2sm_bridge = NULL;
    DA_FINISH(o2sm_bridges);
    return O2_SUCCESS;
}


// Handler for !_o2/o2sm/sv message. This is to create/modify a
// service/tapper for o2sm client. Parameters are: id, service-name,
// exists-flag, service-flag, and tapper-or-properties string.
// This is almost identical to o2lite_sv_handler, but the message
// source is not o2n_message_source->application so it must be
// derived from id.
//
void o2sm_sv_handler(o2_msg_data_ptr msgdata, const char *types,
                        o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_err_t err = O2_SUCCESS;
    bridge_inst_ptr inst;
    o2sm_inst_ptr o2sm;

    O2_DBd(o2_dbg_msg("o2sm_sv_handler gets", NULL, msgdata, NULL, NULL));
    // get the arguments: shared mem bridge id, service name, 
    //     add-or-remove flag, is-service-or-tap flag, property string
    // assumes o2sm is initialized, but it must be
    // because the handler is installed
    int id = argv[0]->i32;
    const char *serv = argv[1]->s;
    bool add = argv[2]->i;
    bool is_service = argv[3]->i;
    const char *prtp = argv[4]->s;

    if (id < 0 || id >= o2sm_bridges.length) {
        goto bad_id;
    }
    inst = O2SM_BRIDGE(id);
    o2sm = (o2sm_inst_ptr) (inst->info);        
    if (!o2sm) {
        goto bad_id;
    }
    if (add) { // add a new service or tap
        if (is_service) {
            // copy the bridge inst and share o2sm info:
            bridge_inst_ptr bi = o2_bridge_inst_new(inst->proto, o2sm);
            // We claim shared mem clock is synchronized even if it is
            // not. This would result in timed messages being sent to 
            // an unsynchronized process, but that could only happen if
            // the local O2 process has clock synchronization, but if
            // that's true, then the shared mem process really IS 
            // synchronized, so it's not a problem.
            bi->tag = BRIDGE_SYNCED;
            err = o2_service_provider_new(serv, prtp, (o2_node_ptr) bi,
                                          o2_ctx->proc);
            if (err) { // oops, didn't work, so free allocations
                O2_FREE(bi);
            }
        } else { // add tap
            err = o2_tap_new(serv, o2_ctx->proc, prtp);
        }
    } else {
        if (is_service) { // remove a service
            err = o2_service_remove(serv, o2_ctx->proc, NULL, -1);
        } else { // remove a tap
            err = o2_tap_remove(serv, o2_ctx->proc, prtp);
        }
    }
    if (err) {
        char errmsg[100];
        snprintf(errmsg, 100, "o2sm/sv handler got %s for service %s",
                 o2_error_to_string(err), serv);
        o2_drop_msg_data(errmsg, msgdata);
    }
    return;
  bad_id:
    printf("o2sm_sv_handler got bad id: %d, message ignored\n", id);
}


void o2sm_fin_handler(o2_msg_data_ptr msgdata, const char *types,
                      o2_arg_ptr *argv, int argc, const void *user_data)
{
    bridge_inst_ptr inst;
    o2sm_inst_ptr o2sm;
    
    O2_DBd(o2_dbg_msg("o2sm_fin_handler gets", NULL, msgdata, NULL, NULL));
    int id = argv[0]->i32;
    if (id < 0 || id >= o2sm_bridges.length) {
        goto bad_id;
    }
    inst = O2SM_BRIDGE(id);
    o2sm = (o2sm_inst_ptr) (inst->info);
    if (!o2sm) {
        goto bad_id;
    }
    o2_bridge_inst_free(inst);
    return;
  bad_id:
    printf("o2sm_fin_handler got bad id: %d, message ignored\n", id);
}


o2_err_t o2_shmem_initialize()
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    if (o2sm_bridge) return O2_ALREADY_RUNNING; // already initialized
    DA_INIT(o2sm_bridges, bridge_inst_ptr, 1);
    o2sm_bridge = o2_bridge_new("o2sm", &shmem_bridge_poll, &shmem_bridge_send,
                                NULL /* recv */,
                                &o2_shmem_inst_finish, &o2_shmem_finish);
    o2_method_new_internal("/_o2/o2sm/sv", "isiis", &o2sm_sv_handler,
                           NULL, false, true);
    o2_method_new_internal("/_o2/o2sm/fin", "i", &o2sm_fin_handler,
                           NULL, false, true);
    return O2_SUCCESS;
}


/************* functions to be called from shared memory thread ************/

#include "sharedmemclient.h"

thread_local o2_message_ptr schedule_head;
thread_local o2_message_ptr schedule_tail;

o2_time o2sm_time_get()
{
    return (o2_clock_is_synchronized ?
            o2_local_time() + o2_global_offset : -1);
}


static int o2sm_get_id()
{
    bridge_inst_ptr bridge = o2_ctx->binst;
    o2sm_inst_ptr o2sm = (o2sm_inst_ptr) (bridge->info);
    return o2sm->bridged_process_index;
}


o2_err_t o2sm_service_new(const char *service, const char *properties)
{
    if (!properties) {
        properties = "";
    }
    return o2sm_send_cmd("!_o2/o2sm/sv", 0.0, "isiis", o2sm_get_id(),
                         service, true, true, properties);
}


int o2sm_method_new(const char *path, const char *typespec,
                    o2_method_handler h, void *user_data, 
                    bool coerce, bool parse)
{
    // o2_heapify result is declared as const, but if we don't share it, there's
    // no reason we can't write into it, so this is a safe cast to (char *):
    char *key = (char *) o2_heapify(path);
    *key = '/'; // force key's first character to be '/', not '!'
    // add path elements as tree nodes -- to get the keys, replace each
    // "/" with EOS and o2_heapify to copy it, then restore the "/"
    int ret = O2_NO_SERVICE;
#ifdef O2SM_PATTERNS
    handler_entry_ptr full_path_handler;
    char *remaining = key + 1;
    char name[NAME_BUF_LEN];

    char *slash = strchr(remaining, '/');
    if (slash) *slash = 0;
    services_entry_ptr services = *o2_services_find(remaining);
    // note that slash has not been restored (see o2_service_replace below)
    // services now is the existing services_entry node if it exists.
    // slash points to end of the service name in the path.

    if (!services) goto free_key_return; // cleanup and return because it is
                       // an error to add a method to a non-existent service
    // find the service offered by this process (o2_ctx->proc) --
    // the method should be attached to our local offering of the service
    service_provider_ptr spp = NULL;
    if (services->services.length > 0) {
        spp = DA_GET_ADDR(services->services, service_provider, 0);
    } else { // if we have no service local, this fails with O2_NO_SERVICE
        O2_FREE(key);
        return O2_NO_SERVICE;
    }
    o2_node_ptr node = spp->service;
    assert(node);    // we must have a local offering of the service
#endif

    handler_entry_ptr handler = O2_MALLOCT(handler_entry);
    handler->tag = NODE_HANDLER;
    handler->key = NULL; // gets set below with the final node of the address
    handler->handler = h;
    handler->user_data = user_data;
    handler->full_path = key;
    o2string types_copy = NULL;
    int types_len = 0;
    if (typespec) {
        types_copy = o2_heapify(typespec);
        if (!types_copy) goto error_return_2;
        // coerce to int to avoid compiler warning -- this could overflow but
        // only in cases where it would be impossible to construct a message
        types_len = (int) strlen(typespec);
    }
    handler->type_string = types_copy;
    handler->types_len = types_len;
    handler->coerce_flag = coerce;
    handler->parse_args = parse;
    
#ifdef O2SM_PATTERNS

    // case 1: method is global handler for entire service replacing a
    //         NODE_HASH with specific handlers: remove the NODE_HASH
    //         and insert a new NODE_HANDLER as local service.
    // case 2: method is a global handler, replacing an existing global handler:
    //         same as case 1 so we can use o2_service_replace to clean up the
    //         old handler rather than duplicate that code.
    // case 3: method is a specific handler and a global handler exists:
    //         replace the global handler with a NODE_HASH and continue to 
    //         case 4
    // case 4: method is a specific handler and a NODE_HASH exists as the
    //         local service: build the path in the tree according to the
    //         the remaining address string

    // slash here means path has nodes, e.g. /serv/foo vs. just /serv
    if (!slash) { // (cases 1 and 2: install new global handler)
        handler->key = NULL;
        handler->full_path = NULL;
        ret = o2_service_provider_replace(key + 1, &spp->service,
                                          (o2_node_ptr) handler);
        goto free_key_return; // do not need full path for global handler
    }

    // cases 3 and 4: path has nodes. If service is a NODE_HANDLER, 
    //   replace with NODE_HASH
    hash_node_ptr hnode = (hash_node_ptr) node;
    if (hnode->tag == NODE_HANDLER) {
        // change global handler to an empty hash_node
        hnode = o2_hash_node_new(NULL); // top-level key is NULL
        if (!hnode) goto error_return_3;
        if ((ret = o2_service_provider_replace(key + 1, &spp->service,
                                               (o2_node_ptr) hnode))) {
            goto error_return_3;
        }
    }
    // now hnode is the root of a path tree for all paths for this service
    assert(slash);
    *slash = '/'; // restore the full path in key
    remaining = slash + 1;

    // support pattern matching by adding this path to the path tree
    while ((slash = strchr(remaining, '/'))) {
        *slash = 0; // terminate the string at the "/"
        o2_string_pad(name, remaining);
        *slash = '/'; // restore the string
        remaining = slash + 1;
        // if necessary, allocate a new entry for name
        hnode = o2_tree_insert_node(hnode, name);
        assert(hnode);
        o2_mem_check(hnode);
        // node is now the node for the path up to name
    }
    // node is now where we should put the final path name with the handler;
    // remaining points to the final segment of the path
    handler->key = o2_heapify(remaining);
    if ((ret = o2_node_add(hnode, (o2_node_ptr) handler))) {
        goto error_return_3;
    }
    // make an entry for the full path table
    full_path_handler = O2_MALLOCT(handler_entry);
    memcpy(full_path_handler, handler, sizeof *handler); // copy info
    if (types_copy) types_copy = o2_heapify(typespec);
    full_path_handler->type_string = types_copy;
    handler = full_path_handler;
#else // if O2_NO_PATTERNS:
    handler->key = handler->full_path;
    handler->full_path = NULL;
#endif
    // put the entry in the full path table
    ret = o2_node_add(&o2_ctx->full_path_table, (o2_node_ptr) handler);
    goto just_return;
#ifdef O2SM_PATTERNS
  error_return_3:
    if (types_copy) O2_FREE((void *) types_copy);
#endif
  error_return_2:
    O2_FREE(handler);
#ifdef O2SM_PATTERNS
  free_key_return: // not necessarily an error (case 1 & 2)
    O2_FREE(key);
#endif
  just_return:
    return ret;
}


static void append_to_schedule(o2_message_ptr msg)
{
    if (schedule_head == NULL) {
        schedule_head = schedule_tail = msg;
    } else {
        schedule_tail->next = msg;
        msg->next = NULL;
        schedule_tail = msg;
    }
}


o2_err_t o2sm_message_send(o2_message_ptr msg)
{
    o2_queue_push(&o2sm_incoming, (o2_obj_ptr) msg);
    return O2_SUCCESS;
}


o2_err_t o2sm_send_finish(o2_time time, const char *address, int tcp_flag)
{
    o2_message_ptr msg = o2_message_finish(time, address, tcp_flag);
    if (!msg) return O2_FAIL;
    return o2sm_message_send(msg);
}


o2_err_t o2sm_send_marker(const char *path, double time, int tcp_flag,
                          const char *typestring, ...)
{
    va_list ap;
    va_start(ap, typestring);

    o2_message_ptr msg;
    o2_err_t rslt = o2_message_build(&msg, time, NULL, path,
                                       typestring, tcp_flag, ap);
    if (rslt != O2_SUCCESS) {
        return rslt; // could not allocate a message!
    }
    return o2sm_message_send(msg);
}


int o2sm_dispatch(o2_message_ptr msg)
{
#ifdef O2SM_PATTERNS
    o2_node_ptr service = o2_msg_service(&msg->data, &services);
    if (service) {
#endif
        char *address = msg->data.address;
    
        // STEP 2: Isolate the type string, which is after the address
        const char *types = o2_msg_types(msg);

#ifdef O2SM_PATTERNS
        // STEP 3: If service is a Handler, call the handler directly
        if (service->tag == NODE_HANDLER) {
            o2_call_handler((handler_entry_ptr) service, &msg->data, types);

        // STEP 4: If path begins with '!', or O2_NO_PATTERNS, full path lookup
        } else if (service->tag == NODE_HASH
                    && (address[0] == '!')
                   ) {
#endif
            o2_node_ptr handler;
            address[0] = '/'; // must start with '/' to get consistent hash
            handler = *o2_lookup(&o2_ctx->full_path_table, address);
            if (handler && handler->tag == NODE_HANDLER) {
                o2_call_handler((handler_entry_ptr) handler,
                                &msg->data, types);
            }
#ifdef O2SM_PATTERNS
        }
        // STEP 5: Use path tree to find handler
        else if (service->tag == NODE_HASH) {
            char name[NAME_BUF_LEN];
            address = strchr(address + 1, '/'); // search for end of service name
            if (address) {
                o2_find_handlers_rec(address + 1, name,
                                   (o2_node_ptr) service, &msg->data, types);
            }
        }
    }
#endif
    O2_FREE(msg);
    return O2_SUCCESS;
}


// This polling routine drives communication and is called from the
// shared memory process thread
void o2sm_poll()
{
    o2_time now = o2sm_time_get();
    o2sm_inst_ptr o2sm = (o2sm_inst_ptr) (o2_ctx->binst->info);
    extern bridge_inst_ptr smbridge;
    o2_message_ptr msgs = get_messages_reversed(OUTGOING(o2sm));
    o2_message_ptr next;
    // sort msgs into immediate and schedule
    o2_message_ptr *prevptr = &msgs;
    while (*prevptr) {
        if ((*prevptr)->data.timestamp != 0) {
            next = (*prevptr)->next;
            append_to_schedule(*prevptr);
            *prevptr = next;
        } else {
            prevptr = &(*prevptr)->next;
        }
    }
    // msgs is left with zero timestamp messages
    if (now < 0) { // no clock! free the messages
        while (schedule_head) {
            next = schedule_head->next;
            O2_FREE(schedule_head);
            schedule_head = next;
        }
    } else { // send timestamped messages that are ready to go
        while (schedule_head && schedule_head->data.timestamp < now) {
            next = schedule_head->next;
            o2sm_dispatch(schedule_head);
            schedule_head = next;
        }
    }
    while (msgs) { // send all zero-timestamp messages
        next = msgs->next;
        o2sm_dispatch(msgs);
        msgs = next;
    }
}


void o2sm_initialize(o2_ctx_ptr ctx, bridge_inst_ptr inst)
{
    // local memory allocation will use malloc() to get a chunk on the
    // first call to O2_MALLOC by the shared memory thread. If
    // o2_memory() was used called with mallocp = false, the thread
    // will fail to allocate any memory, so mallocp must be true (default).
    ctx->chunk = NULL;
    ctx->chunk_remaining = 0;
    o2_ctx_init(ctx);
    o2_ctx->binst = inst;

    schedule_head = NULL;
    schedule_tail = NULL;
}


void o2sm_finish()
{
    // make message before we free the message construction area
    o2_send_start();
    o2_add_int32(o2sm_get_id());
    o2_message_ptr msg = o2_message_finish(0.0, "/_o2/o2sm/fin", true);
    // free the o2_ctx data
    o2_hash_node_finish(&o2_ctx->path_tree);
    o2_hash_node_finish(&o2_ctx->full_path_table);
    o2_argv_finish();
    o2_ctx = NULL;
    // notify O2 to remove bridge: does not require o2_ctx
    o2sm_message_send(msg);
}


#endif

