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

thread_local O2_context *o2_ctx = NULL;

#ifndef O2_NO_PATTERNS
static bool o2_pattern_match(const char *str, const char *p);
#endif
static O2err remove_method_from_tree(char *remaining, char *name,
                                        Hash_node *node);


#ifndef O2_NO_PATTERNS
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
// returns true if a message was delivered
//
bool o2_find_handlers_rec(char *remaining, char *name,
        O2node *node, O2msg_data_ptr msg,
        const char *types)
{
    char *slash = strchr(remaining, '/');
    if (slash) *slash = 0;
    char *pattern = strpbrk(remaining, "*?[{");
    if (slash) *slash = '/';
    bool delivered = false;
    if (pattern) { // this is a pattern
        Enumerate enumerator(TO_HASH_NODE(node));
        O2node *entry;
        while ((entry = enumerator.next())) {
            if (o2_pattern_match(entry->key, remaining)) {
                if (slash && ISA_HASH(entry)) {
                    delivered |= o2_find_handlers_rec(slash + 1, name, entry,
                                                      msg, types);
                } else if (!slash && ISA_HANDLER(entry)) {
                    ((Handler_entry *) entry)->invoke(msg, types);
                    delivered |= true;
                }
            }
        }
    } else { // no pattern characters so do hash lookup
        if (slash) *slash = 0;
        o2_string_pad(name, remaining);
        if (slash) *slash = '/';
        O2node *entry = *TO_HASH_NODE(node)->lookup(name);
        if (entry) {
            if (slash && ISA_HASH(entry)) {
                delivered = o2_find_handlers_rec(slash + 1, name,
                                                 entry, msg, types);
            } else if (!slash && ISA_HANDLER(entry)) {
                ((Handler_entry *) entry)->invoke(msg, o2_msg_data_types(msg));
                delivered = true; // either delivered or warning issued
            }
        }
    }
    return delivered;
}
#endif // O2_NO_PATTERNS

