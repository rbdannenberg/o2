//
//  o2_search.h
//  o2
//
//  Created by 弛张 on 3/14/16.
//
//

#ifndef o2_search_h
#define o2_search_h


#define PATTERN_NODE 0
#define PATTERN_HANDLER 1
#define SERVICES 2
#define O2_BRIDGE_SERVICE 3
#define OSC_REMOTE_SERVICE 4

/**
 *  Structures for hash look up.
 */

// The structure of an entry in hash table. Subclasses o2_info
// subclasses are node_entry, handler_entry, and services_entry
typedef struct o2_entry { // "subclass" of o2_info
    int tag;
    o2string key; // key is "owned" by this generic entry struct
    struct o2_entry *next;
} o2_entry, *o2_entry_ptr;


// Hash table's entry for node, another hash table
typedef struct node_entry { // "subclass" of o2_entry
    int tag; // must be PATTERN_NODE
    o2string key; // key is "owned" by this node_entry struct
    o2_entry_ptr next;
    int num_children;
    dyn_array children; // children is a dynamic array of o2_entry_ptr
    // a o2_entry_ptr can point to a node_entry, a handler_entry, a
    //   remote_service_entry, or an osc_entry (are there more?)
} node_entry, *node_entry_ptr;


// Hash table's entry for handler
typedef struct handler_entry { // "subclass" of o2_entry
    int tag; // must be PATTERN_HANDLER
    o2string key; // key is "owned" by this handler_entry struct
    o2_entry_ptr next;
    o2_method_handler handler;
    void *user_data;
    char *full_path; // this is the key for this entry in the o2_full_path_table
    // it is a copy of the key in the master table entry, so you should never
    // free this pointer -- it will be freed when the master table entry is
    // freed.
    o2string type_string; ///< types expected by handler, or NULL to ignore
    int types_len;     ///< the length of type_string
    int coerce_flag;   ///< boolean - coerce types to match type_string?
                       ///<   The message is not altered, but args will point
                       ///<   to copies of type-coerced data as needed
                       ///<   (coerce_flag is only set if parse_args is true.)
    int parse_args;    ///< boolean - send argc and argv to handler?
} handler_entry, *handler_entry_ptr;


typedef struct services_entry { // "subclass" of o2_entry
    int tag; // must be SERVICES
    o2string key; // key (service name) is "owned" by this struct
    o2_entry_ptr next;
    dyn_array services; // links to offers of this service. First in list
            // is the service to send to. Here "offers" means a node_entry
            // (local service), handler_entry (local service with just one
            // handler for all messages), process_info (for remote
            // service), osc_info (for service delegated to OSC server)
} services_entry, *services_entry_ptr;


/*
// Hash table's entry for a remote service
typedef struct remote_service_info {
    int tag;   // must be O2_REMOTE_SERVICE
    // char *key; // key is "owned" by this remote_service_entry struct
    // o2_entry_ptr next;
    process_info_ptr process;   // points to its host process for the service,
    // the remote service might be discovered but not connected
} remote_service_info, *remote_service_info_ptr;
*/

// Hash table entry for o2_osc_delegate: this service
//    is provided by an OSC server
typedef struct osc_info {
    int tag; // must be OSC_REMOTE_SERVICE
    o2string service_name;
    struct sockaddr_in udp_sa; // address for sending UDP messages
    int port;
    process_info_ptr tcp_socket_info; // points to "process" that has
    // info about the TCP socket to the OSC server, if NULL, then use UDP
} osc_info, *osc_info_ptr;


// Enumerate structure is hash table
typedef struct enumerate {
    dyn_array_ptr dict;
    int index;
    o2_entry_ptr entry;
} enumerate, *enumerate_ptr;


// typedef struct enumerate enumerate, *enumerate_ptr;
extern node_entry o2_path_tree;
extern node_entry o2_full_path_table;

#ifndef O2_NO_DEBUGGING
const char *o2_tag_to_string(int tag);
#define SEARCH_DEBUG
#ifdef SEARCH_DEBUG
void o2_info_show(o2_info_ptr info, int indent);
#endif
#endif

void o2_string_pad(char *dst, const char *src);

int o2_add_entry_at(node_entry_ptr node, o2_entry_ptr *loc,
                    o2_entry_ptr entry);

/**
 * add an entry to a hash table
 */
int o2_entry_add(node_entry_ptr node, o2_entry_ptr entry);


/**
 * make a new node
 */
node_entry_ptr o2_node_new();

int o2_service_provider_replace(process_info_ptr proc, const char *service_name,
                                o2_info_ptr new_service);


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
                         o2_info_ptr service);

void o2_node_finish(node_entry_ptr node);

o2string o2_heapify(const char *path);

/**
 *  When we want to initialize or want to create a table. We need to call this
 *  function.
 *
 *  @param locations The locations you want in the table.
 *
 *  @return O2_SUCCESS or O2_FAIL
 */
node_entry_ptr o2_node_initialize(node_entry_ptr node, const char *key);

/**
 *  Look up the key in certain table and return the pointer to the entry.
 *
 *  @param dict  The table that the entry is supposed to be in.
 *  @param key   The key.
 *  @param index The position in the hash table.
 *
 *  @return The address of the pointer to the entry.
 */
o2_entry_ptr *o2_lookup(node_entry_ptr dict, o2string key);

int o2_remove_remote_process(process_info_ptr info);

#endif /* o2_search_h */
