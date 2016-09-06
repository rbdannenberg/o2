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
#define OSC_LOCAL_SERVICE 6 // TODO: is this used?

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
    int argc;          ///< number of expected arguments
    int coerce_flag;   ///< boolean - coerce types to match type_string?
                       ///<   The message is not altered, but args will point
                       ///<   to copies of type-coerced data as needed
                       ///<   (coerce_flag is only set if parse_args is true.)
    int parse_args;    ///< boolean - send argc and argv to handler?
} handler_entry, *handler_entry_ptr;


/* process_info status values */
#define PROCESS_DISCOVERED 1  // process created from discovery message
#define PROCESS_CONNECTING 2  // we called connect to this process
#define PROCESS_CONNECTED 3   // connect call returned
#define PROCESS_NO_CLOCK 4    // process initial message received, not clock synced
#define PROCESS_OK 5          // process is clock synced

// used for both remote and local process
typedef struct process_info {
    char *name; // e.g. "128.2.1.100:55765", this is used so that when we
        // add a service, we can enumerate all the processes and send them
        // updates. Updates are addressed using this name field. Also, when
        // a new process is connected, we send an /in message to this name.
        // name is "owned" by the process_info struct and will be deleted
        //   when the struct is freed
    int status; // PROCESS_DISCOVERED through PROCESS_OK
    dyn_array services; // these are the keys of remote_service_entry objects,
                        //    owned by the service entries (do not free)
    int little_endian;  // true if the host is little-endian
    // port numbers are here so that in discovery, we can check for any changes
    int udp_port;       // current udp port number
    struct sockaddr_in udp_sa;  // address for sending UDP messages
    int tcp_fd_index;   // index in o2_fds of tcp socket
} process_info, *process_info_ptr;


// Hash table's entry for a remote service
typedef struct remote_service_entry {
    int tag;   // must be O2_REMOTE_SERVICE
    char *key; // key is "owned" by this remote_service_entry struct
    generic_entry_ptr next;
    process_info_ptr parent;   // points to its host process for the service
} remote_service_entry, *remote_service_entry_ptr;


// Hash table entry for o2_delegate_to_osc: this service
//    is provided by an OSC server
typedef struct osc_entry {
    int tag; // must be OSC_REMOTE_SERVICE
    char *key; // key is "owned" by this osc_entry struct
    generic_entry_ptr next;
    struct sockaddr_in udp_sa; // address for sending UDP messages
    char ip[20];
    int port;
    SOCKET tcp_socket; // socket connection for sending TCP messages
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

/** copy a string into the heap  */
char *o2_heapify(const char *path);

/**
 * robust glob pattern matcher
 *
 *  @param str oringinal string
 *  @param p   the string with pattern
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
int o2_pattern_match(const char *str, const char *p);

/**
 *  When we want to initialize or want to create a table. We need to call this
 *  function.
 *
 *  @param locations The locations you want in the table.
 *
 *  @return O2_SUCCESS or O2_FAIL
 */
node_entry_ptr initialize_node(node_entry_ptr node, char *key);

/**
 *  Look up the key in certain table and return the pointer to the entry.
 *
 *  @param dict  The table that the entry is supposed to be in.
 *  @param key   The key.
 *  @param index The position in the hash table.
 *
 *  @return The pointer to the entry.
 */
generic_entry_ptr *lookup(node_entry_ptr dict, const char *key, int *index);


void o2_init_process(process_info_ptr process, int status, int is_little_endian);
      
process_info_ptr o2_add_remote_process(const char *ip_port, int status,
                                     int is_little_endian);

int o2_remove_remote_process(process_info_ptr proc);


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
int add_remote_service(process_info_ptr process, const char *service);


node_entry_ptr tree_insert_node(node_entry_ptr node, char *key);


void free_node(node_entry_ptr node);

void free_entry(generic_entry_ptr entry);


int add_local_osc(const char *path, int port, int sid);

/**
 *  When the method is no longer exist, or the method conflict with a new one.
 *  we need to call this function to delete the old one.
 *
 *  @param path The path of the method
 *
 *  @return If succeed, return O2_SUCCESS. If not, return O2_FAIL.
 */
int o2_remove_method(const char *path);

/**
 *  When we get a new message and want to dispatch it to certain method handlers.
 *  We need to use this function.
 *  Note: as the path is included in the message, we don't need to pass in the path.
 *
 *  @param msg The message structure.
 */
void find_and_call_handlers(o2_message_ptr msg);

void o2_deliver_pending();

int dispatch_osc_message(void *msg);

int remove_node(node_entry_ptr dict, const char *key);

#endif /* o2_search_h */