// insert whole path into flat table, insert path nodes into tree.
// If this path exists, then first remove all sub-tree paths.
//
// path is "owned" by caller (so it is copied here)
//
O2err o2_method_new_internal(const char *path, const char *typespec,
                                O2method_handler h, const void *user_data,
                                bool coerce, bool parse)
{
    // some variables that might not even be used are declared here
    // to avoid compiler warnings related to jumping over initializations
    // (this seems to be a GNU C++ compiler problem)
#ifndef O2_NO_PATTERNS
    Handler_entry *full_path_handler;
#endif
    O2string types_copy;
    Handler_entry *handler;
    O2node *node;
    Service_provider *spp;
    int types_len;
    
    // o2_heapify result is declared as const, but if we don't share it, there's
    // no reason we can't write into it, so this is a safe cast to (char *):
    char *key = (char *) o2_heapify(path);
    *key = '/'; // force key's first character to be '/', not '!'
    // add path elements as tree nodes -- to get the keys, replace each
    // "/" with EOS and o2_heapify to copy it, then restore the "/"
    char *remaining = key + 1;
#ifndef O2_NO_PATTERNS
    char name[NAME_BUF_LEN];
    Hash_node *new_node;
    Hash_node *tree_node;
#else
    O2node *new_node;
#endif
    char *slash = strchr(remaining, '/');
    if (slash) *slash = 0;
    Services_entry *services = *Services_entry::find(remaining);
    // note that slash has not been restored (see o2_service_replace below)
    // services now is the existing services_entry node if it exists.
    // slash points to end of the service name in the path.

    O2err ret = O2_NO_SERVICE;
    if (!services) goto free_key_return; // cleanup and return because it is
                       // an error to add a method to a non-existent service
    // find the service offered by this process (o2_ctx->proc) --
    // the method should be attached to our local offering of the service
    spp = services->proc_service_find(o2_ctx->proc);
    // if we have no service local, this fails with O2_NO_SERVICE
    if (!spp) goto free_key_return;

    node = spp->service;
    assert(node);    // we must have a local offering of the service

    types_copy = NULL;
    types_len = 0;
    if (typespec) {
        types_copy = o2_heapify(typespec);
        if (!types_copy) goto free_key_return;
        // coerce to int to avoid compiler warning -- this could overflow but
        // only in cases where it would be impossible to construct a message
        types_len = (int) strlen(typespec);
    }
    handler = new Handler_entry(NULL, h, user_data, key, types_copy,
                                types_len, coerce, parse);
    
    // case 1: method is global handler for entire service replacing a
    //         Hash_node with specific handlers: remove the O2TAG_HASH
    //         and insert a new Handler_entry as local service.
    // case 2: method is a global handler, replacing an existing global handler:
    //         same as case 1 so we can use o2_service_replace to clean up the
    //         old handler rather than duplicate that code.
    // case 3: method is a specific handler and a global handler exists:
    //         replace the global handler with a Hash_node and continue to
    //         case 4
    // case 4: method is a specific handler and a Hash_node exists as the
    //         local service: build the path in the tree according to the
    //         the remaining address string

    // slash here means path has nodes, e.g. /serv/foo vs. just /serv
    if (!slash) { // (cases 1 and 2: install new global handler)
        handler->full_path = NULL;
        ret = Services_entry::service_provider_replace(key + 1, &spp->service,
                                                       handler);
        goto free_key_return; // do not need full path for global handler
    }

    // cases 3 and 4: path has nodes. If service is a Handler_entry,
    //   replace with Hash_node
    if (ISA_HANDLER(node)) {
        // change global handler to a null node
#ifndef O2_NO_PATTERNS
        // change global handler to an empty hash_node
        new_node = new Hash_node(NULL); // top-level key is NULL
#else
        new_node = new O2node(NULL, O2TAG_EMPTY);  // placeholder that tells
        // message delivery to find the handler in the full_path_table
#endif
        if (!new_node) goto error_return_3;
        if ((ret = Services_entry::service_provider_replace(key + 1,
                                            &spp->service, new_node))) {
            goto error_return_3;
        }
        node = new_node;
    }
    // now node is the root of a path tree for all paths for this service
    assert(slash);
    *slash = '/'; // restore the full path in key
    remaining = slash + 1;
#ifndef O2_NO_PATTERNS
    // if we are installing a Handler entry as a leaf in the tree, node
    // must be a Hash_node, and we need a Hash_node to search the tree
    // for the proper insert point:
    tree_node = (Hash_node *) node;
    // support pattern matching by adding this path to the path tree
    while ((slash = strchr(remaining, '/'))) {
        *slash = 0; // terminate the string at the "/"
        o2_string_pad(name, remaining);
        *slash = '/'; // restore the string
        remaining = slash + 1;
        // if necessary, allocate a new entry for name
        tree_node = tree_node->tree_insert_node(name);
        assert(tree_node);
        o2_mem_check(tree_node);
        // tree_node is now the node for the path up to name
    }
    // node is now where we should put the final path name with the handler;
    // remaining points to the final segment of the path
    handler->key = o2_heapify(remaining);
    if ((ret = tree_node->insert(handler))) {
        goto error_return_3;
    }
    // make an entry for the full path table by copying handler:
    full_path_handler = new Handler_entry(handler);
    handler = full_path_handler;
#else // if O2_NO_PATTERNS:
    handler->key = handler->full_path;
    handler->full_path = NULL;
#endif
    // put the entry in the full path table
    ret = o2_ctx->full_path_table.insert(handler);
    goto just_return;
  error_return_3:
    if (types_copy) O2_FREE((void *) types_copy);
    O2_FREE(handler);
  free_key_return: // not necessarily an error (case 1 & 2)
    O2_FREE(key);
  just_return:
    return ret;
}


#ifndef NEGATE
#define NEGATE  '!'
#endif

