//
//  o2_search.c
//  o2
//
//  Created by 弛张 on 3/14/16.
//
//
// delivery is recursive due to bundles. Here's an overview of the structure:
//
// o2_message_send(o2_message_ptr msg, int schedulable)
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
//         Calls o2_message_send2(msg, FALSE) to send message
//         (FALSE prevents a locally scheduled message you are trying
//         to dispatch from being considered for scheduling using the
//         o2_gtsched, which may not be working yet.)
// o2_msg_data_deliver(o2_msg_data_ptr msg, int tcp_flag,
//                     generic_entry_ptr service)
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

static void free_entry(generic_entry_ptr entry);
static void free_node(node_entry_ptr node);
static void string_pad(char *dst, char *src);
static int o2_pattern_match(const char *str, const char *p);
static int entry_remove(node_entry_ptr node, generic_entry_ptr *child, int resize);
static int remove_method_from_tree(char *remaining, char *name,
                                   node_entry_ptr node);
static int remove_node(node_entry_ptr node, const char *key);
static int remove_remote_services(fds_info_ptr info);
static int resize_table(node_entry_ptr node, int new_locs);

#if IS_LITTLE_ENDIAN
// for little endian machines
#define STRING_EOS_MASK 0xFF000000
#else
#define STRING_EOS_MASK 0x000000FF
#endif
#define SCRAMBLE 2686453351680

#define O2_MAX_NODE_NAME_LEN 1020
#define NAME_BUF_LEN ((O2_MAX_NODE_NAME_LEN) + 4)

#define MAX_SERVICE_NUM  1024

node_entry master_table;
node_entry path_tree_table;


// add_entry_at inserts an entry into the hash table. If the
// table becomes too full, a new larger table is created. 
// This function is called after o2_lookup() has been used to
// determine a pointer to the new entry. This pointer is
// passed in loc.
//
static int add_entry_at(node_entry_ptr node, generic_entry_ptr *loc,
                        generic_entry_ptr entry)
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
                         char *types)
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
        char *typ;
        for (typ = handler->type_string; *typ; typ++) {
            o2_arg_ptr next = o2_get_next(*typ);
            if (!next) {
                return; // type mismatch, do not deliver the message
            }
        }
        types = handler->type_string; // so that handler gets coerced types
        extern dyn_array o2_argv_data;   // these are (mostly) private
        extern dyn_array o2_arg_data;    //     to o2_message.c
        assert(o2_arg_data.allocated >= o2_arg_data.length);
        assert(o2_argv_data.allocated >= o2_argv_data.length);
    } else {
        o2_argv = NULL;
        o2_argc = 0;
    }
    (*(handler->handler))(msg, types, o2_argv, o2_argc, handler->user_data);
}


// create a node in the path tree
//
// key is "owned" by caller
//
static node_entry_ptr create_node(const char *key)
{
    node_entry_ptr node = (node_entry_ptr) O2_MALLOC(sizeof(node_entry));
    if (!node) return NULL;
    return o2_node_initialize(node, key);
}


static void enumerate_begin(enumerate *enumerator, dyn_array_ptr dict)
{
    enumerator->dict = dict;
    enumerator->index = 0;
    enumerator->entry = NULL;
}


