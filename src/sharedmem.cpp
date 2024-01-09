// sharedmem.cpp -- a brige to shared memory O2 service
//
// Roger B. Dannenberg
// August 2020

/*
Supports multiple connections to shared memory processes.
All shared memory processes use the same heap, and O2_MALLOC
is lock-free and thread-safe.

Therefore, O2message types can be transferred directly to
shared memory queues without byte-swapping, copying, or 
changing format.

The implementation is based on o2lite. Instead of O2lite_info
containing a Fds_info pointer (for the TCP connection) and the
udp_address, the O2sm_info contains an outgoing message queue.

Services provided by a shared memory process appear locally in the
services array entry as an O2sm_info, where messages can be directly
enqueued, making delivery quite fast and simple.

Received messages are all enqueued on a global o2sm_incoming queue,
which is checked by o2sm_poll. If messages are found, the entire queue
is atomically copied to a delivery queue, reversed, and then messages
are delivered to O2 in the correct order. Thus, O2sm_info do not
receive or deliver incoming messages to O2 -- it's handled by the
O2sm_protocol object.

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

Timing in shared memory process is simpler and more limited than in
O2. Incoming messages with timestamps must arrive in time order.  A
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

Ownership: Every shared memory process has an O2_context and an
O2sm_info. The O2sm_info is owned by the O2 thread because the
O2 thread manages all bridges and when bridges or protocols shut
down, the O2 thread deletes bridge protocols and bridge instances.
The O2_context is owned by the O2SM thread and is often in static
memory. The O2_context has a pointer to the O2sm_info instance
where messages are queued from O2 to O2SM.

Shutting down the O2SM thread should be initiated by the O2SM thread
by calling o2sm_finish(), which finishes the O2SM context and sends
an /_o2/o2sm/fin message to O2. The application should shut down
O2SM threads before calling o2_finish(). Otherwise, there will be
contexts owned by O2SM threads with references to O2sm_info
instances. If the O2 thread deletes them, it leaves dangling
pointers that could crash O2SM threads. If O2 thread leaves them,
then memory is leaked and the O2SM thread will crash anyway because
o2_finish will free the O2 heap.

There is a synchronization issue: When the O2 thread wants to finish
or exit, how does it know when all O2SM threads have finished? The
application is responsible for shutting down O2SM threads *before*
calling o2_finish. Normally, this should be done by sending messages.
There is no standard message or built-in mechanism for this, since
creating and exiting threads is not an O2 operation. After all
O2SM threads call o2sm_finish() and their fin messages are handled,
there will be no more O2sm_info instances, so the O2 thread should
wait for that. While waiting, the O2 thread must call o2_poll() to
receive and handle fin messages. To check for instances, use
o2_shmem_inst_count().

o2_shmem_initialize() - Initially, an array of O2sm_info* is
    created, a new bridge protocol for "o2sm" is created, and a
    handler is created for /_o2/o2sm/sv and /_o2/o2sm/fin. (O2 thread)

o2_shmem_inst_new() - creates a new O2sm_info. The O2sm_info must be
    passed to the O2SM thread. It is also stored in the o2sm_bridges
    array. (O2 thread)
 
o2_shmem_inst_count() - returns the number of shared memory instances.

o2sm_initialize() - installs an O2_context for the O2SM thread and
    retains the Bridge_info* which contains a message queue for
    messages from O2SM to O2*. The O2_context contains mappings from
    addresses to handlers in path_tree and full_path_table.  (O2SM
    thread)

o2sm_get_id() - returns a unique ID for this bridged process. This
    might be useful if you want to create a unique service that does
    not conflict with any host services or services by other shared
    memory or other bridged processes using other protocols,
    e.g. o2lite. Note that *all* bridged processes and their host
    share must have non-conflicting service names. While other full O2
    processes can offer duplicated service names (and messages are
    directed to the service provider with the highest pip:iip:port
    name), duplicates are not allowed between hosts and their bridged
    processes.  (O2SM thread)

o2sm_service_new() - creates handlers on the O2 side via /_o2/o2sm/sv
    messages. (O2SM thread)

o2sm_method_new() - inserts handlers into the O2_context mappings.
    (O2SM thread)

o2sm_finish() - To shut down cleanly, first the O2SM thread should
    stop calling o2sm_poll() and call o2sm_finish(), which frees the
    O2SM O2_context structures (but not the O2sm_info), and calls
    /_o2/o2sm/fin with the id as parameter. (O2SM thread)

o2_shmem_finish() - shuts down the entire "o2sm" protocol. First, 
    o2sm_bridges is searched and any instance there is deleted by
    calling o2_shmem_inst_finish(). Then o2sm_bridges is freed.

Typical shared memory process organization is as follows:

#include "o2internal.h"  // O2_context is not defined in o2.h, so use this
#include "sharedmem.h"
Bridge_info *smbridge = NULL; // global variable accessed by both threads

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
    O2_context ctx;
    o2sm_initialize(&ctx, smbridge); // connects us to bridge
    ... run the thread ...
    o2sm_finish();
    return NULL;
}
*/