#ifndef O2_NO_PATTERNS
/**
 * robust glob pattern matcher
 *
 *  @param str original string, a node name terminated by zero (eos)
 *  @param p   the string with pattern, p can be the remainder of a whole
 *             address pattern, so it is terminated by either zero (eos)
 *             or slash (/)
 *
 *  @return Iff match, return true.
 *
 * glob patterns:
 *  *   matches zero or more characters
 *  ?   matches any single character
 *  [set]   matches any character in the set
 *  [!set]  matches any character NOT in the set
 *      where a set is a group of characters or ranges. a range
 *      is written as two characters seperated with a hyphen: a-z denotes
 *      all characters between a to z inclusive.
 *  [set-]  matches any character in the set or a literal hypen
 *  {str1,str2,str3} matches any of str1, str2, or str3
 *
 *  char    matches itself except where char is '*' or '?'
 *  the characters space, #, *, comma, /, ?, [, ], { and } are not permitted
 *      in the string to be matched by a pattern, and thus literal characters
 *      in patterns do not include these characters
 *
 * examples:
 *  a*c     ac abc abbc ...
 *  a?c     acc abc aXc ...
 *  a[a-z]c     aac abc ... azc
 *  a[a-z-]c    aac abc ... azc a-c
 */
static bool o2_pattern_match(const char *str, const char *p)
{
    bool negate; // the ! is used before a character or range of characters
    bool match;  // a boolean used to exit a loop looking for a match
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
            return false;
        }
        
        // process the next character(s) of the pattern
        switch ((c = *p++)) {
            case '*': // matches 0 or more characters
                while (*p == '*') p++; // "*...*" is equivalent to "*" so
                                       // skip over a run of '*'s
                
                // if there are no more pattern characters, we can match
                //   '*' to the rest of str, so we have a match. This is an
                //   optimization that tests for a special case:
                if (!*p || *p == '/') return true;
                
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
                        return true;
                    }
                    str++;
                }
                return false;
                
            case '?': // matches exactly 1 character in str
                if (*str) break; // success
                return false;
            /*
             * set specification is inclusive, that is [a-z] is a, z and
             * everything in between. this means [z-a] may be interpreted
             * as a set that contains z, a and nothing in between.
             */
            case '[':
                negate = (*p == NEGATE);
                if (negate) {
                    p++; // skip over '!'
                }
                
                match = false; // no match found yet
                // search in set for a match until it is found
                // if/when you exit the loop, p is pointing to ']' or
                //   before it, or c == ']' and p points to the
                //   next character, or there is no matching ']'
                while (!match && (c = *p++)) {
                    if (c == ']') {
                        p--; // because we search forward for ']' below
                        break;
                    } else if (!*p || *p == '/') { // no matching ']' in pattern
                        return false;
                    } else if (*p == '-') {  // expected syntax is c-c
                        p++;
                        if (!*p || *p == '/')
                            return false; // expected to find at least ']'
                        if (*p != ']') {  // found end of range
                            match = (*str == c || *str == *p ||
                                     (*str > c && *str < *p));
                            p++;  // skip over end of range
                        } else {  //  c-] means ok to match c or '-'
                            match = (*str == c || *str == '-');
                        }
                    } else {   // no dash, so see if we match 'c'
                        match = (c == *str);
                    }
                }
                
                if (negate == match) { // tricky! 2 ways to pass, 2 ways to fail
                    return false; // either !negate and !match or negate and match
                }
                // if there is a match, skip past the cset and continue on
                while ((c = *p++) != ']') {
                    if (!c || c == '/') { // no matching ']' in pattern
                        return false;
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
                        return false;
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
                            return true;
                        } else {
                            str = place; // backtrack on test string
                            // continue testing, skipping the comma
                            p++;
                            if (!*p || *p == '/') { // unexpected end
                                return false;
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
                                return false; // no more choices, so no match
                            }
                        }
                    }
                    c = *p++;
                }
                break;
            }
                
            default:
                if (c != *str) {
                    return false;
                }
                break;
        }
        str++;
    }
    // since we have reached the end of the pattern, we match iff we are
    //   also at the end of the string:
    return (*str == 0);
}
#endif // O2_NO_PATTERNS

/* Remove an entry in the path tree. The full path table entry will be
 * removed as a side effect. If a parent node becomes empty, the
 * parent is removed. Thus we use a recursive algorithm so we can
 * examine parents after visiting the children.
 */
