/* pathtree.c -- implement hash tables */

/* Roger B. Dannenberg
 * April 2020
 */


#include <ctype.h>
#include <stdio.h>
#include "o2internal.h"
#include "pathtree.h"
#include "services.h"
#include "msgsend.h"

thread_local o2_context_ptr o2_context = NULL;

static int o2_pattern_match(const char *str, const char *p);
static int remove_method_from_tree(char *remaining, char *name,
                                   hash_node_ptr node);


// enumerate is used to visit all entries in a hash table
// it is used for:
// - enumerating services with status change when we become the
//       master clock process
// - enumerating all services offered by a given process when
//       that process's clock status changes
// - enumerating local services to send to another process
// - enumerating node entries in o2_node_show
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
        enumerator->entry = DA_GET(*(enumerator->dict), o2_node_ptr, i);
    }
    o2_node_ptr ret = enumerator->entry;
    enumerator->entry = enumerator->entry->next;
    return ret;
}

#ifndef O2_NO_DEBUG
static const char *entry_tags[5] = { 
    "NODE_HASH", "NODE_HANDLER", "NODE_SERVICES",
    "NODE_OSC_REMOTE_SERVICE", "NODE_BRIDGE_SERVICE" };


const char *o2_tag_to_string(int tag)
{
    if (tag >= NODE_HASH && tag <= NODE_BRIDGE_SERVICE)
        return entry_tags[tag - NODE_HASH];
    static char unknown[32];
    snprintf(unknown, 32, "Tag-%d", tag);
    return unknown;
}
#endif 



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
void o2_find_handlers_rec(char *remaining, char *name,
        o2_node_ptr node, o2_msg_data_ptr msg,
        char *types)
{
    char *slash = strchr(remaining, '/');
    if (slash) *slash = 0;
    char *pattern = strpbrk(remaining, "*?[{");
    if (slash) *slash = '/';
    if (pattern) { // this is a pattern 
        enumerate enumerator;
        o2_enumerate_begin(&enumerator, &(TO_HASH_NODE(node)->children));
        o2_node_ptr entry;
        while ((entry = o2_enumerate_next(&enumerator))) {
            if (slash && (entry->tag == NODE_HASH) &&
                (o2_pattern_match(entry->key, remaining) == O2_SUCCESS)) {
                o2_find_handlers_rec(slash + 1, name, entry, msg, types);
            } else if (!slash && (entry->tag == NODE_HANDLER)) {
                char *path_end = remaining + strlen(remaining);
                path_end = O2MEM_BIT32_ALIGN_PTR(path_end);
                o2_call_handler((handler_entry_ptr) entry, msg, path_end + 5);
            }
        }
    } else { // no pattern characters so do hash lookup
        if (slash) *slash = 0;
        o2_string_pad(name, remaining);
        if (slash) *slash = '/';
        o2_node_ptr entry = *o2_lookup(TO_HASH_NODE(node), name);
        if (entry) {
            if (slash && (entry->tag == NODE_HASH)) {
                o2_find_handlers_rec(slash + 1, name, entry, msg, types);
            } else if (!slash && (entry->tag == NODE_HANDLER)) {
                char *path_end = remaining + strlen(remaining);
                path_end = O2MEM_BIT32_ALIGN_PTR(path_end);
                o2_call_handler((handler_entry_ptr) entry, msg, path_end + 5);
            }
        }
    }
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
    // find the service offered by this process (o2_context->proc) --
    // the method should be attached to our local offering of the service
    o2_node_ptr *node_ptr = o2_proc_service_find(o2_context->proc, services);
    assert(node_ptr); // initially service is an empty HASH_NODE, must exist
    o2_node_ptr node = *node_ptr;
    assert(node);    // we must have a local offering of the service

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
    if (!slash) { // (cases 1 and 2: install new global handler)
        handler->key = NULL;
        handler->full_path = NULL;
        ret = o2_service_provider_replace(key + 1, node_ptr,
                                          (o2_node_ptr) handler);
        goto free_key_return; // do not need full path for global handler
    }

    // cases 3 and 4: path has nodes. If service is a NODE_HANDLER, 
    //   replace with NODE_HASH
    hash_node_ptr hnode = TO_HASH_NODE(node);
    if (hnode->tag == NODE_HANDLER) { // change global handler to an empty hash_node
        hnode = o2_hash_node_new(NULL); // top-level key is NULL
        if (!hnode) goto error_return_3;
        if ((ret = o2_service_provider_replace(key + 1, node_ptr,
                                               (o2_node_ptr) hnode))) {
            goto error_return_3;
        }
    }
    // now hnode is the root of a path tree for all paths for this service
    assert(slash);
    *slash = '/'; // restore the full path in key
    remaining = slash + 1;

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
        node = TO_HASH_NODE(*entry_ptr);
        remove_method_from_tree(slash + 1, name, node);
        if (node->num_children == 0) {
            // remove the empty table
            return o2_hash_entry_remove(node, entry_ptr, TRUE);
        }
        return O2_SUCCESS;
    }
    // now table is where we find the final path name with the handler
    // remaining points to the final segment of the path
    o2_string_pad(name, remaining);
    entry_ptr = o2_lookup(node, name);
    // there should be an entry, remove it
    if (*entry_ptr) {
        o2_hash_entry_remove(node, entry_ptr, TRUE);
        return O2_SUCCESS;
    }
    return O2_FAIL;
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
    // first, fill last 32-bit word with zeros so the final word will be zero-padded
    // round up len+1 to get the word after the end-of-string zero byte
    int32_t *end = (int32_t *)(dst + ROUNDUP_TO_32BIT(len + 1)); // round down to word boundary
    end[-1] = 0;
    // now copy the string; this may overwrite some zero-pad bytes:
    strncpy(dst, src, len);
}

void o2_handler_entry_finish(handler_entry_ptr handler)
{
    // if we remove a leaf node from the tree, remove the
    //  corresponding full path:
    if (handler->full_path) {
        o2_remove_hash_entry_by_name(&o2_context->full_path_table, handler->full_path);
        handler->full_path = NULL; // this string should be freed
            // in the previous call to remove_hash_entry_by_name(); remove the
            // pointer so if anyone tries to reference it, it will
            // generate a more obvious and immediate runtime error.
    }
    if (handler->type_string)
        O2_FREE((void *) handler->type_string);
    O2_FREE(handler->key);
}

#ifndef O2_NO_DEBUG
void o2_handler_entry_show(handler_entry_ptr handler)
{
    if (handler->full_path) {
        printf(" full_path=%s", handler->full_path);
    }
}
#endif