#ifndef O2_NO_SHAREDMEM
#include "o2internal.h"
#include "o2atomic.h"
#include "services.h"
#include "message.h"
#include "msgsend.h"
#include "pathtree.h"
#include "sharedmem.h"

O2queue o2sm_incoming;

Bridge_protocol *o2sm_protocol = NULL;

// Call to establish a connection from a shared memory process to 
// O2. This runs in the O2 thread.
// 
O2sm_info *o2_shmem_inst_new()
{
    assert(o2sm_protocol);  // did you remember to call o2_shmem_initialize()?
    return new O2sm_info();
}


int o2_shmem_inst_count()
{
    return o2sm_protocol ? o2sm_protocol->instances.size() : 0;
}

// retrieve all messages from head atomically. Then reverse the list.
//
O2message_ptr get_messages_reversed(O2queue *head)
{
    // store a zero if nothing has changed
    O2message_ptr all = (O2message_ptr) head->grab();
    
    O2message_ptr msgs = NULL;
    O2message_ptr next = NULL;
    while (all) {
        next = all->next;
        all->next = msgs;
        msgs = all;
        all = next;
    }
    return msgs;
}


// Handler for !_o2/o2sm/sv message. This is to create/modify a
// service/tapper for o2sm client. Parameters are: ID, service-name,
// exists-flag, service-flag, and tapper-or-properties string.
// This is almost identical to o2lite_sv_handler.
//
static void o2sm_sv_handler(O2msg_data_ptr msgdata, const char *types,
                            O2arg_ptr *argv, int argc, const void *user_data)
{
    O2err rslt = O2_SUCCESS;

    O2_DBd(o2_dbg_msg("o2sm_sv_handler gets", NULL, msgdata, NULL, NULL));
    // get the arguments: shared mem bridge id, service name, 
    //     add-or-remove flag, is-service-or-tap flag, property string
    // assumes o2sm is initialized, but it must be
    // because the handler is installed
    int id = argv[0]->i;
    const char *serv = argv[1]->s;
    bool add = argv[2]->i;
    bool is_service = argv[3]->i;
    const char *prtp = argv[4]->s;
    O2tap_send_mode send_mode = (O2tap_send_mode) (argv[5]->i32);
    o2_message_source = o2sm_protocol->find(id);
    if (!o2_message_source) {
        o2_drop_msg_data("o2sm_sv_handler could not locate O2sm_info", msgdata);
        return;
    }

    if (add) { // add a new service or tap
        if (is_service) {
            rslt = Services_entry::service_provider_new(
                    serv, prtp, o2_message_source, o2_ctx->proc);
        } else { // add tap
            rslt = o2_tap_new(serv, o2_ctx->proc, prtp, send_mode);
        }
    } else {
        if (is_service) { // remove a service
            rslt = Services_entry::proc_service_remove(
                          serv, o2_ctx->proc, NULL, -1);
        } else { // remove a tap
            rslt = o2_tap_remove(serv, o2_ctx->proc, prtp);
        }
    }
    if (rslt) {
        char errmsg[100];
        snprintf(errmsg, 100, "o2sm/sv handler got %s for service %s",
                 o2_error_to_string(rslt), serv);
        o2_drop_msg_data(errmsg, msgdata);
    }
    return;
}


