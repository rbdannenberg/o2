/* o2node.h -- hash nodes and service providers base class */

/* Roger B. Dannenberg
 * Dec 2020
 */

#ifndef O2NODE_H
#define O2NODE_H

/* Most O2 objects are tagged so we can inspect types at runtime.
An alternative would be to have a virtual "what am I?" function,
or even a bunch of virtual functions so that everytime computation
depends on the type, you call a special virtual function to do it.
Although it's more "pure OOP" that way, it gets really hard to
read the code when special cases are scattered across multiple 
subclasses. We do a little of both in O2 implementation. Here 
are all the possible tag values:
*/

#define O2TAG_EMPTY 1  // just an O2node that redirects to full path table
#define O2TAG_HASH 2
#define O2TAG_HANDLER 4
#define O2TAG_SERVICES 8
#define O2TAG_PROC_TCP_SERVER 0x10
#define O2TAG_PROC_NOMSGYET 0x20
#define O2TAG_PROC 0x40
#define O2TAG_PROC_TEMP 0x80
#define O2TAG_MQTT 0x100
#define O2TAG_OSC_UDP_SERVER 0x200
#define O2TAG_OSC_TCP_SERVER 0x400
#define O2TAG_OSC_UDP_CLIENT 0x800
#define O2TAG_OSC_TCP_CLIENT 0x1000
#define O2TAG_OSC_TCP_CONNECTION 0x2000
#define O2TAG_BRIDGE 0x4000  // includes O2lite, O2sm
#define O2TAG_STUN 0x8000
#define O2TAG_TYPE_BITS ((O2TAG_STUN << 1) - 1)  // mask to get just the type

// The following (2) bits are used for properties. We could have implemented
// virtual methods to get each property or even stored the SYNCED property
// as a boolean, but this seems easier.
#define O2TAG_SYNCED 0x10000  // sync state of PROC, MQTT, BRIDGE
// Some objects are owned by the path_tree and must be deleted when removed
// from the tree. Other objects can be shared by multiple entries in the
// path_tree and are owned by an Fds_info object. Set the initial tag with or
// without this bit accordingly:
#define O2TAG_OWNED_BY_TREE 0x20000
#define O2TAG_HIGH O2TAG_OWNED_BY_TREE


#define ISA_HANDLER(x)  ((x)->tag & O2TAG_HANDLER)
#define ISA_HASH(x)     ((x)->tag & O2TAG_HASH)
#define ISA_SERVICES(x) ((x)->tag & O2TAG_SERVICES)
#define ISA_PROC(x)     ((x)->tag & O2TAG_PROC)
#define ISA_PROC_TCP_SERVER(x)     ((x)->tag & O2TAG_PROC_TCP_SERVER)
#define ISA_MQTT(x)     ((x)->tag & O2TAG_MQTT)
#define ISA_OSC_UDP_CLIENT(x)      ((x)->tag & O2TAG_OSC_UDP_CLIENT)
#define ISA_OSC_TCP_CLIENT(x)      ((x)->tag & O2TAG_OSC_TCP_CLIENT)
#define ISA_BRIDGE(x)   ((x)->tag & O2TAG_BRIDGE)


// ISA_PROXY tells us if the node, considered as a service provider,
// acts as a reference to (proxy for) another thread or
// process. I.e. do we forward the message to another location? OSC
// and BRIDE nodes are considered proxies.
#define ISA_PROXY(x) \
        ((x)->tag & (O2TAG_PROC | O2TAG_MQTT | O2TAG_OSC_UDP_CLIENT | \
                     O2TAG_OSC_TCP_CLIENT | O2TAG_BRIDGE))

// ISA_REMOTE_PROC tells us if the node represents an O2 process that
// is not the local process, e.g. the name is @pip:iip:port and the
// node is either a connected proc or a process known through the MQTT
// protocol:
#define ISA_REMOTE_PROC(x) ((x)->tag & (O2TAG_PROC | O2TAG_MQTT))

// HANDLER_IS_LOCAL tells us if the node represents a service handled
// directly by the local process, e.g. it is a handler (callback
// function), a hash table (a tree of path nodes with handlers at the
// leaves), or empty (which directs the message sender to a direct
// path lookup in o2_ctx->full_path_table:
#define HANDLER_IS_LOCAL(x) \
    ((x)->tag & (O2TAG_EMPTY | O2TAG_HASH | O2TAG_HANDLER))

