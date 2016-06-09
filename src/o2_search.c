//
//  o2_search.c
//  o2
//
//  Created by 弛张 on 3/14/16.
//
//
#include "o2.h"
#include <stdio.h>
#include "o2_dynamic.h"
#include "o2_socket.h"
#include "o2_search.h"
#include "o2_internal.h"
#include "o2_message.h"
#include "o2_discovery.h"

#ifdef _WIN32
#include "malloc.h"
#define allloca _alloca

size_t strlcpy(char *dst, const char *src, size_t size)
{
    strncpy(dst, src, size);
}
#endif

// Declarations.
generic_entry_ptr enumerate_next(enumerate_ptr enumerator);
void enumerate_begin(enumerate_ptr enumerator, dyn_array_ptr array);

#ifndef NEGATE
#define NEGATE  '!'
#endif

int o2_pattern_match(const char *str, const char *p)
{
    int negate;
    int match;
    char c;
    
    while (*p) {
        if (!*str && *p != '*') {
            return FALSE;
        }
        
        switch (c = *p++) {
            case '*':
                while (*p == '*' && *p != '/') p++;
                
                if (!*p) return TRUE;
                
                //if (*p != '?' && *p != '[' && *p != '\\')
                if (*p != '?' && *p != '[' && *p != '{')
                    while (*str && *p != *str) str++;
                
                while (*str) {
                    if (o2_pattern_match(str, p)) {
                        return TRUE;
                    }
                    str++;
                }
                return FALSE;
                
            case '?':
                if (*str) break;
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
                    negate = 0;
                    p++;
                }
                
                match = 0;
                
                while (!match && (c = *p++)) {
                    if (!*p) {
                        return FALSE;
                    }
                    if (*p == '-') {        /* c-c */
                        if (!*++p)
                            return FALSE;
                        if (*p != ']') {
                            if (*str == c || *str == *p ||
                                (*str > c && *str < *p))
                                match = 1;
                        } else {    /* c-] */
                            if (*str >= c)
                                match = 1;
                            break;
                        }
                    } else {        /* cc or c] */
                        if (c == *str)
                            match = 1;
                        if (*p != ']') {
                            if (*p == *str)
                                match = 1;
                        } else {
                            break;
                        }
                    }
                }
                
                if (negate == match) {
                    return FALSE;
                }
                /*
                 * if there is a match, skip past the cset and continue on
                 */
                while (*p && *p != ']')
                    p++;
                if (!*p++) /* oops! */ {
                    return FALSE;
                }
                break;
                
            /*
             * {astring,bstring,cstring}
             */
            case '{': {
                // *p is now first character in the {brace list}
                const char *place = str;        // to backtrack
                const char *remainder = p;      // to forwardtrack
                
                // find the end of the brace list
                while (*remainder && *remainder != '}')
                    remainder++;
                if (!*remainder++)      /* oops! */
                { return O2_FAIL; }
                
                c = *p++;
                
                while (c) {
                    if (c == ',') {
                        if (o2_pattern_match(str, remainder)) {
                            return TRUE;
                        } else {
                            // backtrack on test string
                            str = place;
                            // continue testing,
                            // skip comma
                            if (!*p++) { // oops
                                return FALSE;
                            }
                        }
                    } else if (c == '}') {
                        // continue normal pattern matching
                        if (!*p && !*str) {
                            return TRUE;
                        }
                        str--;  // str is incremented again below
                        break;
                    } else if (c == *str) {
                        str++;
                        if (!*str && *remainder) {
                            return FALSE;
                        }
                    } else {    // skip to next comma
                        str = place;
                        while (*p != ',' && *p != '}' && *p)
                            p++;
                        if (*p == ',')
                            p++;
                        else if (*p == '}') {
                            return FALSE;
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
    
    return (*str == 0);
}


// for little endian machines
#define STRING_EOS_MASK 0xFF000000
#define SCRAMBLE 2686453351680

node_entry master_table;
node_entry path_tree_table;


// Declaration
int add_entry(node_entry_ptr node, generic_entry_ptr entry);


// The hash function processes 4 bytes at a time and is based
// on the idea (and I think this is what Java uses) of repeatedly
// multiplying the hash by 5 and adding the next character until
// all characters are used. The SCRAMBLE number is (5 << 8) +
// ((5 * 5) << 16 + ..., so it is similar to doing the multiplies
// and adds all in parallel for 4 bytes at a time.
int64_t get_hash(const char *key)
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

// lookup returns a pointer to a pointer to the entry, if any.
// The hash table uses linked lists for collisions to make
// deletion simple. key must be aligned on a 32-bit word boundary
// and must be padded with zeros to a 32-bit boundary
generic_entry_ptr *lookup(node_entry_ptr node, const char *key, int *index)
{
    int n = node->children.length;
    int64_t hash = get_hash(key);
    *index = hash % n;
    generic_entry_ptr *ptr = DA_GET(node->children, generic_entry_ptr,
                                    *index);
    while (*ptr) {
        if (streql(key, (*ptr)->key)) {
            return ptr;
        }
        ptr = &((*ptr)->next);
    }
    return NULL;
}


void free_node(node_entry_ptr node)
{
    for (int i = 0; i < node->children.length; i++) {
        generic_entry_ptr e = *DA_GET(node->children, generic_entry_ptr, i);
        while (e) {
            generic_entry_ptr next = e->next;
            free_entry(e);
            e = next;
        }
    }
    O2_FREE(node);
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
void free_entry(generic_entry_ptr entry)
{
    if (entry->tag == PATTERN_NODE) {
        free_node((node_entry_ptr) entry);
    } else if (entry->tag == PATTERN_HANDLER) {
        handler_entry_ptr handler = (handler_entry_ptr) entry;
        // if we remove a leaf node from the tree, remove the
        //  corresponding full path:
        if (handler->full_path) {
            remove_node(&master_table, handler->full_path);
        }
        if (handler->type_string)
            O2_FREE(handler->type_string);
    } else if (entry->tag == O2_REMOTE_SERVICE) {
        // nothing special to do here. "parent" is a process,
        // but it is "owned" by pointer in o2_fds_info.
    } else if (entry->tag == OSC_REMOTE_SERVICE) {
        // TODO: maybe close the TCP connection
    } // TODO: could there be an OSC_LOCAL_SERVICE here?
    O2_FREE(entry->key);
    O2_FREE(entry);
}

int initialize_table(dyn_array_ptr table, int locations)
{
    DA_INIT(*table, generic_entry_ptr, locations);
    if (!table->array) return O2_FAIL;
    memset(table->array, 0, locations * sizeof(generic_entry_ptr));
    table->allocated = locations;
    table->length = locations;
    return O2_SUCCESS;
}


int resize_table(node_entry_ptr node, int new_locs)
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
        add_entry(node, entry);
    }
    // now we have moved all entries into the new table and we can free the
    // old one
    DA_FINISH(old);
    return O2_SUCCESS;
}


// remove a child from a node. Then free the child
// (deleting its entire subtree, or if it is a leaf, removing the
// entry from the master_table).
// ptr is the address of the pointer to the entry to be removed.
// Often, we remove an entry to make room for an insertion, so
// we do not want to resize the table. The resize parameter must
// be true to enable resizing.
//
int remove_entry(node_entry_ptr node, generic_entry_ptr *child, int resize)
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


// remove a dictionary entry by name
// when we remove an entry, we may resize the table to be smaller
// in which case *node is written with a pointer to the new table
// and the old table is freed
//
int remove_node(node_entry_ptr node, const char *key)
{
    int index;
    generic_entry_ptr *ptr = lookup(node, key, &index);
    if (ptr) {
        return remove_entry(node, ptr, TRUE);
    }
    return O2_FAIL;
}


node_entry_ptr create_node(char *key)
{
    node_entry_ptr node = (node_entry_ptr) O2_MALLOC(sizeof(node_entry));
    if (!node) return NULL;
    return initialize_node(node, key);
}

node_entry_ptr initialize_node(node_entry_ptr node, char *key)
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


void enumerate_begin(enumerate *enumerator, dyn_array_ptr dict)
{
    enumerator->dict = dict;
    enumerator->index = 0;
    enumerator->entry = NULL;
}


// return next entry from table. Entries can be inserted into
// a new table because enumerate_next does not depend upon the
// pointers in each entry once the entry is enumerated.
//
generic_entry_ptr enumerate_next(enumerate_ptr enumerator)
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


// add_entry_at inserts an entry into the hash table. If the
// table becomes too full, a new larger table is created. 
// This function is called after lookup() has been used to
// determine a pointer to the new entry. This pointer is
// passed in loc.
//
int add_entry_at(node_entry_ptr node, generic_entry_ptr *loc,
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



// add_entry inserts an entry into the hash table. If the table becomes
// too full, a new larger table is created.
//
int add_entry(node_entry_ptr node, generic_entry_ptr entry)
{
    int index;
    generic_entry_ptr *ptr = lookup(node, entry->key, &index);
    if (ptr) { // if we found it, this is a replacement
        remove_entry(node, ptr, FALSE); // splice out existing entry and delete it
    } else {
        assert(index < node->children.length);
        // where to put new entry:
        ptr = DA_GET(node->children, generic_entry_ptr, index);
    }
    return add_entry_at(node, ptr, entry);
}


// copy a string to the heap, result is 32-bit word aligned, has
//   at least one zero end-of-string byte and is
//   zero-padded to the next word boundary
char *o2_heapify(const char *path)
{
    int len = strlen(path);
    // round up (including eos) to multiple of 4 bytes
    len = (len + 4) & ~0x3;
    char *rslt = (char *) O2_MALLOC(len);
    // zero fill last 4 bytes
    int32_t *end_ptr = (int32_t *) WORD_ALIGN_PTR(rslt + len - 1);
    *end_ptr = 0;
    strcpy(rslt, path);
    return rslt;
}


// tree_insert_node -- insert a node for pattern matching.
// on entry, table points to a tree node pointer, initially it is the
// address of path_tree_table. If key is already in the table and the
// entry is another node, then just return a pointer to the node address.
// Otherwise, if key is a handler, remove it, and then create a new node
// to represent this key.
node_entry_ptr tree_insert_node(node_entry_ptr node, char *key)
{
    int index; // location returned by lookup
    assert(node->children.length > 0);
    node_entry_ptr *entry = (node_entry_ptr *) lookup(node, key, &index);
    // 3 outcomes: entry exists and is a PATTERN_NODE: return location
    //    entry exists but is something else: delete old and create one
    //    entry does not exist: create one
    if (entry) {
        if ((*entry)->tag == PATTERN_NODE) {
            return *entry;
        } else {
            // this node cannot be a handler (leaf) and a (non-leaf) node
            remove_entry(node, (generic_entry_ptr *) entry, FALSE);
        }
    } else {
        assert(index < node->children.length);
        entry = DA_GET(node->children, node_entry_ptr, index);
    }
    // entry is a valid location. Insert a new node:
    node_entry_ptr new_entry = create_node(key);
    add_entry_at(node, (generic_entry_ptr *) entry,
                 (generic_entry_ptr) new_entry);
    return new_entry;
}


void string_pad(char *dst, char *src, int maxlen)
{
    size_t len = strlen(src);
    if (len >= maxlen) {
        len = maxlen - 1;
    }
    // first fill last 32-bit word with zeros so the final word will be zero-padded
    int32_t *last_32 = (int32_t *)(dst + WORD_OFFSET(len)); // round down to word boundary
    *last_32 = 0;
    // now copy the string; this may overwrite some zero-pad bytes:
    strncpy(dst, src, len);
}

#define O2_MAX_NODE_NAME_LEN 1024
#define NAME_BUF_LEN ((O2_MAX_NODE_NAME_LEN) + 4)

// recursive function to remove path from tree. Follow links to the leaf
// node, remove it, then as the stack unwinds, remove empty nodes.
// remaining is the full path, which is manipulated to isolate node names.
// name is storage to copy and pad node names.
// table is the current node.
//
// returns O2_FAIL if path is not found in tree (should not happen)
//
int remove_method_from_tree(char *remaining, char *name, node_entry_ptr node)
{
    char *slash = strchr(remaining, '/');
    int index; // return value from lookup
    generic_entry_ptr *entry; // another return value from lookup
    if (slash) { // we have an internal node name
        *slash = 0; // terminate the string at the "/"
        string_pad(name, remaining, NAME_BUF_LEN);
        *slash = "/"; // restore the string
        entry = lookup(node, name, &index);
        if ((!entry) || ((*entry)->tag != PATTERN_NODE)) {
            printf("could not find method\n");
            return O2_FAIL;
        }
        // *entry addresses a node entry
        node = (node_entry_ptr) *entry;
        remove_method_from_tree(slash + 1, name, node);
        if (node->num_children == 0) {
            // remove the empty table
            return remove_entry(node, entry, TRUE);
        }
        return O2_SUCCESS;
    }
    // now table is where we find the final path name with the handler
    // remaining points to the final segment of the path
    string_pad(name, remaining, NAME_BUF_LEN);
    entry = lookup(node, name, &index);
    // there should be an entry, remove it
    if (entry) {
        remove_entry(node, entry, TRUE);
        return O2_SUCCESS;
    }
    return O2_FAIL;
}


// remove a path -- find the leaf node in the tree and remove it.  The
// master table entry will be removed as a side effect. If a parent node
// becomes empty, the parent is removed. Thus we use a recursive algorithm
// so we can examine parents after visiting the children.
//
int o2_remove_method(const char *path)
{
    // make a zero-padded copy of path
    int len = strlen(path) + 1;
    char *path_copy = (char *) alloca(len);
    memcpy(path_copy, path, len);
    char name[NAME_BUF_LEN];
    
    // search path elements as tree nodes -- to get the keys, replace each
    // "/" with EOS and o2_heapify to copy it, then restore the "/"
    char *remaining = path_copy + 1; // skip the initial "/"
    return remove_method_from_tree(remaining, name, &path_tree_table);
}

#define MAX_SERVICE_NUM  1024



void init_process(process_info_ptr process, int status, int is_little_endian)
{
    process->name = NULL;
    process->status = PROCESS_DISCOVERED;
    DA_INIT(process->services, char *, 0);
    process->udp_port = 0;
    memset(&process->udp_sa, 0, sizeof(process->udp_sa));
    process->tcp_fd_index = -1;
}    


// Add remote service to the path_tree_table
process_info_ptr add_remote_process(const char *ip_port, int status,
                                     int is_little_endian)
{
    // make an entry for the path_tree_table
    process_info_ptr process = (process_info_ptr)
            O2_MALLOC(sizeof(process_info));
    if (!process) return NULL;
    init_process(process, status, is_little_endian);
    if (ip_port) {
        process->name = o2_heapify(ip_port);
        // put a remote service entry in the path_tree_table
        add_remote_service(process, ip_port);
    }
    // printf("%s: added remote process %s\n", debug_prefix, ip_port);
    return process;
}


// Add remote service to the path_tree_table
int add_remote_service(process_info_ptr process, const char *service)
{
    // make an entry for the path table
    remote_service_entry_ptr entry = (remote_service_entry_ptr)
                                     O2_MALLOC(sizeof(remote_service_entry));
    
    entry->tag = O2_REMOTE_SERVICE;
    entry->key = o2_heapify(service);
    entry->next = NULL;
    entry->parent = process;
    
    // put the entry in the path table
    add_entry(&path_tree_table, (generic_entry_ptr) entry);

    // service name also goes into process
    DA_APPEND(process->services, char *, entry->key);
    // printf("%s: added %s service at %s\n", debug_prefix, service, process->name);

    return O2_SUCCESS;
}


int add_local_osc(const char *path, int port, SOCKET tcp_socket)
{
    // make an entry for the path_tree_table
    osc_entry_ptr entry = O2_MALLOC(sizeof(struct osc_entry));
    
    entry->tag = OSC_LOCAL_SERVICE;
    entry->key = o2_heapify(path); 
    entry->ip[0] = 0;
    entry->port = port;
    entry->tcp_socket = tcp_socket;
    /* TODO: set udp_sa */
    
    /* TODO: BIND?
    if (o2_bind(&(entry->osc_entry.socket), 0) == -1) {
        perror("Bind udp socket");
        return O2_FAIL;
    }
    */
    // put the entry in the path_tree_table
    add_entry(&path_tree_table, (generic_entry_ptr) entry);
    return O2_SUCCESS;
}


// insert whole path into master table, insert path nodes into tree
// if this path exists, then first remove all sub-tree paths
//
int o2_add_method(const char *path, const char *typespec,
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
        string_pad(name, remaining, NAME_BUF_LEN);
        *slash = '/'; // restore the string
        remaining = slash + 1;
        // if necessary, allocate a new entry for name
        table = tree_insert_node(table, name);
        assert(table);
        // table is now the node for name
    }
    
    // now table is where we should put the final path name with the handler
    // remaining points to the final segment of the path
    string_pad(name, remaining, NAME_BUF_LEN);
    
    // entry is now a valid location. Insert a new node:
    handler_entry_ptr handler = (handler_entry_ptr) O2_MALLOC(sizeof(handler_entry));
    if (!handler) {
        return O2_FAIL;
    }
    handler->tag = PATTERN_HANDLER;
    handler->key = o2_heapify(remaining);
    char *types_copy = (typespec ? o2_heapify(typespec) : NULL);
    int arg_count = (typespec ? strlen(typespec) : 0);
    handler->type_string = types_copy;
    handler->argc = arg_count;
    handler->handler = h;
    handler->user_data = user_data;
    handler->full_path = key; // key will also be master_table key
    int ret = add_entry(table, (generic_entry_ptr) handler);
    if (ret) {
        // TODO CLEANUP
        return ret;
    }
    
    // make an entry for the master table
    handler = (handler_entry_ptr) O2_MALLOC(sizeof(handler_entry));
    
    handler->tag = PATTERN_HANDLER;
    handler->key = key;
    handler->handler = h;
    // typespec will be freed, so we can't share copies
    if (types_copy) types_copy = o2_heapify(typespec);
    handler->type_string = types_copy;
    handler->argc = arg_count;
    handler->user_data = user_data;
    handler->full_path = NULL; // only leaf nodes have full_path pointer
    
    // put the entry in the master table
    return add_entry(&master_table, (generic_entry_ptr) handler);
}


//Recieving messages.

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
void call_handler(handler_entry_ptr handler, o2_message_ptr msg,
                  char *types)
{
    o2_arg_ptr *argv = NULL;
    int argc = strlen(types);
    int free_argv_flag = FALSE; // boolean says that we need to free argv
    double *coerced; // array for coerced values

    // type checking
    if (handler->type_string && // mismatch detection needs type_string
        ((handler->argc != argc) ||  // first check if counts are equal
         !(handler->coerce_flag ||   // need coercion or exact match
           (streql(handler->type_string, types))))) {
        // printf("!!! %s: find_and_call_handlers skipping %s due to type mismatch\n", debug_prefix, msg->data.address);
        return; // type mismatch
    }
    if (handler->parse_args) {
        // argv is going to have a pointer per argument, and
        // if coercion is required, we could need another 64
        // bits per argument. This may all fit in the remainder
        // of message, so use that space if there is enough.
        // Otherwise, allocate one big block conservatively.
        int needed = argc * sizeof(o2_arg_ptr);
        if (handler->coerce_flag) {
            needed += argc * sizeof(double);
        }
        if (needed <= msg->allocated - msg->length) {
            argv = (o2_arg_ptr *) &(msg->data);
        } else {
            argv = (o2_arg_ptr *) o2_malloc(needed);
            free_argv_flag = TRUE;
        }
        o2_start_extract(msg);
        // double is big enough for any coerced value, so we'll pretend
        // all coerced values are doubles, even though some could be 
        coerced = (double *) (argv + argc);
        char *typ = types;
        char *desired_type = handler->type_string;
        int i = 0;
        for (typ = types; *typ; typ++) {
            if (*typ == *desired_type) {
                argv[i] = o2_get_next(*typ);
            } else { // if no type match, type will be coerced and may
                // be copied to o2_coerced_value. If this happens, we
                // must copy from this temporary value to allocated
                // storage pointed to by coerced.
                if ((argv[i] = o2_get_next(*desired_type) ==
                     &o2_coerced_value)) {
                    *coerced = o2_coerced_value.d;
                    argv[i] = (o2_arg_ptr) (coerced++);
                }
            }
            desired_type++;
        }
    }
    (*(handler->handler))(msg, types, argv, argc, handler->user_data);
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
void find_and_call_handlers_rec(char *remaining, char *name,
                                node_entry_ptr node, o2_message_ptr msg)
{
    char *slash = strchr(remaining, '/');
    if (slash) *slash = 0;
    int pattern = strpbrk(remaining, "*?[{");
    if (pattern) { // this is a pattern 
        enumerate enumerator;
        enumerate_begin(&enumerator, &(node->children));
        generic_entry_ptr entry;
        while ((entry = enumerate_next(&enumerator))) {
            if (slash && (entry->tag == PATTERN_NODE) &&
                (o2_pattern_match(entry->key, remaining) == O2_SUCCESS)) {
                find_and_call_handlers_rec(slash + 1, name,
                                           (node_entry_ptr) entry, msg);
            } else if (!slash && (entry->tag == PATTERN_HANDLER)) {
                char *path_end = remaining + strlen(remaining);
                path_end = WORD_ALIGN_PTR(path_end);
                call_handler((handler_entry_ptr) entry, msg, path_end + 5);
            }
        }
    } else { // no pattern characters so do hash lookup
        int index;
        string_pad(name, remaining, NAME_BUF_LEN);
        generic_entry_ptr *entry_ptr = lookup(node, name, &index);
        if (entry_ptr) {
            if (slash && ((*entry_ptr)->tag == PATTERN_NODE)) {
                find_and_call_handlers_rec(slash + 1, name,
                                           (node_entry_ptr) *entry_ptr, msg);
            } else if (!slash && ((*entry_ptr)->tag == PATTERN_HANDLER)) {
                char *path_end = remaining + strlen(remaining);
                path_end = WORD_ALIGN_PTR(path_end);
                call_handler((handler_entry_ptr) *entry_ptr, msg, path_end + 5);
            }
        }
    }
    if (slash) *slash = '/';
}


// to prevent deep recursion, messages go into a queue if we are already
// delivering a message via find_and_call_handlers:
static int in_find_and_call_handlers = FALSE;
static o2_message_ptr pending_head = NULL;
static o2_message_ptr pending_tail = NULL;

// dispatch msg to all matching handlers
//
void find_and_call_handlers(o2_message_ptr msg)
{
    if (in_find_and_call_handlers) { // enqueue the message and return
        if (pending_tail) {
            pending_tail->next = msg;
        } else {
            pending_head = pending_tail = msg;
        }
        return;
    }
    in_find_and_call_handlers = TRUE;
    char *address = msg->data.address;
    if ((address[0]) == '!') { // do full path lookup
        int index;
        address[0] = '/'; // must start with '/' to get consistent hash value
        generic_entry_ptr *handler = lookup(&master_table, address, &index);
        address[0] = '!'; // restore address for no particular reason
        if (handler && (*handler)->tag == PATTERN_HANDLER) {
            char *path_end = address;
            while (path_end[3]) path_end += 4; // find end of path
            call_handler((handler_entry_ptr) (*handler), msg, path_end + 5);
        }
    } else {
        char name[NAME_BUF_LEN];
        find_and_call_handlers_rec(address + 1, name, &path_tree_table, msg);
    }
    free_message(msg);
    in_find_and_call_handlers = FALSE;
    return;
}


void o2_deliver_pending()
{
    while (pending_head) {
        o2_message_ptr msg = pending_head;
        if (pending_head == pending_tail) {
            pending_head = pending_tail = NULL;
        } else {
            pending_head = pending_head->next;
        }
        find_and_call_handlers(msg);
    }
}
