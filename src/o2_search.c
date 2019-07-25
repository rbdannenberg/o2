//
//  o2_search.c
//  o2
//
//  Created by 弛张 on 3/14/16.
//
//
// delivery is recursive due to bundles. Here's an overview of the structure:
//
// o2_message_send_sched(o2_message_ptr msg, int schedulable)
//         (Defined in o2_send.c)
//         Determines local or remote O2 or OSC
//         If remote, calls o2_send_remote()
//         If local and future and schedulable, calls o2_schedule()
//         Otherwise, calls o2_msg_data_deliver()
//         Caller gives msg to callee. msg is freed eventually.
// o2_send_remote(o2_msg_data_ptr msg, int tcp_flag, fds_info_ptr info)
//         Sends msg_data to remote service
// o2_msg_data_send(o2_msg_data_ptr msg, int tcp_flag)
//         (Defined in o2_send.c)
//         Determines local or remote O2 or OSC
//         If remote, calls o2_send_remote()
//         If OSC, calls sends data as UDP
//         If local but future, builds a message and calls
//         o2_schedule().
//         If local, calls o2_msg_data_deliver()
// o2_schedule(o2_sched_ptr sched, o2_message_ptr msg)
//         (Defined in o2_sched.c)
//         msg could be local or for delivery to an OSC server
//         Caller gives msg to callee. msg is freed eventually.
//         Calls o2_message_send_sched(msg, FALSE) to send message
//         (FALSE prevents a locally scheduled message you are trying
//         to dispatch from being considered for scheduling using the
//         o2_gtsched, which may not be sync'ed yet.)
// o2_msg_data_deliver(o2_msg_data_ptr msg, int tcp_flag,
//                     o2_node_ptr service)
//         delivers a message or bundle locally. Calls
//         o2_embedded_msgs_deliver() if this is a bundle.
//         Otherwise, uses find_and_call_handlers_rec().
// o2_embedded_msgs_deliver(o2_msg_data_ptr msg, int tcp_flag)
//         Deliver or schedule messages in a bundle (recursively).
//         Calls o2_msg_data_send() to deliver each embedded message
//         message, which copies the element into an o2_message if needed.
//
// Message parsing and forming o2_argv with message parameters is not
// reentrant since there is a global buffer used to store coerced
// parameters. Therefore, if you call a handler and the handler sends a
// message, we cannot deliver it immediately, at least not if it has a
// local destination. Therefore, messages sent from handlers may be
// saved on a list and dispatched later. 


#include <stdio.h>
#include "o2_internal.h"
#include "o2_message.h"
#include "o2_discovery.h"
#include "o2_send.h"
#include "o2_sched.h"

#ifdef WIN32
#include "malloc.h"
#define allloca _alloca
#endif

thread_local o2_context_ptr o2_context = NULL;

static void entry_free(o2_node_ptr entry);
static int o2_pattern_match(const char *str, const char *p);
static int entry_remove(hash_node_ptr node, o2_node_ptr *child, int resize);
static int remove_method_from_tree(char *remaining, char *name,
                                   hash_node_ptr node);
static int remove_node(hash_node_ptr node, o2string key);
static int remove_remote_services(o2n_info_ptr info);
static int remove_taps_by(o2n_info_ptr info);
static int resize_table(hash_node_ptr node, int new_locs);
static hash_node_ptr tree_insert_node(hash_node_ptr node, o2string key);



#if IS_LITTLE_ENDIAN
// for little endian machines
#define STRING_EOS_MASK 0xFF000000
#else
#define STRING_EOS_MASK 0x000000FF
#endif
#define SCRAMBLE 2686453351680

#define MAX_SERVICE_NUM  1024


// enumerate is used to visit all entries in a hash table
// it is used for:
// - enumerating services with status change when we become the
//       master clock process
// - enumerating all services offered by a given process when
//       that process's clock status changes
// - enumerating local services to send to another process
// - enumerating node entries in o2_info_show
// - enumerating node entries for pattern matching an address
// - enumerating all services to look for tappers that 
//       match a deleted process
// - enumerating all services to look for services offered by
//       a deleted process
// - enumerating all entries to rehash them into a different
//       size of table
// - enumerating services that belong to process to show service
//       names in o2_sockets_show()

void o2_enumerate_begin(enumerate_ptr enumerator, dyn_array_ptr dict)
{
    enumerator->dict = dict;
    enumerator->index = 0;
    enumerator->entry = NULL;
}


// return next entry from table. Entries can be inserted into
// a new table because enumerate_next does not depend upon the
// pointers in each entry once the entry is enumerated.
//
o2_node_ptr o2_enumerate_next(enumerate_ptr enumerator)
{
    while (!enumerator->entry) {
        int i = enumerator->index++;
        if (i >= enumerator->dict->length) {
            return NULL; // no more entries
        }
        enumerator->entry = *DA_GET(*(enumerator->dict),
                                    o2_node_ptr, i);
    }
    o2_node_ptr ret = enumerator->entry;
    enumerator->entry = enumerator->entry->next;
    return ret;
}

#ifndef O2_NO_DEBUGGING
static const char *entry_tags[6] = { "NODE_HASH", "NODE_HANDLER", "NODE_SERVICES", "NODE_TAP",
                                     "NODE_OSC_REMOTE_SERVICE", "NODE_BRIDGE_SERVICE" };
static const char *info_tags[10] = { "INFO_TCP_SERVER", "INFO_TCP_NOMSGYET", "INFO_TCP_NOCLOCK",
                                     "INFO_TCP_SOCKET", "INFO_UDP_SOCKET", "INFO_OSC_UDP_SERVER",
                                     "INFO_OSC_TCP_SERVER", "INFO_OSC_TCP_CONNECTION",
                                     "INFO_OSC_TCP_CONNECTING", "INFO_OSC_TCP_CLIENT" };
const char *o2_tag_to_string(int tag)
{
    if (tag <= NODE_BRIDGE_SERVICE) return entry_tags[tag - NODE_HASH];
    if (tag >= INFO_TCP_SERVER && tag <= INFO_OSC_TCP_CLIENT)
        return info_tags[tag - INFO_TCP_SERVER];
    static char unknown[32];
    snprintf(unknown, 32, "Tag-%d", tag);
    return unknown;
}
#endif 

#ifdef SEARCH_DEBUG
// debugging code to print o2_node and o2_info structures
void o2_info_show(o2n_info_ptr info, int indent)
{
    for (int i = 0; i < indent; i++) printf("  ");
    printf("%s@%p", o2_tag_to_string(info->tag), info);
    if (info->tag == NODE_HASH || info->tag == NODE_HANDLER ||
        info->tag == NODE_SERVICES) {
        o2_node_ptr entry = (o2_node_ptr) info;
        if (entry->key) printf(" key=%s", entry->key);
    }
    if (info->tag == NODE_HASH) {
        printf("\n");
        hash_node_ptr node = (hash_node_ptr) info;
        enumerate en;
        o2_enumerate_begin(&en, &(node->children));
        o2_node_ptr entry;
        while ((entry = o2_enumerate_next(&en))) {
            // see if each entry can be found
#ifdef NDEBUG
            o2_lookup(node, entry->key);
#else
            o2_node_ptr *ptr = o2_lookup(node, entry->key);
            if (*ptr != entry)
                printf("ERROR: *ptr %p != entry %p\n", *ptr, entry);
#endif
            o2_info_show((o2n_info_ptr) entry, indent + 1);
        }
    } else if (info->tag == NODE_SERVICES) {
        services_entry_ptr s = (services_entry_ptr) info;
        printf("\n");
        for (int j = 0; j < s->services.length; j++) {
            o2_info_show((o2n_info_ptr) GET_SERVICE(s->services, j), indent + 1);
        }
    } else if (info->tag == NODE_HANDLER) {
        handler_entry_ptr h = (handler_entry_ptr) info;
        if (h->full_path) printf(" full_path=%s", h->full_path);
        printf("\n");
    } else if (info->tag == INFO_TCP_SOCKET) {
        printf(" port=%d name=%s\n", info->proc.udp_port, info->proc.name);
    } else {
        printf("\n");
    }
}
#endif


// o2_add_entry_at inserts an entry into the hash table. If the
// table becomes too full, a new larger table is created. 
// This function is called after o2_lookup() has been used to
// determine a pointer to the new entry. This pointer is
// passed in loc.
//
int o2_add_entry_at(hash_node_ptr node, o2_node_ptr *loc,
                    o2_node_ptr entry)
{
    node->num_children++;
    entry->next = *loc;
    
    *loc = entry;
    // expand table if it is too small
    if (node->num_children * 3 > node->children.length * 2) {
        return resize_table(node, node->num_children * 3);
    }
    return O2_SUCCESS;
}