// return next entry from table. Entries can be inserted into
// a new table because enumerate_next does not depend upon the
// pointers in each entry once the entry is enumerated.
//
static generic_entry_ptr enumerate_next(enumerate_ptr enumerator)
{
    while (!enumerator->entry) {
        int i = enumerator->index++;
        if (i >= enumerator->dict->length) {
            return NULL; // no more entries
        }
        enumerator->entry = *DA_GET(*(enumerator->dict),
                                    generic_entry_ptr, i);
    }
    generic_entry_ptr ret = enumerator->entry;
    enumerator->entry = enumerator->entry->next;
    return ret;
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
        generic_entry_ptr node, o2_msg_data_ptr msg, char *types)
{
    char *slash = strchr(remaining, '/');
    // if (slash) *slash = 0;
    char *pattern = strpbrk(remaining, "*?[{");
    if (pattern) { // this is a pattern 
        enumerate enumerator;
        enumerate_begin(&enumerator, &(((node_entry_ptr)node)->children));
        generic_entry_ptr entry;
        while ((entry = enumerate_next(&enumerator))) {
            if (slash && (entry->tag == PATTERN_NODE) &&
                (o2_pattern_match(entry->key, remaining) == O2_SUCCESS)) {
                find_and_call_handlers_rec(slash + 1, name, entry, msg, types);
            } else if (!slash && (entry->tag == PATTERN_HANDLER)) {
                char *path_end = remaining + strlen(remaining);
                path_end = WORD_ALIGN_PTR(path_end);
                call_handler((handler_entry_ptr) entry, msg, path_end + 5);
            }
        }
    } else { // no pattern characters so do hash lookup
        int index;
        if (slash) *slash = 0;
        string_pad(name, remaining);
        if (slash) *slash = '/';
        generic_entry_ptr entry = *o2_lookup((node_entry_ptr) node, name, &index);
        if (entry) {
            if (slash && (entry->tag == PATTERN_NODE)) {
                find_and_call_handlers_rec(slash + 1, name, entry,
                                           msg, types);
            } else if (!slash && (entry->tag == PATTERN_HANDLER)) {
                char *path_end = remaining + strlen(remaining);
                path_end = WORD_ALIGN_PTR(path_end);
                call_handler((handler_entry_ptr) entry, msg, path_end + 5);
            }
        }
    }
}


// free_entry - when an entry is inserted into a table, it
// may conflict with a previous entry. For example, if you
// define a handler for /a/b/1 and /a/b/2, then define a
// handler for /a/b, the table representing /a/b/ and
// containing entries for 1 and 2 will be replaced by the
// new handler for /a/b. This function recursively deletes
// and frees subtrees (such as the one rooted at /a/b).
// As a side-effect, the full paths (such as /a/b/1 and
// /a/b/2) corresponding to leaf nodes in the tree will be
// removed from the master_table, which hashes full paths.
// Note that the master_table entries do not have full_path
// fields (the .full_path field is NULL), so we know that
// an entry is in the path_tree_table by looking at the
// .full_path field.
//
// The parameter should be an entry to remove -- either an
// internal entry (PATTERN_NODE) or a leaf entry (PATTERN_HANDLER)
// 
static void free_entry(generic_entry_ptr entry)
{
    if (entry->tag == PATTERN_NODE) {
        free_node((node_entry_ptr) entry);
        return;
    } else if (entry->tag == PATTERN_HANDLER) {
        handler_entry_ptr handler = (handler_entry_ptr) entry;
        // if we remove a leaf node from the tree, remove the
        //  corresponding full path:
        if (handler->full_path) {
            remove_node(&master_table, handler->full_path);
            handler->full_path = NULL; // this string should be freed
                // in the previous call to remove_node(); remove the
                // pointer so if anyone tries to reference it, it will
                // generate a more obvious and immediate runtime error.
        }
        if (handler->type_string)
            O2_FREE(handler->type_string);
    } else if (entry->tag == O2_REMOTE_SERVICE) {
        // nothing special to do here. "parent" is a process,
        // but it is "owned" by pointer in o2_fds_info.
    } // else if OSC_REMOTE_SERVICE: TCP socket, if any, is deleted by
      // o2_service_free()
    O2_FREE(entry->key);
    O2_FREE(entry);
}


static void free_node(node_entry_ptr node)
{
    o2_node_finish(node);
    O2_FREE(node);
}


