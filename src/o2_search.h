//
//  o2_search.h
//  o2
//
//  Created by 弛张 on 3/14/16.
//
//

#ifndef o2_search_h
#define o2_search_h


/* IMPORTANT: If these change, fix tag_to_status */
#define PATTERN_NODE 0
#define PATTERN_HANDLER 1
#define O2_REMOTE_SERVICE 2
#define O2_BRIDGE_SERVICE 3
#define OSC_REMOTE_SERVICE 4
#define O2_PROCESS 5
#define index strchr
/**
 *  Structures for hash look up.
 */
// The structure of generic entry in hash table
typedef struct generic_entry {
    int tag;
    char *key; // key is "owned" by this generic entry struct
    struct generic_entry *next;
} generic_entry, *generic_entry_ptr;

// Hash table's entry for node
typedef struct node_entry {
    int tag; // must be PATTERN_NODE
    char *key; // key is "owned" by this node_entry struct
    generic_entry_ptr next;
    int num_children;
    dyn_array children; // children is a dynamic array of generic_entry_ptr
    // a generic_entry_ptr can point to a node_entry, a handler_entry, a
    //   remote_service_entry, or an osc_entry (are there more?)
} node_entry, *node_entry_ptr;

// Hash table's entry for handler
typedef struct handler_entry {
    int tag; // must be PATTERN_HANDLER
    char *key; // key is "owned" by this handler_entry struct
    generic_entry_ptr next;
    o2_method_handler handler;
    void *user_data;
    char *full_path; // this is the key for this entry in the master_table
    // it is a copy of the key in the master table entry, so you should never
    // free this pointer -- it will be freed when the master table entry is
    // freed.
    char *type_string; ///< types expected by handler, or NULL to ignore
    int types_len;     ///< the length of type_string
    int coerce_flag;   ///< boolean - coerce types to match type_string?
                       ///<   The message is not altered, but args will point
                       ///<   to copies of type-coerced data as needed
                       ///<   (coerce_flag is only set if parse_args is true.)
    int parse_args;    ///< boolean - send argc and argv to handler?
} handler_entry, *handler_entry_ptr;


// Hash table's entry for a remote service
typedef struct remote_service_entry {
    int tag;   // must be O2_REMOTE_SERVICE
    char *key; // key is "owned" by this remote_service_entry struct
    generic_entry_ptr next;
    int process_index;   // points to its host process for the service,
    // -1 indicates the remote service is discovered but not connected
} remote_service_entry, *remote_service_entry_ptr;


// Hash table entry for o2_osc_delegate: this service
//    is provided by an OSC server
typedef struct osc_entry {
    int tag; // must be OSC_REMOTE_SERVICE
    char *key; // key is "owned" by this osc_entry struct
    generic_entry_ptr next;
    struct sockaddr_in udp_sa; // address for sending UDP messages
    char ip[20];
    int port;
    int fds_index; // points to fds and fds_info entries for TCP socket
    // otherwise the value is -1 indicating UDP
} osc_entry, *osc_entry_ptr;


/*
// Hash table to implements path pattern matching and lookup
typedef struct dict {
    int num_entries;
    dyn_array darray;
} dict, *dict_ptr;
*/

// Enumerate structure is hash table
typedef struct enumerate {
    dyn_array_ptr dict;
    int index;
    generic_entry_ptr entry;
} enumerate, *enumerate_ptr;


// typedef struct enumerate enumerate, *enumerate_ptr;
extern node_entry path_tree_table;
extern node_entry master_table;

/**
 * add an entry to a hash table
 */
int o2_entry_add(node_entry_ptr node, generic_entry_ptr entry);


/**
 *  When we use the discover function or manual add in a remote service. We need to
 *  add the path and the remote process number into the hash table. So that
 *  when we send messages to it, we can use the hash table to find the number.
 *  Then we use the remote_process[n] to get certain informatinos.
 *
 *  @param path The path of remote process(service name).
 *  @param num  The remote process number.
 *
 *  @return If succeed, return O2_SUCCESS. If not, return O2_FAIL.
 */
int o2_remote_service_add(fds_info_ptr info, const char *service);


remote_service_entry_ptr o2_remote_process_new_at(
        const char *name, generic_entry_ptr *entry_ptr, fds_info_ptr info);


int o2_remote_service_remove(const char *service);


int o2_embedded_msgs_deliver(o2_msg_data_ptr msg, int tcp_flag);

/**
 * Deliver a message immediately and locally. If service is given,
 * the caller must check to see if this is a bundle and call 
 * deliver_embedded_msgs() if so.
 *
 * @param msg the message data to deliver
 * @param tcp_flag tells whether to send via tcp; this is only used
 *                 when the message is a bundle and elements of the
 *                 bundle are addressed to remote services
 * @param service is the service to which the message is addressed.
 *                If the service is unknown, pass NULL.
 */
void o2_msg_data_deliver(o2_msg_data_ptr msg, int tcp_flag,
                         generic_entry_ptr service);

void o2_node_finish(node_entry_ptr node);

char *o2_heapify(const char *path);

/**
 *  When we want to initialize or want to create a table. We need to call this
 *  function.
 *
 *  @param locations The locations you want in the table.
 *
 *  @return O2_SUCCESS or O2_FAIL
 */
node_entry_ptr o2_node_initialize(node_entry_ptr node, char *key);

/**
 *  Look up the key in certain table and return the pointer to the entry.
 *
 *  @param dict  The table that the entry is supposed to be in.
 *  @param key   The key.
 *  @param index The position in the hash table.
 *
 *  @return The address of the pointer to the entry.
 */
generic_entry_ptr *o2_lookup(node_entry_ptr dict, const char *key, int *index);

int o2_remove_remote_process(fds_info_ptr info);

node_entry_ptr o2_tree_insert_node(node_entry_ptr node, char *key);


#endif /* o2_search_h */