// call handler for message. Does type coercion, argument vector
// construction, and type checking. types points to the type string
// after the initial ','
//
// Design note: We could find types by scanning over the address in
// msg, but since address pattern matching already scans over most
// of the address, it's faster (I think) for the caller to compute
// types and pass it in. An exception is the case where we do a hash
// lookup of the full address. In that case the caller has to scan
// over the whole address (4 bytes at a time) to find types in order
// to pass it in.
//
static void call_handler(handler_entry_ptr handler, o2_msg_data_ptr msg,
                         const char *types)
{
    // coerce to avoid compiler warning -- even 2^31 is absurdly big
    //     for the type string length
    int types_len = (int) strlen(types);

    // type checking
    if (handler->type_string && // mismatch detection needs type_string
        ((handler->types_len != types_len) || // first check if counts are equal
         !(handler->coerce_flag ||  // need coercion or exact match
           (streql(handler->type_string, types))))) {
        // printf("!!! %s: call_handler skipping %s due to type mismatch\n",
        //        o2_debug_prefix, msg->address);
        return; // type mismatch
    }

    if (handler->parse_args) {
        o2_extract_start(msg);
        o2string typ = handler->type_string;
        if (!typ) { // if handler type_string is NULL, use message types
            typ = types;
        }
        while (*typ) {
            o2_arg_ptr next = o2_get_next(*typ++);
            if (!next) {
                return; // type mismatch, do not deliver the message
            }
        }
        if (handler->type_string) {
            types = handler->type_string; // so that handler gets coerced types
        }
        assert(o2_context->arg_data.allocated >= o2_context->arg_data.length);
        assert(o2_context->argv_data.allocated >= o2_context->argv_data.length);
    } else {
        o2_context->argv = NULL;
        o2_context->argc = 0;
    }
    (*(handler->handler))(msg, types, o2_context->argv, o2_context->argc, handler->user_data);
}


// create a node in the path tree
//
// key is "owned" by caller
//
hash_node_ptr o2_hash_node_new(const char *key)
{
    hash_node_ptr node = (hash_node_ptr) O2_MALLOC(sizeof(hash_node));
    return o2_node_initialize(node, key);
}


// This is the main worker for dispatching messages. It determines if a node
// name is a pattern (if so, enumerate all nodes in the table and try to match)
// or not a pattern (if so, do a faster hash lookup). In either case, when the
// address node is internal (not the last part of the address), call this
// function recursively to search the tree of tables for matching handlers.
// Otherwise, call the handler specified by the/each matching entry.
// 
// remaining is what remains of path to be matched. The base case is the 2nd
//     character of the whole address (skipping ! or /).
// name is a buffer used to copy a node name and pad it with zeros
//     for the hash function
// node is the current node in the tree
// msg is message to be dispatched
//
static void find_and_call_handlers_rec(char *remaining, char *name,
        o2_node_ptr node, o2_msg_data_ptr msg, char *types)
{
    char *slash = strchr(remaining, '/');
    if (slash) *slash = 0;
    char *pattern = strpbrk(remaining, "*?[{");
    if (slash) *slash = '/';
    if (pattern) { // this is a pattern 
        enumerate enumerator;
        o2_enumerate_begin(&enumerator, &(((hash_node_ptr)node)->children));
        o2_node_ptr entry;
        while ((entry = o2_enumerate_next(&enumerator))) {
            if (slash && (entry->tag == NODE_HASH) &&
                (o2_pattern_match(entry->key, remaining) == O2_SUCCESS)) {
                find_and_call_handlers_rec(slash + 1, name, entry, msg, types);
            } else if (!slash && (entry->tag == NODE_HANDLER)) {
                char *path_end = remaining + strlen(remaining);
                path_end = WORD_ALIGN_PTR(path_end);
                call_handler((handler_entry_ptr) entry, msg, path_end + 5);
            }
        }
    } else { // no pattern characters so do hash lookup
        if (slash) *slash = 0;
        o2_string_pad(name, remaining);
        if (slash) *slash = '/';
        o2_node_ptr entry = *o2_lookup((hash_node_ptr) node, name);
        if (entry) {
            if (slash && (entry->tag == NODE_HASH)) {
                find_and_call_handlers_rec(slash + 1, name, entry,
                                           msg, types);
            } else if (!slash && (entry->tag == NODE_HANDLER)) {
                char *path_end = remaining + strlen(remaining);
                path_end = WORD_ALIGN_PTR(path_end);
                call_handler((handler_entry_ptr) entry, msg, path_end + 5);
            }
        }
    }
}


void osc_info_free(osc_info_ptr osc)
{
    if (osc->tcp_socket_info) { // TCP: close the TCP socket
        o2n_socket_mark_to_free(osc->tcp_socket_info);
        osc->tcp_socket_info->osc.service_name = NULL;
        osc->tcp_socket_info = NULL;   // just to be safe
        osc->service_name = NULL; // shared pointer with services_entry
    }
    O2_FREE(osc);
}


// entry_free - when an entry is inserted into a table, it
// may conflict with a previous entry. For example, if you
// define a handler for /a/b/1 and /a/b/2, then define a
// handler for /a/b, the table representing /a/b/ and
// containing entries for 1 and 2 will be replaced by the
// new handler for /a/b. This function recursively deletes
// and frees subtrees (such as the one rooted at /a/b).
// As a side-effect, the full paths (such as /a/b/1 and
// /a/b/2) corresponding to leaf nodes in the tree will be
// removed from the full_path_table, which hashes full paths.
// Note that the full_path_table entries do not have full_path
// fields (the .full_path field is NULL), so we know that
// an entry is in the o2_context->path_tree by looking at the
// .full_path field.
//
// The parameter should be an entry to remove -- either an
// internal entry (NODE_HASH) or a leaf entry (NODE_HANDLER)
// 
static void entry_free(o2_node_ptr entry)
{
    // printf("entry_free: freeing %s %s\n",
    //        o2_tag_to_string(entry->tag), entry->key);
    if (entry->tag == NODE_HASH) {
        o2_node_finish((hash_node_ptr) entry);
        O2_FREE(entry);
        return;
    } else if (entry->tag == NODE_HANDLER) {
        handler_entry_ptr handler = (handler_entry_ptr) entry;
        // if we remove a leaf node from the tree, remove the
        //  corresponding full path:
        if (handler->full_path) {
            remove_node(&o2_context->full_path_table, handler->full_path);
            handler->full_path = NULL; // this string should be freed
                // in the previous call to remove_node(); remove the
                // pointer so if anyone tries to reference it, it will
                // generate a more obvious and immediate runtime error.
        }
        if (handler->type_string)
            O2_FREE((void *) handler->type_string);
    } else if (entry->tag == NODE_SERVICES) {
        // free the service providers here
        services_entry_ptr ss = (services_entry_ptr) entry;
        for (int i = 0; i < ss->services.length; i++) {
            o2_node_ptr service = GET_SERVICE(ss->services, i);
            if (service->tag == NODE_HASH) {
                entry_free(service);
            } else if (service->tag == NODE_HANDLER) {
                entry_free(service);
            } else if (service->tag == NODE_OSC_REMOTE_SERVICE) {
                osc_info_free((osc_info_ptr) service);
            } else assert(service->tag == INFO_TCP_SOCKET ||
                          service->tag == INFO_TCP_NOCLOCK);
        }
        DA_FINISH(ss->services);
        // free the taps
        for (int i = 0; i < ss->taps.length; i++) {
            service_tap_ptr info = GET_TAP(ss->taps, i);
            O2_FREE((void *) info->tapper);
        }
        DA_FINISH(ss->taps);
    } else assert(FALSE); // nothing else should be freed
    O2_FREE((void *) entry->key);
    O2_FREE(entry);
}


// The hash function processes 4 bytes at a time and is based
// on the idea (and I think this is what Java uses) of repeatedly
// multiplying the hash by 5 and adding the next character until
// all characters are used. The SCRAMBLE number is (5 << 8) +
// ((5 * 5) << 16 + ..., so it is similar to doing the multiplies
// and adds all in parallel for 4 bytes at a time.
//
// In O2, o2string means "const char *" with zero padding to a
// 32-bit word boundary.
//
static int64_t get_hash(o2string key)
{
    int32_t *ikey = (int32_t *) key;
    uint64_t hash = 0;
    int32_t c;
    do {
        c = *ikey++;
        // c must be either all non-zero, or each zero must be followed by zero
        // and the last character is zero
        assert(((c & 0xff) && (c & 0xff00) &&
                (c & 0xff0000) && (c & 0xff000000)) ||
               ((((c & 0xff) != 0) || ((c & 0xff00) == 0)) &&
                (((c & 0xff00) != 0) || ((c & 0xff0000) == 0)) &&
                ((c & 0xff000000) == 0)));
        hash = ((hash + c) * SCRAMBLE) >> 32;
    } while (c & STRING_EOS_MASK);
    return hash;
}


static int initialize_table(dyn_array_ptr table, int locations)
{
    DA_INIT(*table, o2_node_ptr, locations);
    if (!table->array) return O2_FAIL;
    memset(table->array, 0, locations * sizeof(o2_node_ptr));
    table->allocated = locations;
    table->length = locations;
    return O2_SUCCESS;
}