// The hash function processes 4 bytes at a time and is based
// on the idea (and I think this is what Java uses) of repeatedly
// multiplying the hash by 5 and adding the next character until
// all characters are used. The SCRAMBLE number is (5 << 8) +
// ((5 * 5) << 16 + ..., so it is similar to doing the multiplies
// and adds all in parallel for 4 bytes at a time.
static int64_t get_hash(const char *key)
{
    int32_t *ikey = (int32_t *) key;
    uint64_t hash = 0;
    int32_t c;
    do {
        c = *ikey++;
        hash = ((hash + c) * SCRAMBLE) >> 32;
    } while (c & STRING_EOS_MASK);
    return hash;
}


static int initialize_table(dyn_array_ptr table, int locations)
{
    DA_INIT(*table, generic_entry_ptr, locations);
    if (!table->array) return O2_FAIL;
    memset(table->array, 0, locations * sizeof(generic_entry_ptr));
    table->allocated = locations;
    table->length = locations;
/*    printf("initialize_table %p len %d, array %p\n", table, table->length, table->array);
    int i;
    for (i = 0; i < table->length; i++)
        printf("   %d: %p\n", i, ((generic_entry_ptr *)(table->array))[i]); */
    return O2_SUCCESS;
}


// o2_entry_add inserts an entry into the hash table. If the table becomes
// too full, a new larger table is created.
//
int o2_entry_add(node_entry_ptr node, generic_entry_ptr entry)
{
    int index;
    generic_entry_ptr *ptr = o2_lookup(node, entry->key, &index);
    if (*ptr) { // if we found it, this is a replacement
        entry_remove(node, ptr, FALSE); // splice out existing entry and delete it
    } else {
        assert(index < node->children.length);
        // where to put new entry:
        ptr = DA_GET(node->children, generic_entry_ptr, index);
    }
    return add_entry_at(node, ptr, entry);
}


// insert whole path into master table, insert path nodes into tree
// if this path exists, then first remove all sub-tree paths
//
// path is "owned" by caller (so it is copied here)
//
int o2_method_new(const char *path, const char *typespec,
                  o2_method_handler h, void *user_data, int coerce, int parse)
{
    char *key = o2_heapify(path);
    *key = '/'; // force key's first character to be '/', not '!'
    
    // add path elements as tree nodes -- to get the keys, replace each
    // "/" with EOS and o2_heapify to copy it, then restore the "/"
    char *remaining = key + 1;
    node_entry_ptr table;
    char name[NAME_BUF_LEN];
    char *slash;
    
    table = &path_tree_table;
    
    while ((slash = strchr(remaining, '/'))) {
        *slash = 0; // terminate the string at the "/"
        string_pad(name, remaining);
        *slash = '/'; // restore the string
        remaining = slash + 1;
        // if necessary, allocate a new entry for name
        table = o2_tree_insert_node(table, name);
        assert(table);
        // table is now the node for name
    }
    
    // now table is where we should put the final path name with the handler
    // remaining points to the final segment of the path
    string_pad(name, remaining);
    
    // entry is now a valid location. Insert a new node:
    handler_entry_ptr handler = (handler_entry_ptr) O2_MALLOC(sizeof(handler_entry));
    if (!handler) {
        return O2_FAIL;
    }
    handler->tag = PATTERN_HANDLER;
    handler->key = o2_heapify(remaining);
    handler->handler = h;
    handler->user_data = user_data;
    handler->full_path = key; // key will also be master_table key
    char *types_copy = NULL;
    int types_len = 0;
    if (typespec) {
        types_copy = o2_heapify(typespec);
        if (!types_copy) {
            return O2_FAIL;
        }
        // coerce to int to avoid compiler warning -- this could overflow but only
        // in cases where it would be impossible to construct a message
        types_len = (int) strlen(typespec);
    }
    handler->type_string = types_copy;
    handler->types_len = types_len;
    handler->coerce_flag = coerce;
    handler->parse_args = parse;
    int ret = o2_entry_add(table, (generic_entry_ptr) handler);
    if (ret) {
        // TODO CLEANUP
        return ret;
    }
    
    // make an entry for the master table
    handler = (handler_entry_ptr) O2_MALLOC(sizeof(handler_entry));
    
    handler->tag = PATTERN_HANDLER;
    handler->key = key; // this key has already been copied
    handler->handler = h;
    handler->user_data = user_data;
    handler->full_path = NULL; // only leaf nodes have full_path pointer
    // typespec will be freed, so we can't share copies
    if (types_copy) types_copy = o2_heapify(typespec);
    handler->type_string = types_copy;
    handler->types_len = types_len;
    handler->coerce_flag = coerce;
    handler->parse_args = parse;
    
    // put the entry in the master table
    return o2_entry_add(&master_table, (generic_entry_ptr) handler);
}


