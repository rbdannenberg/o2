/* hashnode.h -- hash node implementation */

/* Roger B. Dannenberg
 * April 2020
 */

#ifndef HASHNODE_H
#define HASHNODE_H

#ifdef __cplusplus
extern "C" {
#endif

#define NODE_HASH 10         // tag for hash_node
#define NODE_HANDLER 11      // tag for handler_entry
#define NODE_SERVICES 12     // tag for services_entry
#define NODE_EMPTY 13        // tag that redirects to full path table
// see also the tag values in o2_net.h for o2n_info_ptr's


/**
 *  Structures for hash look up.
 */

// The structure of an entry in hash table. o2_node
// subclasses are:
//     hash_node, 
//     handler_entry, 
//     services_entry,
//     osc_info,
//     bridge_inst
// Any o2_node can be an entry in a hash table, so hash tables
// can be used to form trees with named links, i.e. path trees
// for O2 address search.
typedef struct o2_node {
    int tag;
    o2string key; // key is "owned" by this generic entry struct
    struct o2_node *next;
} o2_node, *o2_node_ptr;


// Hash table node, another hash table
typedef struct hash_node { // "subclass" of o2_node
    int tag; // must be NODE_HASH
    o2string key; // key is "owned" by this hash_node struct
    o2_node_ptr next;
    int num_children;
    dyn_array children; // children is a dynamic array of o2_node_ptr.
    // At the top level, all children are services_entry_ptrs (tag
    // NODE_SERVICES). Below that level children can have tags NODE_HASH
    // or NODE_HANDLER.
} hash_node, *hash_node_ptr;

#ifdef O2_NO_DEBUG
#define TO_HASH_NODE(node) ((hash_node_ptr) node)
#else
#define TO_HASH_NODE(node) (assert((node)->tag == NODE_HASH), \
                            (hash_node_ptr) (node))
#endif

// To enumerate elements of a hash table (at one level), use this
// structure and see o2_enumerate_begin(), o2_enumerate_next()
typedef struct enumerate {
    dyn_array_ptr dict;
    int index;
    o2_node_ptr entry;
} enumerate, *enumerate_ptr;


// Hash table's entry for handler
typedef struct handler_entry { // "subclass" of o2_node
    int tag; // must be NODE_HANDLER
    o2string key; // key is "owned" by this handler_entry struct
    o2_node_ptr next;
    o2_method_handler handler;
    const void *user_data;
    char *full_path; // this is the key for this entry in the
    // o2_ctx->full_path_table; it is a copy of the key in the
    // path_tree table entry, so you should never free this pointer
    // -- it will be freed when the path_tree table entry is freed.
    // (Exception: if O2_NO_PATTERNS, there is no path_tree.)
    o2string type_string; ///< types expected by handler, or NULL to ignore
    int types_len;     ///< the length of type_string
    int coerce_flag;   ///< boolean - coerce types to match type_string?
                       ///<   The message is not altered, but args will point
                       ///<   to copies of type-coerced data as needed
                       ///<   (coerce_flag is only set if parse_args is true.)
    int parse_args;    ///< boolean - send argc and argv to handler?
} handler_entry, *handler_entry_ptr;

#ifdef O2_NO_DEBUG
#define TO_HANDLER_ENTRY((node) ((handler_entry_ptr) node)
#else
#define TO_HANDLER_ENTRY(node) (assert(node->tag == NODE_HANDLER), \
                               (handler_entry_ptr) node)
#endif


void o2_enumerate_begin(enumerate_ptr enumerator, dyn_array_ptr dict);


o2_node_ptr o2_enumerate_next(enumerate_ptr enumerator);


#ifndef O2_NO_DEBUG
void o2_node_show(o2_node_ptr info, int indent);
#endif


/* compute the size of a string including EOS and padding to next word */
int o2_strsize(const char *s);

void o2_string_pad(char *dst, const char *src);

o2_err_t o2_add_entry_at(hash_node_ptr node, o2_node_ptr *loc,
                         o2_node_ptr entry);

/**
 * add an entry to a hash table
 */
o2_err_t o2_node_add(hash_node_ptr node, o2_node_ptr entry);

/**
 * free a NODE_HASH, NODE_HANDLER, or NODE_SERVICES
 */
void o2_node_free(o2_node_ptr entry);

/**
 * make a new node
 */
hash_node_ptr o2_hash_node_new(const char *key);

hash_node_ptr o2_tree_insert_node(hash_node_ptr node, o2string key);

o2_err_t o2_embedded_msgs_deliver(o2_msg_data_ptr msg);

int o2_remove_hash_entry_by_name(hash_node_ptr node, o2string key);

o2_err_t o2_hash_entry_remove(hash_node_ptr node, o2_node_ptr *child,
                              int resize);

void o2_hash_node_finish(hash_node_ptr node);

o2string o2_heapify(const char *path);

/**
 *  Initialize a table entry.
 *
 *  @param node table entry to be initialized.
 *  @param key The key (name) of this entry. key is owned by the caller and
 *         a copy is made and owned by the node.
 *
 *  @return O2_SUCCESS or O2_FAIL
 */
hash_node_ptr o2_node_initialize(hash_node_ptr node, const char *key);

/**
 *  Look up the key in certain table and return the pointer to the entry.
 *
 *  @param dict  The table that the entry is supposed to be in.
 *  @param key   The key.
 *
 *  @return The address of the pointer to the entry.
 */
o2_node_ptr *o2_lookup(hash_node_ptr dict, o2string key);

/* Since hash entries can be many things, we delegate the cleanup
 * to higher levels of abstraction. Think of this as subclasses 
 * overriding the finish method to do the cleanup. Declare them 
 * here in hashnode.h, but implement them in pathtree.c, services.c.
 */
void o2_handler_entry_finish(handler_entry_ptr handler);
#ifndef O2_NO_DEBUG
void o2_handler_entry_show(handler_entry_ptr handler);
#endif


#ifdef __cplusplus
}
#endif

#endif /* HASHNODE_H */