// o2_node_add inserts an entry into the hash table. If the table becomes
// too full, a new larger table is created.
//
int o2_node_add(hash_node_ptr node, o2_node_ptr entry)
{
    o2_node_ptr *ptr = o2_lookup(node, entry->key);
    if (*ptr) { // if we found it, this is a replacement
        entry_remove(node, ptr, FALSE); // splice out existing entry and delete it
    }
    return o2_add_entry_at(node, ptr, entry);
}


// insert whole path into master table, insert path nodes into tree.
// If this path exists, then first remove all sub-tree paths.
//
// path is "owned" by caller (so it is copied here)
//
int o2_method_new(const char *path, const char *typespec,
                  o2_method_handler h, void *user_data, int coerce, int parse)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    // o2_heapify result is declared as const, but if we don't share it, there's
    // no reason we can't write into it, so this is a safe cast to (char *):
    char *key = (char *) o2_heapify(path);
    *key = '/'; // force key's first character to be '/', not '!'
    
    // add path elements as tree nodes -- to get the keys, replace each
    // "/" with EOS and o2_heapify to copy it, then restore the "/"
    char *remaining = key + 1;
    char name[NAME_BUF_LEN];
    char *slash = strchr(remaining, '/');
    if (slash) *slash = 0;
    services_entry_ptr services = *o2_services_find(remaining);
    // note that slash has not been restored (see o2_service_replace below)
    // services now is the existing services_entry node if it exists.
    // slash points to end of the service name in the path.

    int ret = O2_NO_SERVICE;
    if (!services) goto free_key_return; // cleanup and return because it is
        // an error to add a method to a non-existent service
    // find the service offered by this process (o2_context->info) --
    // the method should be attached to our local offering of the service
    o2_node_ptr node = o2_proc_service_find(o2_context->info, services);
    // we must have a local offering of the service
    if (!node || (node->tag != NODE_HASH && node->tag != NODE_HANDLER)) {
        goto free_key_return;
    }

    handler_entry_ptr handler = (handler_entry_ptr)
            O2_MALLOC(sizeof(handler_entry));
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
    if (!slash) { // (cases 1 and 2)
        handler->key = NULL;
        handler->full_path = NULL;
        ret = o2_service_provider_replace(key + 1, (o2_node_ptr) handler);
        goto free_key_return; // do not need full path for global handler
    }

    // cases 3 and 4: path has nodes. If service is a NODE_HANDLER, replace with NODE_HASH
    hash_node_ptr hash_node = (hash_node_ptr) node;
    if (hash_node->tag == NODE_HANDLER) { // change global handler to an empty hash_node
        hash_node = o2_hash_node_new(NULL); // top-level key is NULL
        if (!hash_node) goto error_return_3;
        if ((ret = o2_service_provider_replace(key + 1, (o2_node_ptr) hash_node))) {
            goto error_return_3;
        }
    }
    // now hash_node is the root of a path tree for all paths for this service
    assert(slash);
    *slash = '/'; // restore the full path in key
    remaining = slash + 1;

    while ((slash = strchr(remaining, '/'))) {
        *slash = 0; // terminate the string at the "/"
        o2_string_pad(name, remaining);
        *slash = '/'; // restore the string
        remaining = slash + 1;
        // if necessary, allocate a new entry for name
        hash_node = tree_insert_node(hash_node, name);
        assert(hash_node);
        // node is now the node for the path up to name
    }
    // node is now where we should put the final path name with the handler;
    // remaining points to the final segment of the path
    handler->key = o2_heapify(remaining);
    if ((ret = o2_node_add(hash_node, (o2_node_ptr) handler))) {
        goto error_return_3;
    }
    
    // make an entry for the master table
    handler_entry_ptr mhandler = (handler_entry_ptr) O2_MALLOC(sizeof(handler_entry));
    memcpy(mhandler, handler, sizeof(handler_entry)); // copy the handler info
    mhandler->key = key; // this key has already been copied
    mhandler->full_path = NULL; // only leaf nodes have full_path pointer
    if (types_copy) types_copy = o2_heapify(typespec);
    mhandler->type_string = types_copy;
    // put the entry in the master table
    ret = o2_node_add(&o2_context->full_path_table, (o2_node_ptr) mhandler);
    goto just_return;
  error_return_3:
    if (types_copy) O2_FREE((void *) types_copy);
  error_return_2:
    O2_FREE(handler);
  free_key_return: // not necessarily an error (case 1 & 2)
    O2_FREE(key);
  just_return:
    return ret;
}


static const char *info_to_ipport(o2_node_ptr info)
{
    return info->tag == INFO_TCP_SERVER ?
            o2_context->info->proc.name : ((o2n_info_ptr) info)->proc.name;
}


// in the list of services, find the service with the highest service provider
// name and move it to the top position in the list. This is called when the
// top (active) service is removed and must be replaced
static void pick_service_provider(dyn_array_ptr list)
{
    int top_index = 0;
    int search_start = 1;
    if (top_index >= list->length) return;
    o2_node_ptr top_info = GET_SERVICE(*list, top_index);
    const char *top_name = info_to_ipport(top_info);
    for (int i = search_start; i < list->length; i++) {
        o2_node_ptr info = GET_SERVICE(*list, i);
        const char *name = info_to_ipport(info);
        // if location 0 was not a tap, we did not update search_start,
        // so we have to skip over taps to find real services.
        if (strcmp(name, top_name) > 0) {
            // we found a service and it has a greater name, so remember
            // the top name so far and where we found it.
            top_info = info;
            top_name = name;
            top_index = i;
        }
    }
    // swap top_index and 0. It is possible there's only one service at
    // location 0 and we swap it with itself - a no-op. Or maybe service
    // 0 is the top ipport, so again swapping with itself is OK.
    DA_SET(*list, o2_node_ptr, top_index, GET_SERVICE(*list, 0));
    DA_SET(*list, o2_node_ptr, 0, top_info);
}


/** replace the service named service_name offered by proc with new_service.
 * This happens when we change from all-service handler to per-node handlers
 * or vice versa. Also happens when we delete a service, and when we remove a
 * remote service when it disconnects.
 *
 * precondition: service_name does not have '/' && new_service != NULL
 *
 * CASE 1: called from o2_method_new(), installing a global handler for
 *         service, maybe replacing an existing one, maybe not.
 * CASE 2: called from o2_method_new(), replacing a global handler with
 *         a hash_node where we can install a NODE_HASH based on the
 *         next node in the address.
 * (no more cases because we moved others to o2_service_remove)
 */
int o2_service_provider_replace(const char *service_name,
                                o2_node_ptr new_service)
{
    int rslt = O2_NO_SERVICE;
    assert(new_service);
    services_entry_ptr *services = o2_services_find(service_name);
    if (!*services || (*services)->tag != NODE_SERVICES) {
        O2_DBg(printf("%s o2_service_provider_replace(%s,...) did not find "
                      "service\n", o2_debug_prefix, service_name));
        return rslt;
    }
    dyn_array_ptr svlist = &((*services)->services); // list of services
    // search for the entry in the list of services that corresponds to proc
    int i;
    for (i = 0; i < svlist->length; i++) {
        o2_node_ptr service = GET_SERVICE(*svlist, i);
        switch (service->tag) {
            case NODE_HASH:
            case NODE_HANDLER:
            case NODE_BRIDGE_SERVICE:
                entry_free(service);
                goto done; // break from switch AND for loop
            case NODE_OSC_REMOTE_SERVICE:
                osc_info_free((osc_info_ptr) service);
                rslt = O2_SERVICE_CONFLICT;
                i = svlist->length; // force error return
                goto done; // break from switch AND for loop
            case INFO_TCP_NOCLOCK:
            case INFO_TCP_SOCKET:
                break;
            case NODE_SERVICES: // can't have a services array as a service
            case INFO_TCP_SERVER: // can't have local server port as a service
            case INFO_UDP_SOCKET: // can't have udp recv socket as a service
            case INFO_OSC_UDP_SERVER: // can't have incoming OSC as a service
            case INFO_OSC_TCP_SERVER: // can't have OSC server as a service
            case INFO_OSC_TCP_CONNECTION: // can't have connected OSC client as service
            case INFO_OSC_TCP_CONNECTING: // can't have connection to OSC server as service
            case INFO_OSC_TCP_CLIENT: // can't have connection to OSC server as service
            default:
                assert(FALSE); // debug mode will halt, release will ignore
                break;
        }
    }
done:
    // if we did not find what we wanted to replace, stop here
    if (i >= svlist->length) {
        O2_DBg(printf("%s o2_service_provider_replace(%s, ...) did not find "
                      "service offered by this process\n",
                      o2_debug_prefix, service_name));
        return rslt;
    }
    // ASSERT: i is now the index of the service we are replacing
    DA_SET(*svlist, o2_node_ptr, i, new_service);
    return O2_SUCCESS;
}