// Add remote service to the path_tree_table
//
// service is "owned" by the caller
//
int o2_remote_service_add(fds_info_ptr info, const char *service)
{
    int index;
    generic_entry_ptr *entry_ptr = o2_lookup(&path_tree_table, service, &index);
    o2_remote_process_new_at(service, entry_ptr, info);
    return O2_SUCCESS;
}


// note name should be heapified and owned by caller, eventually it is owned
// by the fds_info record for the remote process TCP_SOCKET.
//
remote_service_entry_ptr o2_remote_process_new_at(
        const char *name, generic_entry_ptr *entry_ptr, fds_info_ptr info)
{
    remote_service_entry_ptr process = O2_MALLOC(sizeof(remote_service_entry));
    process->tag = O2_REMOTE_SERVICE;
    process->key = o2_heapify(name);
    process->next = NULL;
    process->process_index = (int) INFO_TO_INDEX(info);
    add_entry_at(&path_tree_table, entry_ptr, (generic_entry_ptr) process);
    return process;
}


// remove a service from path_tree_table
//
int o2_service_free(char *service_name)
{
    generic_entry_ptr *entry_ptr = o2_service_find(service_name);
    if (!*entry_ptr) return O2_FAIL;
    int tag = (*entry_ptr)->tag;
    switch (tag) {
        case O2_REMOTE_SERVICE: 
            // fails because you can only remove a local service
        case O2_BRIDGE_SERVICE: // fails because not implemented yet
        default: // fails because service is unrecognized
            return O2_FAIL; 
        case PATTERN_NODE:
        case PATTERN_HANDLER:
            break;
        case OSC_REMOTE_SERVICE: {
            // shut down any OSC connection
            osc_entry_ptr osc_entry = (osc_entry_ptr) (*entry_ptr);
            if (osc_entry->fds_index >= 0) { // TCP
                o2_socket_mark_to_free(osc_entry->fds_index); // close the TCP socket
                osc_entry->fds_index = -1;   // just to be safe
            }
            break;
        }
    }
    entry_remove(&path_tree_table, entry_ptr, TRUE);
    o2_notify_others(service_name, FALSE);
    return O2_SUCCESS;
}


int o2_remote_service_remove(const char *service)
{
    generic_entry_ptr *node_ptr = o2_service_find(service);
    if (*node_ptr) {
        int tag = (*node_ptr)->tag;
        if (tag != O2_REMOTE_SERVICE) {
            fprintf(stderr, "O2 WARNING: some other process claims it "
                    "is deleting service %s, but %s is local to THIS "
                    "process. Perhaps both created the same service. "
                    "This would be an error: services must be unique.\n",
                    service, service);
            return O2_FAIL;
        }
        return entry_remove(&path_tree_table, node_ptr, TRUE);
    }
    return O2_FAIL;
}


int o2_embedded_msgs_deliver(o2_msg_data_ptr msg, int tcp_flag)
{
    char *end_of_msg = PTR(msg) + MSG_DATA_LENGTH(msg);
    o2_msg_data_ptr embedded = (o2_msg_data_ptr) (msg->address + o2_strsize(msg->address) + sizeof(int32_t));
    while (PTR(embedded) < end_of_msg) {
        o2_msg_data_send(embedded, tcp_flag);
        embedded = (o2_msg_data_ptr)
                (PTR(embedded) + MSG_DATA_LENGTH(embedded) + sizeof(int32_t));
    }
    return O2_SUCCESS;
}