// Handler for "/_o2/o2sm/fin" message
static void o2sm_fin_handler(O2msg_data_ptr msgdata, const char *types,
                             O2arg_ptr *argv, int argc, const void *user_data)
{
    O2_DBb(o2_dbg_msg("o2sm_fin_handler gets", NULL, msgdata, NULL, NULL));
    O2sm_info *info = (O2sm_info *) o2sm_protocol->find(argv[0]->i);
    if (info) {
        info->o2_delete();
    }
    return;
}


O2err o2_shmem_initialize()
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    if (o2sm_protocol) return O2_ALREADY_RUNNING; // already initialized
    o2sm_protocol = new O2sm_protocol();
    o2_method_new_internal("/_o2/o2sm/sv", "isiisi", &o2sm_sv_handler,
                           NULL, false, true);
    o2_method_new_internal("/_o2/o2sm/fin", "i", &o2sm_fin_handler,
                           NULL, false, true);
    return O2_SUCCESS;
}


/************* functions to be called from shared memory thread ************/

#include "sharedmemclient.h"

O2time o2sm_time_get()
{
    return (o2_clock_is_synchronized ?
            o2_local_time() + o2_global_offset : -1);
}


O2err o2sm_service_new(const char *service, const char *properties)
{
    if (!properties) {
        properties = "";
    } else assert(properties[0] == ';');
    return o2sm_send_cmd("!_o2/o2sm/sv", 0.0, "isiisi", o2_ctx->binst->id,
                         service, true, true, properties, 0);
}