// This code is used by o2_service_remove and o2_tap_remove_from:
// it checks if a services_entry no longer has services or taps.
// If not, it removes the services entry completely.
//
static void remove_empty_services_entry(services_entry_ptr ss,
                                        o2n_info_ptr proc)
{
    // if no service providers or taps left, remove service entry
    if (ss->services.length == 0 && ss->taps.length == 0) {
        printf("Removing %s from &o2_context->path_tree\n", ss->key);
        remove_node(&o2_context->path_tree, ss->key);
        printf(" Here is the result:\n");
        o2_info_show(&o2_context->path_tree, 2);
        // service name (the key in path_tree) is now freed.
    }
}


/*
 * Remove a service offering from proc. If this is the last use of the
 * service, remove the service entirely. If the service has already
 * been looked up, you can pass in the services_entry and index of the
 * service matching proc. Otherwise, pass NULL for ss. If the index
 * is unknown, pass -1 to do a search.
 *
 * CASE 1: OSC_TCP_CLIENT gets hangup. service_name is 
 *         from process_info with tag==OSC_TCP_CLIENT, 
 *         proc is o2_context->info
 *     finds and frees osc_info pointed to by services_entry
 *     removes the osc_info pointer from services_entry
 *     if services_entry is empty, remove service from path_tree
 *     if proc is o2_context->info, notify that service is gone from proc
 *  
 * CASE 2: /ip:port/sv gets a service removed message. proc is remote
 *
 * CASE 3: INFO_TCP_SOCKET gets hangup. remove_remote_services makes a list
 *     of services to remove, then calls this to do the work.
 *
 * CASE 4: service is removed from local process by o2_service_free()
 */
int o2_service_remove(const char *service_name, o2n_info_ptr proc,
                      services_entry_ptr ss, int index)
{
    if (!ss) {
        ss = *o2_services_find(service_name);
        index = -1; // indicates we should search ss
    }
    if (!ss || ss->tag != NODE_SERVICES) {
        O2_DBg(printf("%s o2_service_remove(%s, %s) did not find "
                      "service\n",
                      o2_debug_prefix, service_name, proc->proc.name));
        return O2_FAIL;
    }
    dyn_array_ptr svlist = &(ss->services); // list of services
    
    // search for the entry in the list of services that corresponds to proc
    if (index < 0) {
        for (index = 0; index < svlist->length; index++) {
            o2_node_ptr s = GET_SERVICE(*svlist, index);
            int tag = s->tag;
            if ((tag == INFO_TCP_SOCKET || tag == INFO_TCP_NOCLOCK) &&
                (o2n_info_ptr) s == proc) {
                break;
            } else if ((tag == NODE_HASH || tag == NODE_HANDLER) &&
                       proc == o2_context->info) {
                entry_free(s);
                break;
            } else if (tag == NODE_OSC_REMOTE_SERVICE &&
                       proc == o2_context->info) {
                // shut down any OSC connection
                osc_info_free((osc_info_ptr) s);
                break;
            } else {
                assert(tag != NODE_BRIDGE_SERVICE);
            }
        }
    }
    // if we did not find what we wanted to replace, stop here
    if (index >= svlist->length) {
        O2_DBg(printf("%s o2_service_remove(%s, %s, ...) did not find "
                      "service offered by this process\n",
                      o2_debug_prefix, service_name, proc->proc.name));
        return O2_FAIL;
    }
    // ASSERT: index is now the index of the service we are deleting or
    //         replacing
    //
    // we found the service to replace; finalized the info depending on the
    // type, so now we have a dangling pointer in the services list
    DA_REMOVE(*svlist, o2n_info_ptr, index);

    o2_do_not_reenter++; // protect data structures
    // send notification message
    assert(proc->proc.name[0]);
    o2_send_cmd("!_o2/si", 0.0, "sis", service_name, O2_FAIL, proc->proc.name);

    // if we deleted active service, pick a new one
    if (index == 0) { // move top ip:port provider to top spot
        pick_service_provider(svlist);
    }
    // now we probably have a new service, report it:
    if (svlist->length > 0) {
        o2_node_ptr info = GET_SERVICE(*svlist, 0);
        const char *process_name;
        int status = o2_status_from_info(info, &process_name);
        if (status != O2_FAIL) {
            assert(process_name[0]);
            o2_send_cmd("!_o2/si", 0.0, "sis", service_name, status,
                        process_name);
        }
    }

    remove_empty_services_entry(ss, proc);

    // if the service was local, tell other processes that it is gone
    if (proc == o2_context->info) {
        o2_notify_others(service_name, FALSE, NULL, NULL);
    }
    // finally, remove service from proc_service_data list, find it first
    for (int i = 0; i < proc->proc.services.length; i++) {
        proc_service_data_ptr psdp = DA_GET(proc->proc.services,
                                            proc_service_data, i);
        if (psdp->services == ss) {
            if (psdp->properties) {
                O2_FREE(psdp->properties);
            }
            DA_REMOVE(proc->proc.services, proc_service_data, i);
            break;
        }
    }
    o2_do_not_reenter--;
    return O2_SUCCESS;
}


int o2_tap_remove_from(services_entry_ptr ss, o2n_info_ptr process,
                       o2string tapper)
{
    // services exists, find the tap
    int i;
    for (i = 0; i < ss->taps.length; i++) {
        service_tap_ptr tap = GET_TAP(ss->taps, i);
        if (streql(tap->tapper, tapper) && tap->proc == process) {
            O2_FREE(tap->tapper);
            DA_REMOVE(ss->taps, service_tap, i);
            break;
        }
    }

    if (i >= ss->taps.length) {
        return O2_FAIL; // failed to find tap
    }

    // remove tap from the processs's list of taps:
    for (int i = 0; i < process->proc.taps.length; i++) {
        proc_tap_data_ptr ptdp = DA_GET(process->proc.taps, proc_tap_data, i);
        if (ptdp->services == ss) {
            DA_REMOVE(process->proc.taps, proc_tap_data, i);
            return O2_SUCCESS;
        }
    }

    remove_empty_services_entry(ss, process);

    // failed to find the tap in the process's taps list:
    return O2_FAIL;
}


// find existing services_entry or create an empty services_entry
// for service_name
//
services_entry_ptr o2_must_get_services(o2string service_name)
{
    services_entry_ptr *services = (services_entry_ptr *)
            o2_lookup(&o2_context->path_tree, service_name);
    if (*services) return *services;
    services_entry_ptr s = O2_CALLOC(1, sizeof(services_entry));
    s->tag = NODE_SERVICES;
    s->key = o2_heapify(service_name);
    s->next = NULL;
    DA_INIT(s->services, o2n_info_ptr, 1);
    // No need to initialize s->taps because it is empty.
    o2_add_entry_at(&o2_context->path_tree, (o2_node_ptr *) services, 
                    (o2_node_ptr) s);
    return s;
}


// remove a service from o2_context->path_tree
//
int o2_service_free(const char *service_name)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    if (!service_name || strchr(service_name, '/'))
        return O2_BAD_SERVICE_NAME;
    return o2_service_remove(service_name, o2_context->info, NULL, -1);
}


int o2_embedded_msgs_deliver(o2_msg_data_ptr msg, int tcp_flag)
{
    char *end_of_msg = PTR(msg) + MSG_DATA_LENGTH(msg);
    o2_msg_data_ptr embedded = (o2_msg_data_ptr)
            (msg->address + o2_strsize(msg->address) + sizeof(int32_t));
    while (PTR(embedded) < end_of_msg) {
        // need to copy each embedded message before sending
        int len = MSG_DATA_LENGTH(embedded);
        o2_message_ptr message = o2_alloc_size_message(len);
        memcpy((char *) &(message->data), (char *) embedded, len);
        message->next = NULL;
        message->tcp_flag = tcp_flag;
        message->length = len;
        o2_message_send_sched(message, TRUE);
        embedded = (o2_msg_data_ptr)
                (PTR(embedded) + len + sizeof(int32_t));
    }
    return O2_SUCCESS;
}


void send_msg_data_to_tapper(o2_msg_data_ptr msg, o2string tapper)
{
    // construct a new message to send to tapper by replacing service name
    // how big is the existing service name?
    // I think coerce to char * will remove bounds checking, which might
    // limit the search to 4 characters since msg->address is declared to
    // be char [4]
    // Skip first character which might be a slash; we want the slash after the
    // service name.
    char *slash = strchr((char *) (msg->address) + 1, '/');
    if (!slash) {
        return;  // this is not a valid address, stop now
    }
    int curlen = (int) (slash - msg->address);

    // how much space will tapper take?
    int newlen = (int) strlen(tapper) + 1; // add 1 for initial '/' or '!'

    // how long is current address?
    int curaddrlen = (int) strlen((char *) (msg->address));

    // how long is new address?
    int newaddrlen = curaddrlen + (newlen - curlen);

    // what is the difference in space needed for address (and message)?
    // "+ 4" accounts for end-of-string byte and padding in each case
    int curaddrall = WORD_OFFSET(curaddrlen + 4); // address + padding
    int newaddrall = WORD_OFFSET(newaddrlen + 4);
    int extra = newaddrall - curaddrall;

    // allocate a new message
    o2_message_ptr newmsg = o2_alloc_size_message(MSG_DATA_LENGTH(msg) + extra);
    newmsg->length = MSG_DATA_LENGTH(msg) + extra;
    newmsg->data.timestamp = msg->timestamp;
    // fill end of address with zeros before creating address string
    *((int32_t *) (newmsg->data.address + WORD_OFFSET(newaddrlen))) = 0;
    // first character is either / or ! copied from original message
    newmsg->data.address[0] = msg->address[0];
    // copies name and EOS:
    memcpy((char *) (newmsg->data.address + 1), tapper, newlen);
    memcpy((char *) (newmsg->data.address + newlen), msg->address + curlen,
           curaddrlen - curlen);
    // copy the rest of the message
    memcpy((char *) (newmsg->data.address + newaddrall),
           msg->address + curaddrall, MSG_DATA_LENGTH(msg) - curaddrall);
    o2_message_send_sched(newmsg, FALSE);
}