// ISA_LOCAL_SERVICE tells us if the service is associated with the
// local process. This includes connections via osc, o2lite, shared
// memory and other bridge protocols because to remote processes, the
// services offered by all these service providers appear as services
// of the local process:
#define ISA_LOCAL_SERVICE(x) \
    ((x)->tag & (O2TAG_EMPTY | O2TAG_HASH | O2TAG_HANDLER | \
                 O2TAG_OSC_UDP_CLIENT | O2TAG_OSC_TCP_CLIENT | O2TAG_BRIDGE))


// Properties:
#define IS_SYNCED(x) ((x)->tag & O2TAG_SYNCED)


/* compute the size of a string including EOS and padding to next word */
int o2_strsize(const char *s);

void o2_string_pad(char *dst, const char *src);

O2string o2_heapify(const char *path);

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
// Any O2node can be an entry in a hash table, so hash tables
// can be used to form trees with named links, i.e. path trees
// for O2 address search.
class O2node : public O2obj {
  public:
    int tag;
    O2string key; // key is "owned" by this generic entry struct
    O2node *next;
    O2node(const char *key_, int tag_) {
        tag = tag_;
        key = (key_ ? o2_heapify(key_) : NULL);
        next = NULL;
    }
    virtual ~O2node() { if (key) O2_FREE(key); }
    
    // Get the process that offers this service. If not remote, it's
    // just _o2, so that's the default. Proc_info overrides this method:
    // if proc has a key, return it; if it is o2_ctx->proc, return "_o2"
    virtual const char *get_proc_name() { return "_o2"; }

    virtual O2status status(const char **process) {
        assert(HANDLER_IS_LOCAL(this));
        if (process) {
            *process = get_proc_name();
        }
        return o2_clock_is_synchronized ? O2_LOCAL : O2_LOCAL_NOTIME;
    }

#ifndef O2_NO_DEBUG
    virtual void show(int indent);
#endif
};


// Hash table node, another hash table
class Hash_node : public O2node { // "subclass" of o2_node
    friend class Enumerate;
    int num_children;
    Vec<O2node *> children; // children is a dynamic array of o2_node_ptr.
  public:
    // The key (name) of this entry. key is owned by the caller and
    //     a copy is made and owned by the node.
    Hash_node(const char *key_) :
            O2node(key_, O2TAG_HASH | O2TAG_OWNED_BY_TREE) {
        num_children = 0;
        table_init(2);
    }
    // avoid any memory allocation if no parameters:
    Hash_node() : O2node(NULL, O2TAG_HASH) {
        num_children = 0;
        table_init(0);
    }
    void finish();
    virtual ~Hash_node() { finish(); }

    bool empty() { return num_children == 0; }
#ifndef O2_NO_DEBUG
    void show(int indent);
#endif
    O2node **lookup(O2string key);

    // At the top level, all children are services_entry_ptrs (tag
    // O2TAG_SERVICES). Below that level children can have tags O2TAG_HASH
    // or O2TAG_HANDLER.
    O2err entry_insert_at(O2node **loc,  O2node *entry);
    /// add an entry to a hash table
    O2err insert(O2node *entry);
    Hash_node *tree_insert_node(O2string key);
    O2err entry_remove_by_name(O2string key);
    O2err entry_remove(O2node **child, bool resize);
  protected:
    void table_init(int locations) {
        children.init(locations, true);
        children.append_space(children.get_allocated());
    }
    O2err table_resize(int new_locs);
};

#ifdef O2_NO_DEBUG
#define TO_HASH_NODE(node) ((Hash_node *) node)
#else
#define TO_HASH_NODE(node) (assert(ISA_HASH(node)), \
                            (Hash_node *) (node))
#endif

// enumerate is used to visit all entries in a hash table
// it is used for:
// - enumerating services with status change when we become the
//       reference clock process
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
//
// To enumerate elements of a hash table (at one level), use this
// structure and see o2_enumerate_begin(), o2_enumerate_next()
class Enumerate {
    Vec<O2node *> *dict;
    int index;
    O2node *entry;
  public:
    Enumerate(Hash_node *hn) { dict = &hn->children; index = 0; entry = NULL; }
    Enumerate(Vec<O2node *> *vec) { dict = vec; index = 0; entry = NULL; }
    O2node *next();
};


// Hash table's entry for handler
class Handler_entry : public O2node {  // "subclass" of o2_node
public:
    O2method_handler handler;
    const void *user_data;
    O2string full_path; // this is the key for this entry in the
    // o2_ctx->full_path_table; it is a copy of the key in the