O2err o2sm_method_new(const char *path, const char *typespec,
                    O2method_handler h, void *user_data, 
                    bool coerce, bool parse)
{
    // o2_heapify result is declared as const, but if we don't share it, there's
    // no reason we can't write into it, so this is a safe cast to (char *):
    char *key = (char *) o2_heapify(path);
    *key = '/'; // force key's first character to be '/', not '!'
    // add path elements as tree nodes -- to get the keys, replace each
    // "/" with EOS and o2_heapify to copy it, then restore the "/"
    O2err ret = O2_NO_SERVICE;
#ifdef O2SM_PATTERNS
    Handler_entry *full_path_handler;
    char *remaining = key + 1;
    char name[NAME_BUF_LEN];

    char *slash = strchr(remaining, '/');
    if (slash) *slash = 0;
    Services_entry **services = Services_entry::find(remaining);
    // but with a shared memory thread, we don't support multiple service
    // providers, so services is either NULL, a Handler_entry, or a Hash_node
    O2node *node = (O2node *) *services;
    // note that slash has not been restored (see o2_service_replace below)
    // services now is the existing services_entry node if it exists.
    // slash points to end of the service name in the path.

    if (!node) goto free_key_return; // cleanup and return
#endif

    O2string types_copy = NULL;
    int types_len = 0;
    if (typespec) {
        types_copy = o2_heapify(typespec);
        if (!types_copy) goto free_key_return;
        // coerce to int to avoid compiler warning -- this could overflow but
        // only in cases where it would be impossible to construct a message
        types_len = (int) strlen(typespec);
    }
    Handler_entry *handler;
    handler = new Handler_entry(NULL, h, user_data, key, types_copy,
                                types_len, coerce, parse);
            // key gets set below with the final node of the address 
            
    
#ifdef O2SM_PATTERNS

    // case 1: method is global handler for entire service replacing a
    //         Hash_node with specific handlers: remove the Hash_node
    //         and insert a new O2TAG_HANDLER as local service.
    // case 2: method is a global handler, replacing an existing global handler:
    //         same as case 1 so we can use o2_service_replace to clean up the
    //         old handler rather than duplicate that code.
    // case 3: method is a specific handler and a global handler exists:
    //         replace the global handler with a Hash_node and continue to
    //         case 4
    // case 4: method is a specific handler and a Hash_node exists as the
    //         local service: build the path in the tree according to the
    //         the remaining address string

    // support pattern matching by adding this path to the path tree
    while ((slash = strchr(remaining, '/'))) {
        *slash = 0; // terminate the string at the "/"
        o2_string_pad(name, remaining);
        *slash = '/'; // restore the string
        remaining = slash + 1;
        // if necessary, allocate a new entry for name
        hnode = hnode.tree_insert_node(name);
        assert(hnode);
        o2_mem_check(hnode);
        // node is now the node for the path up to name
    }
    // node is now where we should put the final path name with the handler;
    // remaining points to the final segment of the path
    handler->key = o2_heapify(remaining);
    if ((ret = o2_node_add(hnode->insert(handler))) {
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
    return o2_ctx->full_path_table.insert(handler);
#ifdef O2SM_PATTERNS
  error_return_3:
    if (types_copy) O2_FREE((void *) types_copy);
#endif
    O2_FREE(handler);
  free_key_return: // not necessarily an error (case 1 & 2)
    O2_FREE(key);
    return ret;
}


static void append_to_schedule(O2message_ptr msg)
{
    msg->next = NULL;
    if (o2_ctx->schedule_head == NULL) {
        o2_ctx->schedule_head = o2_ctx->schedule_tail = msg;
    } else {
        o2_ctx->schedule_tail->next = msg;
        o2_ctx->schedule_tail = msg;
    }
}


O2err o2sm_message_send(O2message_ptr msg)
{
    o2sm_incoming.push((O2list_elem *) msg);
    return O2_SUCCESS;
}


O2err o2sm_send_finish(O2time time, const char *address, bool tcp_flag)
{
    O2message_ptr msg = o2_message_finish(time, address, tcp_flag);
    if (!msg) return O2_FAIL;
    return o2sm_message_send(msg);
}


O2err o2sm_send_marker(const char *path, double time, bool tcp_flag,
                          const char *typestring, ...)
{
    va_list ap;
    va_start(ap, typestring);

    O2message_ptr msg;
    O2err rslt = o2_message_build(&msg, time, NULL, path,
                                  typestring, tcp_flag, ap);
    if (rslt != O2_SUCCESS) {
        return rslt; // could not allocate a message!
    }
    return o2sm_message_send(msg);
}


int o2sm_dispatch(O2message_ptr msg)
{
    bool delivered = false;
    // printf("o2sm_dispatch %s\n", msg->data.address);
    assert(msg->data.address[0] == '/' || msg->data.address[0] == '!');
#ifdef O2SM_PATTERNS
    O2node *service = o2_msg_service(&msg->data, &services);
    if (service) {
#endif
        char *address = msg->data.address;
    
        // STEP 2: Isolate the type string, which is after the address
        const char *types = o2_msg_types(msg);

#ifdef O2SM_PATTERNS
        // STEP 3: If service is a Handler, call the handler directly
        if (ISA_HANDLER(service)) {
            o2_call_handler((handler_entry_ptr) service, &msg->data, types);
            delivered = true;
        // STEP 4: If path begins with '!', or O2_NO_PATTERNS, full path lookup
        } else if (ISA_HASH(service) && (address[0] == '!')) {
#endif
            O2node *handler;
            address[0] = '/'; // must start with '/' to get consistent hash
            handler = *o2_ctx->full_path_table.lookup(address);
            if (handler && ISA_HANDLER(handler)) {
                TO_HANDLER_ENTRY(handler)->invoke(&msg->data, types);
                delivered = true;
            }
#ifdef O2SM_PATTERNS
        }
        // STEP 5: Use path tree to find handler
        else if (ISA_HASH(service)) {
            char name[NAME_BUF_LEN];
            address = strchr(address + 1, '/'); // search for end of srvc name
            if (address) {
                delivered = o2_find_handlers_rec(address + 1, name,
                                   (o2_node_ptr) service, &msg->data, types);
            }
        }
    }
#endif
    if (!delivered) {
        o2_drop_msg_data("no handler was found", &msg->data);
    }
    O2_FREE(msg);
    return O2_SUCCESS;
}


// This polling routine drives communication and is called from the
// shared memory process thread
void o2sm_poll()
{
    O2sm_info *o2sm = (O2sm_info *) (o2_ctx->binst);
    o2sm->poll_outgoing();
}


// Here, the o2sm thread polls for messages coming from the O2 process
void O2sm_info::poll_outgoing()
{
    O2time now = o2sm_time_get();
    O2message_ptr msgs = get_messages_reversed(&outgoing);
    O2message_ptr next;
    // sort msgs into immediate and schedule
    O2message_ptr *prevptr = &msgs;
    while (*prevptr) {
        // printf("poll_outgoing msg %p\n", *prevptr);
        if ((*prevptr)->data.timestamp != 0) {
            next = (*prevptr)->next;
            append_to_schedule(*prevptr);
            *prevptr = next;
        } else {
            prevptr = &(*prevptr)->next;
        }
    }
    // msgs is left with zero timestamp messages
    O2message_ptr head = o2_ctx->schedule_head;
    if (now < 0) { // no clock! free the messages
        while (head) {
            O2_DBB(o2_dbg_msg("Incoming to shmem thread dropped for no clock",
                              head, &head->data, NULL, NULL));
            next = head->next;
            O2_FREE(head);
            head = next;
        }
    } else { // send timestamped messages that are ready to go
        while (head && head->data.timestamp < now) {
            O2_DBB(o2_dbg_msg("Incoming to shmem thread ready now",
                              head, &head->data, NULL, NULL));
            next = head->next;
            o2sm_dispatch(head);
            head = next;
        }
    }
    o2_ctx->schedule_head = head;
    while (msgs) { // send all zero-timestamp messages
        O2_DBB(o2_dbg_msg("Incoming to shmem thread zero timestamp",
                          msgs, &msgs->data, NULL, NULL));
        next = msgs->next;
        o2sm_dispatch(msgs);
        msgs = next;
    }
}


void o2sm_initialize(O2_context *ctx, Bridge_info *inst)
{
    O2_DBb(printf("%s o2sm_initialize ctx %p Bridge_info %p\n",
                  o2_debug_prefix, ctx, inst));
    o2_ctx = ctx;
    // local memory allocation will use malloc() to get a chunk on the
    // first call to O2_MALLOC by the shared memory thread. If
    // o2_memory() was called with mallocp = false, the thread
    // will fail to allocate any memory.
    //     Therefore, if mallocp is false, you should:
    //         o2_ctx->chunk = <chunk of memory for o2sm allocations when
    //                          freelists do not have suitable free objects>
    //         o2_ctx->remaining = <size of o2_ctx->chunk>
    // The chunk will not be freed by O2 and should either be static or it
    // should not be freed until O2 finishes. (Note that the lifetime of this
    // chunk is longer than the lifetime of the shared memory thread because
    // memory gets passed around as messages.)

    o2_ctx->proc = NULL;
    o2_ctx->binst = inst;
}


void o2sm_finish()
{
    assert(o2_ctx);
    assert(o2_ctx->binst);
    // make message before we free the message construction area
    o2_send_start();
    o2_add_int32(o2_ctx->binst->id);
    O2message_ptr msg = o2_message_finish(0.0, "/_o2/o2sm/fin", true);
    // free the o2_ctx data
    O2_DBb(printf("%s o2sm_finish finishing O2_context@%p\n",
                  o2_debug_prefix, o2_ctx));
    o2_ctx->finish();
    o2_ctx = NULL;
    // notify O2 to remove bridge: does not require o2_ctx
    o2sm_message_send(msg);
}


#endif