// deliver msg locally and immediately. If service is not null,
//    assume it is correct, saving the cost of looking it up
void o2_msg_data_deliver(o2_msg_data_ptr msg, int tcp_flag,
                         o2_node_ptr service, services_entry_ptr ss)
{
    if (IS_BUNDLE(msg)) {
        o2_embedded_msgs_deliver(msg, tcp_flag);
        return;
    }

    char *address = msg->address;
    if (!service) {
        service = o2_msg_service(msg, &ss);
        if (!service) return; // service must have been removed
    }

    // we are going to deliver a non-bundle message, so we'll need
    // to find the type string...
    char *types = address;
    while (types[3]) types += 4; // find end of address string
    // types[3] is the zero marking the end of the address,
    // types[4] is the "," that starts the type string, so
    // types + 5 is the first type char
    types += 5; // now types points to first type char

    // if you o2_add_message("/service", ...) then the service entry is a
    // pattern handler used for ALL messages to the service
    if (service->tag == NODE_HANDLER) {
        call_handler((handler_entry_ptr) service, msg, types);
    } else if ((address[0]) == '!') { // do full path lookup
        address[0] = '/'; // must start with '/' to get consistent hash value
        o2_node_ptr handler = *o2_lookup(&o2_context->full_path_table, address);
        address[0] = '!'; // restore address for no particular reason
        if (handler && handler->tag == NODE_HANDLER) {
            call_handler((handler_entry_ptr) handler, msg, types);
        }
    } else if (service->tag == NODE_HASH) {
        char name[NAME_BUF_LEN];
        address = strchr(address + 1, '/'); // search for end of service name
        if (!address) {
            // address is "/service", but "/service" is not a NODE_HANDLER
            ;
        } else {
            find_and_call_handlers_rec(address + 1, name, (o2_node_ptr) service, msg, types);
        }
    } // else the assumption that the service is local fails, drop the message

    // if there are tappers, send the message to them as well
    for (int i = 0; i < ss->taps.length; i++) {
        send_msg_data_to_tapper(msg, GET_TAP(ss->taps, i)->tapper);
    }
}


void o2_node_finish(hash_node_ptr node)
{
    for (int i = 0; i < node->children.length; i++) {
        o2_node_ptr e = *DA_GET(node->children, o2_node_ptr, i);
        while (e) {
            o2_node_ptr next = e->next;
            entry_free(e);
            e = next;
        }
    }
    // not all nodes have keys, top-level nodes have key == NULL
    if (node->key) O2_FREE((void *) node->key);
}


// copy a string to the heap, result is 32-bit word aligned, has
//   at least one zero end-of-string byte and is
//   zero-padded to the next word boundary
o2string o2_heapify(const char *path)
{
    long len = o2_strsize(path);
    char *rslt = (char *) O2_MALLOC(len);
    // zero fill last 4 bytes
    int32_t *end_ptr = (int32_t *) WORD_ALIGN_PTR(rslt + len - 1);
    *end_ptr = 0;
    strcpy(rslt, path);
    assert(*path == 0 || *rslt);
    return rslt;
}


// set fields for a node in the path tree
//
// key is "owned" by the caller
//
hash_node_ptr o2_node_initialize(hash_node_ptr node, const char *key)
{
    node->tag = NODE_HASH;
    node->key = key;
    if (key) { // note: top level and services don't have keys
        node->key = o2_heapify(key);
        if (!node->key) {
            O2_FREE(node);
            return NULL;
        }
    }
    node->num_children = 0;
    initialize_table(&(node->children), 2);
    return node;
}


// o2_lookup returns a pointer to a pointer to the entry, if any.
// The hash table uses linked lists for collisions to make
// deletion simple. key must be aligned on a 32-bit word boundary
// and must be padded with zeros to a 32-bit boundary
o2_node_ptr *o2_lookup(hash_node_ptr node, o2string key)
{
    int n = node->children.length;
    int64_t hash = get_hash(key);
    int index = hash % n;
    // printf("o2_lookup %s in %s hash %ld index %d\n", key, node->key, hash, *index);
    o2_node_ptr *ptr = DA_GET(node->children, o2_node_ptr, index);
    while (*ptr) {
        if (streql(key, (*ptr)->key)) {
            break;
        }
        ptr = &((*ptr)->next);
    }
    return ptr;
}


#ifndef NEGATE
#define NEGATE  '!'
#endif

/**
 * robust glob pattern matcher
 *
 *  @param str oringinal string, a node name terminated by zero (eos)
 *  @param p   the string with pattern, p can be the remainder of a whole
 *             address pattern, so it is terminated by either zero (eos)
 *             or slash (/)
 *
 *  @return Iff match, return TRUE.
 *
 * glob patterns:
 *  *   matches zero or more characters
 *  ?   matches any single character
 *  [set]   matches any character in the set
 *  [^set]  matches any character NOT in the set
 *      where a set is a group of characters or ranges. a range
 *      is written as two characters seperated with a hyphen: a-z denotes
 *      all characters between a to z inclusive.
 *  [-set]  set matches a literal hypen and any character in the set
 *  []set]  matches a literal close bracket and any character in the set
 *
 *  char    matches itself except where char is '*' or '?' or '['
 *  \char   matches char, including any pattern character
 *
 * examples:
 *  a*c     ac abc abbc ...
 *  a?c     acc abc aXc ...
 *  a[a-z]c     aac abc acc ...
 *  a[-a-z]c    a-c aac abc ...
 */
static int o2_pattern_match(const char *str, const char *p)
{
    int negate; // the ! is used before a character or range of characters
    int match;  // a boolean used to exit a loop looking for a match
    char c;     // a character from the pattern
    
    // match each character of the pattern p with string str up to
    //   pattern end marked by zero (end of string) or '/'
    while (*p && *p != '/') {
        // fast exit: if we have exhausted str and there is more
        //   pattern to match, give up (unless *p is '*', which can
        //   match zero characters)
        // also, [!...] processing assumes a character to match in str
        //   without checking, so the case of !*str is handled here
        if (!*str && *p != '*') {
            return FALSE;
        }
        
        // process the next character(s) of the pattern
        switch ((c = *p++)) {
            case '*': // matches 0 or more characters
                while (*p == '*') p++; // "*...*" is equivalent to "*" so
                                       // skip over a run of '*'s
                
                // if there are no more pattern characters, we can match
                //   '*' to the rest of str, so we have a match. This is an
                //   optimization that tests for a special case:
                if (!*p || *p == '/') return TRUE;
                
                // if the next pattern character is not a meta character,
                //   we can skip over all the characters in str that
                //   do not match: at least these skipped characters must
                //   match the '*'. This is an optimization:
                if (*p != '?' && *p != '[' && *p != '{')
                    while (*str && *p != *str) str++;
                
                // we do not know if '*' should match more characters or
                //   not, so we have to try every possibility. This is
                //   done recursively. There are more special cases and
                //   possible optimizations, but at this point we give up
                //   looking for special cases and just try everything:
                while (*str) {
                    if (o2_pattern_match(str, p)) {
                        return TRUE;
                    }
                    str++;
                }
                return FALSE;
                
            case '?': // matches exactly 1 character in str
                if (*str) break; // success
                return FALSE;
            /*
             * set specification is inclusive, that is [a-z] is a, z and
             * everything in between. this means [z-a] may be interpreted
             * as a set that contains z, a and nothing in between.
             */
            case '[':
                if (*p != NEGATE) {
                    negate = 1;
                } else {
                    negate = 0; // note that negate == 0 if '!' found
                    p++;        //   so in this case, 0 means "true"
                }
                
                match = 0; // no match found yet
                // search in set for a match until it is found
                // if/when you exit the loop, p is pointing to ']' or
                //   before it, or c == ']' and p points to the
                //   next character, or there is no matching ']'
                while (!match && (c = *p++)) {
                    if (!*p || *p == '/') { // no matching ']' in pattern
                        return FALSE;
                    } else if (c == ']') {
                        p--; // because we search forward for ']' below
                        break;
                    } else if (*p == '-') {  // expected syntax is c-c
                        p++;
                        if (!*p || *p == '/')
                            return FALSE; // expected to find at least ']'
                        if (*p != ']') {  // found end of range
                            match = (*str == c || *str == *p ||
                                     (*str > c && *str < *p));
                        } else {  //  c-] means ok to match c or '-'
                            match = (*str == c || *str == '-');
                        }
                    } else {   // no dash, so see if we match 'c'
                        match = (c == *str);
                    }
                }
                
                if (negate == match) {
                    return FALSE;
                }
                // if there is a match, skip past the cset and continue on
                while ((c = *p++) != ']') {
                    if (!c || c == '/') { // no matching ']' in pattern
                        return FALSE;
                    }
                }
                break;
                
            /*
             * {astring,bstring,cstring}: This is tricky because astring
             *   could be a prefix of bstring, so even if astring matches
             *   the beginning of str, we may have to backtrack and match
             *   bstring in order to get an overall match
             */
            case '{': {
                // *p is now first character in the {brace list}
                const char *place = str;        // to backtrack
                const char *remainder = p;      // to forwardtrack
                
                // find the end of the brace list (or end of pattern)
                c = *remainder;
                while (c != '}') {
                    if (!c || c == '/') {  // unexpected end of pattern
                        return FALSE;
                    }
                    c = *remainder++;
                }
                c = *p++;
                
                // test each string in the {brace list}. At the top of
                //   the loop:
                //     c is a character of a {brace list} string
                //     p points to the next character after c
                //     str points to the so-far unmatched remainder of
                //         the address
                //     place points to the location in str that must
                //         be matched with this {brace list}
                while (c && c != '/') {
                    if (c == ',') {
                        // recursively see if we can complete the match
                        if (o2_pattern_match(str, remainder)) {
                            return TRUE;
                        } else {
                            str = place; // backtrack on test string
                            // continue testing, skipping the comma
                            p++;
                            if (!*p || *p == '/') { // unexpected end
                                return FALSE;
                            }
                        }
                    } else if (c == '}') {
                        str--;  // str is incremented again below
                        break;
                    } else if (c == *str) { // match a character
                        str++;
                    } else {    // skip to next comma
                        str = place;
                        while ((c = *p++) != ',') {
                            if (!c || c == '/' || c == '}') {
                                return FALSE; // no more choices, so no match
                            }
                        }
                    }
                    c = *p++;
                }
                break;
            }
                
            default:
                if (c != *str) {
                    return FALSE;
                }
                break;
        }
        str++;
    }
    // since we have reached the end of the pattern, we match iff we are
    //   also at the end of the string:
    return (*str == 0);
}