O2err o2_method_free(const char *path)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    if (!path || path[0] == 0 || path[1] == 0 || path[0] != '/') {
        return O2_BAD_NAME;
    }
    char name[NAME_BUF_LEN];
    o2_strcpy(name, path, NAME_BUF_LEN);
    // search path elements as tree nodes -- to get the keys, replace each
    // "/" with EOS and o2_heapify to copy it, then restore the "/"
    char *remaining = name + 1; // skip the initial "/"
    char *slash = strchr(remaining, '/');
    if (slash) *slash = 0;
    Services_entry *service = *Services_entry::find(remaining);
    if (slash) *slash = '/';
    if (!service) {
        return O2_NO_SERVICE;
    }
    Service_provider *spp = service->proc_service_find(o2_ctx->proc);
    if (ISA_HANDLER(spp->service) || !slash) {
        // all we have to do is replace the service with an empty Hash_entry
        delete spp->service;
        spp->service = new Hash_node(NULL);
        // note that there is no change in service, so no /_o2/si is sent
        return O2_SUCCESS;
    } else if (ISA_HASH(spp->service)) {
        char temp[NAME_BUF_LEN];
        return remove_method_from_tree(slash + 1, temp,
                                       (Hash_node *) spp->service);
    }
    return O2_FAIL;
}


// recursive function to remove path from tree. Follow links to the leaf
// node, remove it, then as the stack unwinds, remove empty nodes.
// remaining is the full path, which is manipulated to isolate node names.
// name is storage to copy and pad node names.
//
// returns O2_FAIL if path is not found in tree, even if nodes are
// removed. (This removes any handler encountered along the path.)
//
static O2err remove_method_from_tree(char *remaining, char *name,
                                        Hash_node *node)
{
    O2err rslt = O2_SUCCESS;
    char *slash = strchr(remaining, '/');
    O2node **entry_ptr; // another return value from o2_lookup
    if (slash) *slash = 0; // terminate the string at the "/"
    o2_string_pad(name, remaining);
    if (slash) *slash = '/'; // restore the string
    entry_ptr = node->lookup(name);
    if (!*entry_ptr) {
        rslt = O2_FAIL;
    } else if (!slash) {
        node->entry_remove(entry_ptr, true);
        rslt = O2_SUCCESS;
    } else if (ISA_HANDLER(*entry_ptr)) {
        rslt = O2_FAIL;  // did not find match to path
    } else {  // *entry_ptr addresses a HASH entry; recurse to subtree
        Hash_node *hn = TO_HASH_NODE(*entry_ptr);
        rslt = remove_method_from_tree(slash + 1, name, hn);
        if (hn->empty()) {
            // remove the empty table
            node->entry_remove(entry_ptr, true);
        }
    }
    return rslt;
}


// o2_string_pad -- copy src to dst, adding zero padding to word boundary
//
// dst MUST point to a buffer of size NAME_BUF_LEN or bigger
// src should be no longer than O2_MAX_NODE_NAME_LEN to avoid truncation
//
void o2_string_pad(char *dst, const char *src)
{
    size_t len = (strlen(src) + 4) & ~3;
    if (len >= NAME_BUF_LEN) {
        len = NAME_BUF_LEN;
    }
    // now copy the string; this also fills in zero pad bytes
    strncpy(dst, src, len);
    dst[NAME_BUF_LEN - 1] = 0; // finish padding and/or terminate string
}

void o2_handler_entry_finish(Handler_entry *handler)
{
    // if we remove a leaf node from the tree, remove the
    //  corresponding full path:
    if (handler->full_path) {
        o2_ctx->full_path_table.entry_remove_by_name(handler->full_path);
        handler->full_path = NULL; // this string should be freed
            // in the previous call to remove_hash_entry_by_name(); remove
            // the pointer so if anyone tries to reference it, it will
            // generate a more obvious and immediate runtime error.
    }
    if (handler->type_string)
        O2_FREE((void *) handler->type_string);
    if (handler->key) {        // key can be NULL if this is a global handler
        O2_FREE((char *) handler->key); // for everything in the service.
    }
}

#ifndef O2_NO_DEBUG
void o2_handler_entry_show(Handler_entry *handler)
{
    if (handler->full_path) {
        printf(" full_path=%s", handler->full_path);
    }
}
#endif