// deliver msg locally and immediately
void o2_msg_data_deliver(o2_msg_data_ptr msg, int tcp_flag,
                         generic_entry_ptr service)
{
    if (IS_BUNDLE(msg)) {
        o2_embedded_msgs_deliver(msg, tcp_flag);
        return;
    }

    char *address = msg->address;
    if (!service) {
        service = *o2_service_find(address + 1);
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
    if (service->tag == PATTERN_HANDLER) {
        call_handler((handler_entry_ptr) service, msg, types);
    } else if ((address[0]) == '!') { // do full path lookup
        int index;
        address[0] = '/'; // must start with '/' to get consistent hash value
        generic_entry_ptr handler = *o2_lookup(&master_table, address, &index);
        address[0] = '!'; // restore address for no particular reason
        if (handler && handler->tag == PATTERN_HANDLER) {
            call_handler((handler_entry_ptr) handler, msg, types);
        }
    } else if (service->tag == PATTERN_NODE) {
        char name[NAME_BUF_LEN];
        address = strchr(address + 1, '/'); // search for end of service name
        if (!address) {
            // address is "/service", but "/service" is not a PATTERN_HANDLER
            ;
        } else {
            find_and_call_handlers_rec(address + 1, name, service, msg, types);
        }
    } // the assumption that the service is local fails, drop the message
}


void o2_node_finish(node_entry_ptr node)
{
    for (int i = 0; i < node->children.length; i++) {
        generic_entry_ptr e = *DA_GET(node->children, generic_entry_ptr, i);
        while (e) {
            generic_entry_ptr next = e->next;
            free_entry(e);
            e = next;
        }
    }
    O2_FREE((void *) node->key);
}


// copy a string to the heap, result is 32-bit word aligned, has
//   at least one zero end-of-string byte and is
//   zero-padded to the next word boundary
char *o2_heapify(const char *path)
{
    long len = o2_strsize(path);
    char *rslt = (char *) O2_MALLOC(len);
    if (!rslt) return NULL;
    // zero fill last 4 bytes
    int32_t *end_ptr = (int32_t *) WORD_ALIGN_PTR(rslt + len - 1);
    *end_ptr = 0;
    strcpy(rslt, path);
    return rslt;
}


// set fields for a node in the path tree
//
// key is "owned" by the caller
//
node_entry_ptr o2_node_initialize(node_entry_ptr node, const char *key)
{
    node->tag = PATTERN_NODE;
    node->key = o2_heapify(key);
    if (!node->key) {
        O2_FREE(node);
        return NULL;
    }
    node->num_children = 0;
    initialize_table(&(node->children), 2);
    return node;
}


// o2_lookup returns a pointer to a pointer to the entry, if any.
// The hash table uses linked lists for collisions to make
// deletion simple. key must be aligned on a 32-bit word boundary
// and must be padded with zeros to a 32-bit boundary
generic_entry_ptr *o2_lookup(node_entry_ptr node, const char *key, int *index)
{
    int n = node->children.length;
    int64_t hash = get_hash(key);
    *index = hash % n;
    // printf("o2_lookup %s in %s hash %ld index %d\n", key, node->key, hash, *index);
    generic_entry_ptr *ptr = DA_GET(node->children, generic_entry_ptr,
                                    *index);
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
    // make a zero-padded copy of path
    long len = (long) strlen(path) + 1;
    char *path_copy = (char *) alloca(len);
    if (!path_copy) return O2_FAIL;
    memcpy(path_copy, path, len);
    char name[NAME_BUF_LEN];
    
    // search path elements as tree nodes -- to get the keys, replace each
    // "/" with EOS and o2_heapify to copy it, then restore the "/"
    char *remaining = path_copy + 1; // skip the initial "/"
    return remove_method_from_tree(remaining, name, &path_tree_table);
}


int o2_remove_remote_process(fds_info_ptr info)
{
    if (info->tag == TCP_SOCKET) {
        // remove the remote services provided by the proc
        remove_remote_services(info);
        // proc.name may be NULL if we have not received an init (/_o2/dy) message
        if (info->proc.name) {
            // remove the remote service associated with the ip_port string
            o2_remote_service_remove(info->proc.name);
            O2_DBD(printf("%s removing remote process %s\n",
                          o2_debug_prefix, info->proc.name));
            O2_FREE(info->proc.name);
            info->proc.name = NULL;
        }
        
    } // else if (info->tag == OSC_TCP_SOCKET) do nothing special
    // else if (info->tag == OSC_REMOTE_SERVICE
    o2_socket_mark_to_free((int) INFO_TO_INDEX(info)); // close the TCP socket
    return O2_SUCCESS;
}


// o2_tree_insert_node -- insert a node for pattern matching.
// on entry, table points to a tree node pointer, initially it is the
// address of path_tree_table. If key is already in the table and the
// entry is another node, then just return a pointer to the node address.
// Otherwise, if key is a handler, remove it, and then create a new node
// to represent this key.
//
// key is "owned" by caller and must be aligned to 4-byte word boundary
//
node_entry_ptr o2_tree_insert_node(node_entry_ptr node, const char *key)
{
    int index; // location returned by o2_lookup
    assert(node->children.length > 0);
    generic_entry_ptr *entry_ptr = o2_lookup(node, key, &index);
    // 3 outcomes: entry exists and is a PATTERN_NODE: return location
    //    entry exists but is something else: delete old and create one
    //    entry does not exist: create one
    if (*entry_ptr) {
        if ((*entry_ptr)->tag == PATTERN_NODE) {
            return (node_entry_ptr) *entry_ptr;
        } else {
            // this node cannot be a handler (leaf) and a (non-leaf) node
            entry_remove(node, entry_ptr, FALSE);
        }
    } else {
        assert(index < node->children.length);
        entry_ptr = DA_GET(node->children, generic_entry_ptr, index);
    }
    // entry is a valid location. Insert a new node:
    node_entry_ptr new_entry = create_node(key);
    add_entry_at(node, entry_ptr, (generic_entry_ptr) new_entry);
    return new_entry;
}


// remove a child from a node. Then free the child
// (deleting its entire subtree, or if it is a leaf, removing the
// entry from the master_table).
// ptr is the address of the pointer to the table entry to be removed.
// This ptr must be a value returned by o2_lookup or o2_service_find
// Often, we remove an entry to make room for an insertion, so
// we do not want to resize the table. The resize parameter must
// be true to enable resizing.
//
static int entry_remove(node_entry_ptr node, generic_entry_ptr *child, int resize)
{
    node->num_children--;
    generic_entry_ptr entry = *child;
    *child = entry->next;
    free_entry(entry);
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
                                   node_entry_ptr node)
{
    char *slash = strchr(remaining, '/');
    int index; // return value from o2_lookup
    generic_entry_ptr *entry_ptr; // another return value from o2_lookup
    if (slash) { // we have an internal node name
        *slash = 0; // terminate the string at the "/"
        string_pad(name, remaining);
        *slash = '/'; // restore the string
        entry_ptr = o2_lookup(node, name, &index);
        if ((!*entry_ptr) || ((*entry_ptr)->tag != PATTERN_NODE)) {
            printf("could not find method\n");
            return O2_FAIL;
        }
        // *entry addresses a node entry
        node = (node_entry_ptr) *entry_ptr;
        remove_method_from_tree(slash + 1, name, node);
        if (node->num_children == 0) {
            // remove the empty table
            return entry_remove(node, entry_ptr, TRUE);
        }
        return O2_SUCCESS;
    }
    // now table is where we find the final path name with the handler
    // remaining points to the final segment of the path
    string_pad(name, remaining);
    entry_ptr = o2_lookup(node, name, &index);
    // there should be an entry, remove it
    if (*entry_ptr) {
        entry_remove(node, entry_ptr, TRUE);
        return O2_SUCCESS;
    }
    return O2_FAIL;
}


// remove a dictionary entry by name
// when we remove an entry, we may resize the table to be smaller
// in which case *node is written with a pointer to the new table
// and the old table is freed
//
static int remove_node(node_entry_ptr node, const char *key)
{
    int index;
    generic_entry_ptr *ptr = o2_lookup(node, key, &index);
    if (*ptr) {
        return entry_remove(node, ptr, TRUE);
    }
    return O2_FAIL;
}


static int remove_remote_services(fds_info_ptr info)
{
    int i, index;
    for (i = 0; i < info->proc.services.length; i++) {
        char *service = *DA_GET(info->proc.services, char *, i);
        generic_entry_ptr *node_ptr = o2_lookup(&path_tree_table, service, &index);
        assert(*node_ptr);
        // wait and resize later
        entry_remove(&path_tree_table, node_ptr, FALSE);
    }
    info->proc.services.length = 0;
    return O2_SUCCESS;
}


static int resize_table(node_entry_ptr node, int new_locs)
{
    dyn_array old = node->children; // copy whole dynamic array
    if (initialize_table(&(node->children), new_locs))
        return O2_FAIL;
    // now, old array is in old, node->children is newly allocated
    // copy all entries from old to nde->children
    assert(node->children.array != NULL);
    enumerate enumerator;
    enumerate_begin(&enumerator, &old);
    generic_entry_ptr entry;
    while ((entry = enumerate_next(&enumerator))) {
        o2_entry_add(node, entry);
    }
    // now we have moved all entries into the new table and we can free the
    // old one
    DA_FINISH(old);
    return O2_SUCCESS;
}


#define SEARCH_DEBUG
#ifdef SEARCH_DEBUG
void show_table(node_entry_ptr node, int indent)
{
    enumerate en;
    enumerate_begin(&en, &(node->children));
    generic_entry_ptr entry;
    while ((entry = enumerate_next(&en))) {
        int i;
        for (i = 0; i < indent; i++) printf("  ");
        printf("%s (%d) @ %p\n", entry->key, entry->tag, entry);

        // see if each entry can be found
        int index;
        generic_entry_ptr *ptr = o2_lookup(node, entry->key, &index);
        assert(*ptr == entry);

        if (entry->tag == PATTERN_NODE) {
            show_table((node_entry_ptr) entry, indent + 1);
        }
    }
}
#endif


// string_pad -- copy src to dst, adding zero padding to word boundary
//
// dst MUST point to a buffer of size NAME_BUF_LEN or bigger
//
static void string_pad(char *dst, char *src)
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



//Recieving messages.
/* TODO DELETE THIS
ssize_t o2_get_length(o2_type type, void *data)
{
    switch (type) {
        case O2_TRUE:
        case O2_FALSE:
        case O2_NIL:
        case O2_INFINITUM:
            return 0;
            
        case O2_INT32:
        case O2_FLOAT:
        case O2_MIDI:
        case O2_CHAR:
            return 4;
            
        case O2_INT64:
        case O2_TIME:
        case O2_DOUBLE:
            return 8;
            
        case O2_STRING:
        case O2_SYMBOL:
        {
            int i = 0, len = 0;
            char *pos = data;
            
            for (i = 0; ; ++i) {
                if (pos[i] == '\0') {
                    len = 4 * (i / 4 + 1);
                    break;
                }
            }
            for (; i < len; ++i) {
                if (pos[i] != '\0') {
                    return O2_FAIL;    // non-zero char found in pad area
                }
            }
            
            return len;

        }
            
        //case O2_BLOB:
            //return o2_validate_blob((o2_blob) data, size);
            
        default:
            return -O2_EINVALIDTYPE;
    }
    return -O2_INT_ERR;
}
*/