/**
 * \brief remove a path -- find the leaf node in the tree and remove it.
 *
 * When the method is no longer exist, or the method conflict with a new one.
 * we need to call this function to delete the old one. The master table entry
 * will be removed as a side effect. If a parent node becomes empty, the 
 * parent is removed. Thus we use a recursive algorithm so we can examine 
 * parents after visiting the children.
 *
 *  @param path The path of the method
 *
 *  @return If succeed, return O2_SUCCESS. If not, return O2_FAIL.
 */
int o2_remove_method(const char *path)
{
    // this is one of the few times where we need the result of o2_heapify
    // to be writeable, so coerce from o2string to (char *)
    char *path_copy = (char *) o2_heapify(path);
    if (!path_copy) return O2_FAIL;
    char name[NAME_BUF_LEN];
    
    // search path elements as tree nodes -- to get the keys, replace each
    // "/" with EOS and o2_heapify to copy it, then restore the "/"
    char *remaining = path_copy + 1; // skip the initial "/"
    return remove_method_from_tree(remaining, name, &o2_context->path_tree);
}

// ORGANIZATION FOR OSC_TCP_CLIENT (service delegated to OSC over TCP)
/***************************************
o2_context->fds                       service
         o2_context->fds_info         names
+----+   +---------+   +---------+    ______
|    |   |        -+-> | local   +-->|______| (these pointers are equal
|----|   |---------|   | process |   |______|  to keys in o2_context->path_tree)
|    |   |         |   +---------+
|----|   |---------|        ^     +---------+
|    |   |        -|--------+---->| process +--> osc.service_name (not shared)
+----+   +---------+        |     |   info  | (this process info
                            |     +---------+  has no service names)
o2_context->path_tree |                    ^
+----------+    +--------+  |              |
|         -+--->|services+--+             ++---------+
+----------+    | entry  +--------------->| osc_info |
|         -+->  |        |   +-------- +  |          |
+----------+    |       -+-->| (local) |  +----------+
                +--------+   | node   -+--->etc
                             | entry   | -> +---------+
                             +--------+     | handler |
							                | entry   |
                                            +---------+

local process should have osc.service_name in it's list of service names
osc_info has tag = NODE_OSC_REMOTE_SERVICE
process info has tag = OSC_TCP_CLIENT (it's not a "real" process, which 
   would be an O2 process to which we have a TCP connection. This OSC_TCP_CLIENT
   process is just a place-holder for the TCP connection to an OSC server.
*****************************************/


// Called when socket gets a TCP_HUP (hang-up) error;
// Delete the socket and data associated with it:
//    for INFO_TCP_SOCKET or INFO_TCP_NOCLOCK
//        remove all services for this process, these all point to a single
//            o2n_info_ptr
//        if the services_entry becomes empty (and it will for the ip:port
//            service), remove the services_entry
//        delete this o2n_info_ptr contents, including:
//            proc.name
//            array of service names (names themselves are keys to 
//                services_entry, so they are not freed (or maybe they are
//                already freed by the time we free this array)
//            any message
//        mark the socket to be freed, and in a deferred action, the 
//            socket is closed and removed from the o2_context->fds array. The 
//            corresponding o2_context->fds_info entry is freed then too
//    for INFO_UDP_SOCKET, INFO_OSC_DISCOVER_SOCKET, INFO_OSC_TCP_SOCKET,
//        TINFO_CP_SERVER_SOCKET, INFO_TCP_SERVER_SOCKET, INFO_OSC_TCP_SOCKET (name is
//        "owned" by OSC_TCP_SERVER_SOCKET):
//            free any message
//            mark the socket to be freed later
//    for INFO_OSC_SOCKET, INFO_OSC_TCP_SERVER:
//        (it must be the case that we're shutting down: when we free the
//         osc.service_name, all the INFO_OSC_TCP_SOCKETS accepted from this
//         socket will have freed osc.service_name fields)
//        free the osc.service_name (it's a copy)
//        free any message
//        mark the socket to be freed later
//    for INFO_OSC_TCP_CLIENT
//        the service name is osc.service_name
//        remember info's osc.service_name
//       done by o2_service_remove:
//           find the services_entry array
//           find the entry with tag NODE_OSC_REMOTE_SERVICE
//           the entry's tcp_socket_info should point to info
//           free the entry
//           remove service_name from local process's services list
//           free info (o2n_info_ptr)
//           mark socket to be freed later
//        free remembered info's osc.service_name
//
int o2_info_remove(o2n_info_ptr info)
{
    printf("o2_info_remove info tag %d name %s\n", info->tag, info->proc.name);
    if (info->tag == INFO_TCP_SOCKET || info->tag == INFO_TCP_NOCLOCK) {
        // remove the remote services provided by the proc
        remove_remote_services(info);
        remove_taps_by(info);
        // proc.name may be NULL if we have not received an init (/_o2/dy)
        // message
        if (info->proc.name) {
            O2_DBd(printf("%s removing remote process %s\n",
                          o2_debug_prefix, info->proc.name));
            O2_FREE((void *) info->proc.name);
            info->proc.name = NULL;
        }
    } else if (info->tag == INFO_OSC_UDP_SERVER ||
               info->tag == INFO_OSC_TCP_SERVER ||
               info->tag == INFO_OSC_TCP_CONNECTING ||
               info->tag == INFO_OSC_TCP_CONNECTION ||
               info->tag == INFO_OSC_TCP_CLIENT) {
        // TODO: Does EVERY one of these cases have an osc.service.name?
        O2_FREE(info->osc.service_name);
    }
    if (info->in_message) O2_FREE(info->in_message);
    while (info->out_message) {
        o2_message_ptr p = info->out_message;
        info->out_message = p->next;
        O2_FREE(p);
    }
    o2n_socket_mark_to_free(info); // close the TCP socket
    return O2_SUCCESS;
}


// tree_insert_node -- insert a node for pattern matching.
// on entry, table points to a tree node pointer, initially it is the
// address of o2_context->path_tree. If key is already in the table and the
// entry is another node, then just return a pointer to the node address.
// Otherwise, if key is a handler, remove it, and then create a new node
// to represent this key.
//
// key is "owned" by caller and must be aligned to 4-byte word boundary
//
static hash_node_ptr tree_insert_node(hash_node_ptr node, o2string key)
{
    assert(node->children.length > 0);
    o2_node_ptr *entry_ptr = o2_lookup(node, key);
    // 3 outcomes: entry exists and is a NODE_HASH: return location
    //    entry exists but is something else: delete old and create one
    //    entry does not exist: create one
    if (*entry_ptr) {
        if ((*entry_ptr)->tag == NODE_HASH) {
            return (hash_node_ptr) *entry_ptr;
        } else {
            // this node cannot be a handler (leaf) and a (non-leaf) node
            entry_remove(node, entry_ptr, FALSE);
        }
    }
    // entry is a valid location. Insert a new node:
    hash_node_ptr new_entry = o2_hash_node_new(key);
    o2_add_entry_at(node, entry_ptr, (o2_node_ptr) new_entry);
    return new_entry;
}