    // path_tree table entry, so you should never free this pointer
    // -- it will be freed when the path_tree table entry is freed.
    // (Exception: if O2_NO_PATTERNS, there is no path_tree.)
    O2string type_string; ///< types expected by handler, or NULL to ignore
    int types_len;     ///< the length of type_string
    int coerce_flag;   ///< boolean - coerce types to match type_string?
                       ///<   The message is not altered, but args will point
                       ///<   to copies of type-coerced data as needed
                       ///<   (coerce_flag is only set if parse_args is true.)
    int parse_args;    ///< boolean - send argc and argv to handler?
  public:
    Handler_entry(const char *key, O2method_handler h, const void *user_data_,
                  O2string full_path_, O2string type_string_, int types_len_,
                  bool coerce_flag_, bool parse_args_) :
            O2node(key, O2TAG_HANDLER | O2TAG_OWNED_BY_TREE) {
        handler = h; user_data = user_data_; full_path = full_path_;
        type_string = type_string_; types_len = types_len_;
        coerce_flag = coerce_flag_; parse_args = parse_args_;
    }
    // copies everything except full_path, which is set to NULL, also
    //    makes a full copy of type_string if any.
    Handler_entry(Handler_entry *src) : O2node(src->full_path, O2TAG_HANDLER) {
        handler = src->handler; user_data = src->user_data;
        full_path = NULL; type_string = src->type_string;
        if (type_string) type_string = o2_heapify(type_string);
        types_len = src->types_len; coerce_flag = src->coerce_flag;
        parse_args = src->parse_args;
    }
    virtual ~Handler_entry();
    void invoke(o2_msg_data_ptr msg, const char *types);
#ifndef O2_NO_DEBUG
    void show(int indent);
#endif
};


// A message handler that uses a socket or other connection to
// deliver messages remotely
class Proxy_info : public O2node, public Net_interface {
public:
    Proxy_info(const char *key, int tag) : O2node(key, tag) { fds_info = NULL; }
    // remove is only called from ~Fds_info(): it's purpose is to delete
    // a Proxy without a recursive deletion of Fds_info that it links to.
    virtual void remove() { fds_info = NULL; delete this; }

    void delete_fds_info() {
        if (fds_info) {
            fds_info->owner = NULL;  // remove socket-to-node_info pointer
            fds_info->close_socket();  // shut down the connection
        }
    }

    // Tell this proxy that the local process is synchronized with the
    // global clock (it may even be the *source* of the global clock).
    // Not all proxies care, e.g. Stun_info is a subclass of Proxy_info,
    // but it does not handle O2 messages or care about clock sync.
    // If the Proxy represents a process that has clock sync
    // status, e.g. O2_REMOTE vs O2_REMOTE_NOTIME or O2_BRIDGE vs
    // O2_BRIDGE_NOTIME, and the remote process is synchronized, e.g.
    // PROC_SYNC instead of PROC_NOCLOCK, then return true
    // so that the change can be reported to the application via /_o2/si.
    // Note that if the clock is a 3rd party, the remote process could be
    // synchronized before we are, so it becomes "synchronized with us"
    // when we go from LOCAL_NOTIME to LOCAL, i.e. globally synchronized.
    virtual bool local_is_synchronized() { return false; }

    // Proxy tells whether to send messages ahead of time or to schedule
    // them locally and send according to timestamp. Return false if the
    // destination process has a scheduler to handle timestamped
    // messages. Note that OSC schedules bundles but not regular messages,
    // so the result depends on the message. The callee can assume there is
    // a pending message that can be accessed by o2_current_message().
    virtual bool schedule_before_send() { return false; };
    virtual O2err send(bool block) {
        o2_drop_message("Proxy::send called by mistake", true);
        return O2_FAIL; }
    virtual O2err deliver(o2n_message_ptr msg);
    // returns message to send, or null if O2_NO_SERVICE:
    O2message_ptr pre_send(int *tcp_flag);

};

extern Proxy_info *o2_message_source;


#ifdef O2_NO_DEBUG
#define TO_HANDLER_ENTRY((node) ((Handler_entry *) node)
#else
#define TO_HANDLER_ENTRY(node) (assert(ISA_HANDLER(node)), \
                                (Handler_entry *) node)
#endif

#ifndef O2_NO_DEBUG
void o2_fds_info_debug_predelete(Fds_info *info);
#endif

#endif /* HASHNODE_H */