// remove a child from a node. Then free the child
// (deleting its entire subtree, or if it is a leaf, removing the
// entry from the o2_context->full_path_table).
// ptr is the address of the pointer to the table entry to be removed.
// This ptr must be a value returned by o2_lookup or o2_service_find
// Often, we remove an entry to make room for an insertion, so
// we do not want to resize the table. The resize parameter must
// be true to enable resizing.
//
static int entry_remove(hash_node_ptr node, o2_node_ptr *child, int resize)
{
    node->num_children--;
    o2_node_ptr entry = *child;
    *child = entry->next;
    entry_free(entry);
    // if the table is too big, rehash to smaller table
    if (resize && (node->num_children * 3 < node->children.length) &&
        (node->num_children > 3)) {
        // suppose we jumped to 12 locations when we got up to 8 entries.
        // when we go back down to 4 entries, we still allow 12, but when
        // we are one less, 3 entries, we want to cut the size in half.
        // Therefore the math is (3 + 1) * 3 / 2 = 6, which is half of 12.
        // We do not make table size less than 3.
        return resize_table(node, ((node->num_children + 1) * 3) / 2);
    }
    return O2_SUCCESS;
}


// recursive function to remove path from tree. Follow links to the leaf
// node, remove it, then as the stack unwinds, remove empty nodes.
// remaining is the full path, which is manipulated to isolate node names.
// name is storage to copy and pad node names.
// table is the current node.
//
// returns O2_FAIL if path is not found in tree (should not happen)
//
static int remove_method_from_tree(char *remaining, char *name,
                                   hash_node_ptr node)
{
    char *slash = strchr(remaining, '/');
    o2_node_ptr *entry_ptr; // another return value from o2_lookup
    if (slash) { // we have an internal node name
        *slash = 0; // terminate the string at the "/"
        o2_string_pad(name, remaining);
        *slash = '/'; // restore the string
        entry_ptr = o2_lookup(node, name);
        if ((!*entry_ptr) || ((*entry_ptr)->tag != NODE_HASH)) {
            printf("could not find method\n");
            return O2_FAIL;
        }
        // *entry addresses a node entry
        node = (hash_node_ptr) *entry_ptr;
        remove_method_from_tree(slash + 1, name, node);
        if (node->num_children == 0) {
            // remove the empty table
            return entry_remove(node, entry_ptr, TRUE);
        }
        return O2_SUCCESS;
    }
    // now table is where we find the final path name with the handler
    // remaining points to the final segment of the path
    o2_string_pad(name, remaining);
    entry_ptr = o2_lookup(node, name);
    // there should be an entry, remove it
    if (*entry_ptr) {
        entry_remove(node, entry_ptr, TRUE);
        return O2_SUCCESS;
    }
    return O2_FAIL;
}


// remove a dictionary entry by name
// when we remove an entry, we may resize the table to be smaller
// in which case node->children.table is written with a pointer to
// the new table and the old table is freed
//
static int remove_node(hash_node_ptr node, o2string key)
{
    o2_node_ptr *ptr = o2_lookup(node, key);
    if (*ptr) {
        return entry_remove(node, ptr, TRUE);
    }
    return O2_FAIL;
}


/*
// return value for o2_find_proc_services -- reused to save reallocating
dyn_array o2_services_rslt = {0, 0, NULL};
dyn_array o2_taps_rslt = {0, 0, NULL};

// find list of services provided by info, a remote process
//
void o2_find_proc_services(o2n_info_ptr info, int taps_flag)
{
    enumerate enumerator;
    o2_enumerate_begin(&enumerator, &o2_context->path_tree.children);
    o2_services_rslt.length = 0; // empty the dynamic array of results
    o2_taps_rslt.length = 0;     // empty the dynamic array of taps results
    services_entry_ptr ss;
    while ((ss = (services_entry_ptr) o2_enumerate_next(&enumerator))) {
        assert(ss->tag == NODE_SERVICES);
        for (int i = 0; i < ss->services.length; i++) {
            o2n_info_ptr s = GET_SERVICE(ss->services, i);
            int tag = s->tag;
            if ((tag == INFO_TCP_SOCKET && ((o2n_info_ptr) s) == info)) {// ||
//                commented this case out because info is always remote
//                (info == o2_context->info &&
//                 (tag == NODE_HASH || tag == NODE_HANDLER ||
//                  tag == NODE_OSC_REMOTE_SERVICE || tag == NODE_BRIDGE_SERVICE))) {
                DA_EXPAND(o2_services_rslt, service_data);
                service_data_ptr sdp = DA_LAST(o2_services_rslt, service_data);
                sdp->ss = ss;
                sdp->s = s;
                sdp->i = i;
            }
        }
        if (taps_flag) {
            for (int i = 0; i < ss->taps.length; i++) {
                tap_entry_ptr tap = GET_TAP(ss->taps, i);
                if (tap->proc == info) {
                    DA_EXPAND(o2_taps_rslt, tap_data);
                    tap_data_ptr tdp = DA_LAST(o2_taps_rslt, tap_data);
                    tdp->ss = ss;
                    tdp->tap = tap;
                }                    
            }
        }
    }
}
*/


// for each service named in info (a o2n_info_ptr),
//     find the service offered by this process and remove it:
//         since info has tag INFO_TCP_SOCKET, each service will be a
//         pointer back to this o2n_info_ptr, so do not delete info
//     if a service is the last service in services, remove the 
//         services_entry as well
// deallocate the dynamic array holding service names
//
static int remove_remote_services(o2n_info_ptr info)
{
    while (info->proc.services.length > 0) {
        services_entry_ptr ss = DA_GET(info->proc.services,
                                       proc_service_data, 0)->services;
        if (o2_service_remove(ss->key, info, ss, -1) != O2_SUCCESS) {
            break;
        }
    }
    return O2_SUCCESS;
}


static int remove_taps_by(o2n_info_ptr info)
{
    while (info->proc.taps.length > 0) {
        proc_tap_data_ptr ptdp = DA_GET(info->proc.taps, proc_tap_data, 0);
        if (o2_tap_remove_from(ptdp->services, info, ptdp->tapper) ==
            O2_FAIL) {
            return O2_FAIL; // avoid infinite loop, can't remove tap
        }
    }
    return O2_SUCCESS;
}


static int resize_table(hash_node_ptr node, int new_locs)
{
    dyn_array old = node->children; // copy whole dynamic array
    if (initialize_table(&(node->children), new_locs))
        return O2_FAIL;
    // now, old array is in old, node->children is newly allocated
    // copy all entries from old to nde->children
    assert(node->children.array != NULL);
    enumerate enumerator;
    o2_enumerate_begin(&enumerator, &old);
    o2_node_ptr entry;
    while ((entry = o2_enumerate_next(&enumerator))) {
        o2_node_add(node, entry);
    }
    // now we have moved all entries into the new table and we can free the
    // old one
    DA_FINISH(old);
    return O2_SUCCESS;
}


// o2_string_pad -- copy src to dst, adding zero padding to word boundary
//
// dst MUST point to a buffer of size NAME_BUF_LEN or bigger
//
void o2_string_pad(char *dst, const char *src)
{
    size_t len = strlen(src);
    if (len >= NAME_BUF_LEN) {
        len = NAME_BUF_LEN - 1;
    }
    // first fill last 32-bit word with zeros so the final word will be zero-padded
    int32_t *last_32 = (int32_t *)(dst + WORD_OFFSET(len)); // round down to word boundary
    *last_32 = 0;
    // now copy the string; this may overwrite some zero-pad bytes:
    strncpy(dst, src, len);
}

/******************* properties ********************/

typedef struct service_info {
    o2string name;
    int service_type;
    o2string process; // the port:ip of process offering the service
    o2string properties; // service properties or the tapper of tappee
} service_info, *service_info_ptr;

static dyn_array service_list = {0, 0, NULL};


// add services from proc that are active to service_list
void add_to_services_list(o2n_info_ptr proc)
{
    for (int i = 0; i < proc->proc.services.length; i++) {
        proc_service_data_ptr psdp = DA_GET(proc->proc.services,
                                            proc_service_data, i);
        services_entry_ptr ss = psdp->services;
        o2_node_ptr service;
        if (ss->services.length > 0 &&
            (service = GET_SERVICE(ss->services, 0)) &&
            (((service->tag == INFO_TCP_SOCKET &&
              (o2n_info_ptr) service == proc)) ||
             ((service->tag == NODE_HASH ||
               service->tag == NODE_HANDLER) &&
            proc == o2_context->info))) {
            DA_EXPAND(service_list, service_info);
            service_info_ptr sip = DA_LAST(service_list, service_info);
            sip->name = o2_heapify(psdp->services->key);
            sip->process = o2_heapify(proc->proc.name);
            sip->service_type = (proc == o2_context->info ?
                                 O2_LOCAL : O2_REMOTE);
            sip->properties = psdp->properties;
            if (sip->properties) { // need to own string if any
                sip->properties = o2_heapify(psdp->properties);
            }
        }
    }
    for (int i = 0; i < proc->proc.taps.length; i++) {
        proc_tap_data_ptr ptdp = DA_GET(proc->proc.taps, proc_tap_data, i);
        DA_EXPAND(service_list, service_info);
        service_info_ptr sip = DA_LAST(service_list, service_info);
        sip->name = o2_heapify(ptdp->services->key);
        sip->process = o2_heapify(proc->proc.name);
        sip->service_type = O2_TAP;
        sip->properties = o2_heapify(ptdp->tapper);
    }
}


// add every service to service_list. Get services from list of
// processes.
int o2_services_list()
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    o2_services_list_free();
    for (int i = 0; i < o2_context->fds.length; i++) {
        o2n_info_ptr proc = GET_PROCESS(i);
        // note: TCP_SERVER is the local process, o2_context->info
        if (proc->tag == INFO_TCP_SOCKET || proc->tag == INFO_TCP_SERVER) {
            add_to_services_list(proc);
        }
    }
    return O2_SUCCESS;
}


int o2_services_list_free()
{
    for (int i = 0; i < service_list.length; i++) {
        service_info_ptr sip = DA_GET(service_list, service_info, i);
        O2_FREE(sip->name);
        O2_FREE(sip->process);
        O2_FREE(sip->properties); // NULL is OK
    }
    service_list.length = 0;
    return O2_SUCCESS;
}


const char *o2_service_name(int i)
{
    if (i >= 0 && i < service_list.length) {
        service_info_ptr sip = DA_GET(service_list, service_info, i);
        return sip->name;
    }
    return NULL;
}


int o2_service_type(int i)
{
    if (i >= 0 && i < service_list.length) {
        service_info_ptr sip = DA_GET(service_list, service_info, i);
        return sip->service_type;
    }
    return NULL;
}


const char *o2_service_process(int i)
{
    if (i >= 0 && i < service_list.length) {
        service_info_ptr sip = DA_GET(service_list, service_info, i);
        return sip->process;
    }
    return NULL;
}


const char *o2_service_tapper(int i)
{
    if (i >= 0 && i < service_list.length) {
        service_info_ptr sip = DA_GET(service_list, service_info, i);
        if (sip->service_type != O2_TAP) {
            return NULL; // there is no tapper, it's a service
        }
        return sip->properties;
    }
    return NULL;
}


const char *o2_service_properties(int i)
{
    if (i >= 0 && i < service_list.length) {
        service_info_ptr sip = DA_GET(service_list, service_info, i);
        if (sip->service_type == O2_TAP) {
            return NULL; // it's a tap
        } // otherwise it's a service and properties is good
        return (sip->properties ? sip->properties + 1 : // skip initial ";"
                ";" + 1); // generate an initial ";" for o2_service_search()
    }
    return NULL;
}


// find end of attribute (a pointer to ':' in properties string)
// attr is the attribute with no ';' or ':'. We construct ";attr:"
// and search for an exact match to find the attribute in the properties
// string.
static const char *find_attribute_end(const char *attr, const char *properties)
{
    if (properties) {
        int len = strlen(attr);
        char exact[MAX_SERVICE_LEN];
        // construct the string ";attr:" to get exact match
        exact[0] = ';';
        exact[1] = 0;
        if (len + 3 > MAX_SERVICE_LEN) {
            return NULL; // attr is too big!
        }
        strcpy(exact + 1, attr);
        exact[len + 1] = ':';
        exact[len + 2] = 0;
        const char *loc = strstr(properties, exact);
        if (loc) {
            return loc + len + 2;
        }
    }
    return NULL;
}


// how long is the value string starting at loc
static int value_len(const char *loc)
{
    const char *end = loc;
    while (*end && *end != ';') {
        if (*end && *end == '\\') {
            end++;
        }
        end++;
    }
    return end - loc; // length not including terminating character
}


// how long will value string be after inserting escape chars?
static int value_encoded_len(const char *loc)
{
    int len = 0;
    while (*loc) {
        if (*loc == '\\' || *loc == ':' || *loc == ';') len++;
        loc++;
        len++;
    }
    return len;
}


const char *o2_service_getprop(int i, const char *attr)
{
    const char *p = o2_service_properties(i);
    if (p) {
        p--; // back up to initial ";"
        const char *loc = find_attribute_end(attr, p);
        if (loc) {
            int len = value_len(loc);
            const char *end = loc + len;
            // len may be too big given that we will remove escape characters
            char *rslt = O2_MALLOC(len + 1); // include space for EOS
            // copy string value, removing escape characters
            char *dest = rslt;
            while (loc < end) {
                if (*loc == '\\') { // skip escape characters
                    loc++;
                }
                *dest++ = *loc++;
            }
            *dest = 0; // end-of-string
            return rslt;
        }
    }
    return NULL;
}


int o2_service_search(int i, const char *attr, const char *value)
{
    while (i >= 0 && i < service_list.length) {
        const char *p = o2_service_properties(i);
        if (p) {
            p--; // back up to initial ";"
            const char *v = find_attribute_end(attr, p);
            if (v) {
                int len = value_len(v);
                // start searching at the ":" preceding v
                const char *loc = strstr(v - 1, value);
                // search must find value before v + len
                if (loc != NULL && loc <= v + len) {
                    return i;
                } // otherwise, value not found, continue search
            } // otherwise, attr not found, continue search
        } // otherwise it's a tap, continue search
        i++;
    }    
    return -1;
}


static void encode_value_to(char *p, const char *v)
{
    while (*v) {
        if (*v == '\\' || *v == ':' || *v == ';') *p++ = '\\';
        *p++ = *v++;
    }
}


// returns true if properties string has changed
//
static int service_property_free(proc_service_data_ptr psdp,
                                  const char *attr)
{
    // see if attr already exists. If so, just remove it in place.
    const char *attr_end = find_attribute_end(attr, psdp->properties);
    if (attr_end) {
        // find beginning of attr as destination for copy
        char *dst = (char *) attr_end - strlen(attr) - 1;
        // find end of value
        const char *src = attr_end + value_len(attr_end) + 1;
        // splice out attr:value; <- the "+ 1" above is to splice out the ';'
        while (*src) {
            *dst++ = *src++;
        }
        *dst = 0;
        return TRUE;
    }
    return FALSE;
}

    
// add property at front of old properties; assume old properties does
// not contain attr
// 
static void service_property_add(proc_service_data_ptr psdp, const char *attr,
                                 const char *value)
{
    // allocate space for new properties string
    // need attr, ':', escaped value, ';', existing string, eos
    int attr_len = strlen(attr);
    int value_len = value_encoded_len(value);
    const char *old_p = psdp->properties;
    // we want the old properties to be a real string to avoid checking for NULL
    if (!old_p) old_p = ";";
    int len =  attr_len + value_len + strlen(old_p) + 3;
    char *p = O2_MALLOC(len);
    p[0] = ';';
    strcpy(p + 1, attr);
    p[1 + attr_len] = ':'; // "1 +" is for leading ';'
    encode_value_to(p + 2 + attr_len, value);
    p[attr_len + value_len + 2] = ';'; // "+ 2" is for ';' and ':'
    // when copying psdp->properties, skip the leading ';' because we
    // already inserted that. We could have appended the new property
    // instead or prepending and things would work out a little cleaner,
    // but since attr has just changed, maybe lookups of attr are more
    // likely and putting it first will make lookups faster
    strcpy(p + attr_len + value_len + 3, old_p + 1);
    O2_FREE(psdp->properties);
    psdp->properties = p;
}


int o2_service_set_property(const char *service, const char *attr,
                            const char *value)
{
    // find service data matching service
    for (int i = 0; i < o2_context->info->proc.services.length; i++) {
        proc_service_data_ptr psdp = DA_GET(o2_context->info->proc.services,
                                            proc_service_data, i);
        if (streql(psdp->services->key, service)) {
            service_property_free(psdp, attr);
            service_property_add(psdp, attr, value);
            o2_notify_others(service, TRUE, NULL, psdp->properties + 1);
            return O2_SUCCESS;
        }
    }
    return O2_FAIL;
}


int o2_service_property_free(const char *service, const char *attr)
{
    // find service data matching service
    for (int i = 0; i < o2_context->info->proc.services.length; i++) {
        proc_service_data_ptr psdp = DA_GET(o2_context->info->proc.services,
                                            proc_service_data, i);
        if (streql(psdp->services->key, service)) {
            if (service_property_free(psdp, attr)) {
                o2_notify_others(service, TRUE, NULL, psdp->properties + 1);
            }
            return O2_SUCCESS;
        }
    }
    return O2_FAIL;
}
