/* o2node.cpp -- implement node base class and hash tables */

/* Roger B. Dannenberg
 * April 2020
 */

#include "o2internal.h"
#include "services.h"
#include "message.h"
#include "msgsend.h"
#include "o2osc.h"

Proxy_info *o2_message_source = NULL;

#if IS_LITTLE_ENDIAN
// for little endian machines
#define STRING_EOS_MASK 0xFF000000
#define INT32_MASK0 0x000000FF
#define INT32_MASK1 0x0000FF00
#define INT32_MASK2 0x00FF0000
#define INT32_MASK3 0xFF000000
#else
#define STRING_EOS_MASK 0x000000FF
#define INT32_MASK0 0xFF000000
#define INT32_MASK1 0x00FF0000
#define INT32_MASK2 0x0000FF00
#define INT32_MASK3 0x000000FF
#endif
#define SCRAMBLE 2686453351680

#define MAX_SERVICE_NUM  1024


int o2_strsize(const char *s)
{
    // coerce to int to avoid compiler warning, O2 messages can't be that long
    return (int) ((strlen(s) + 4) & ~3);
}

#ifndef O2_NO_DEBUG
void Hash_node::show(int indent)
{
    O2node::show(indent);
    if (num_children == 0) printf(" (hash table is empty)");
    printf("\n");
    Enumerate en(this);
    O2node *entry;
    while ((entry = en.next())) {
        entry->show(indent + 1);
        // data integrity check -- see if node names hash to nodes
        O2node **entry_ptr = lookup(entry->key);
        assert(*entry_ptr == entry);
    }
}
#endif


// insert an entry into the hash table. If the table becomes
// too full, a new larger table is created.
//
O2err Hash_node::insert(O2node *entry)
{
    O2node **ptr = lookup(entry->key);
    if (*ptr) { // if we found it, this is a replacement
        // splice out existing entry and delete it
        entry_remove(ptr, false);
    }
    return entry_insert_at(ptr, entry);
}


O2err Hash_node::table_resize(int new_locs)
{
    Vec<O2node *> old(children); // copy whole dynamic array
    Enumerate enumerator(&old);
    // now, old array is in old, children is newly allocated
    // copy all entries from old to children
    table_init(new_locs);
    O2node *entry;
    while ((entry = enumerator.next())) {
        insert(entry);
    }
    // now we have moved all entries into the new table, old one will be
    // freed by destructor when we return
    return O2_SUCCESS;
}


// node_free - when an entry is inserted into a table, it
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
// an entry is in the o2_ctx->path_tree by looking at the
// .full_path field.
//
// The parameter should be an entry to remove -- either an
// internal entry (O2TAG_HASH) or a leaf entry (O2TAG_HANDLER)
//
void Hash_node::finish()
{
    for (int i = 0; i < children.size(); i++) {
        O2node *e = children[i];
        while (e) {
            O2node *next = e->next;
            e->o2_delete();
            e = next;
        }
    }
    children.finish();
    // key is freed by ~O2node (the superclass deconstructor)
}


// return next entry from table. Entries can be inserted into
// a new table because enumerate_next does not depend upon the
// pointers in each entry once the entry is enumerated.
//
O2node *Enumerate::next()
{
    while (!entry) {  // scan for next entry list in dictionary's array
        int i = index++;
        if (i >= dict->size()) {
            return NULL; // no more entries
        }
        entry = (*dict)[i];
    }
    O2node *ret = entry;  // we have or found an entry to return
    entry = entry->next;  // next time, we return the next element of the bucket
    return ret;
}
   


Handler_entry::~Handler_entry()
{
    // if we remove a leaf node from the tree, remove the
    //  corresponding full path:
    if (full_path) {
        o2_ctx->full_path_table.entry_remove_by_name(full_path);
        // Maybe full_path_table entries could use full_path for
        // their keys -- then we would not need 2 copies of
        // full_path (one here, one in the full_path tree). O2
        // used to work this way, so maybe making the 2nd copy
        // was added unintentionally.
        O2_FREE((char *) full_path);
        full_path = NULL; // remove the pointer to aid with debugging
    }
    if (type_string) O2_FREE((char *) type_string);
}

#ifndef O2_NO_DEBUG
void Handler_entry::show(int indent)
{
    O2node::show(indent);
    if (key) printf(" key=%s", key);
    if (full_path) printf(" full_path=%s", full_path);
    printf("\n");
}
#endif


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
void Handler_entry::invoke(O2msg_data_ptr msg, const char *types)
{
    // coerce to avoid compiler warning -- even 2^31 is absurdly big
    //     for the type string length
    int types_len = (int) strlen(types);

    // type checking
    if (type_string && // mismatch detection needs type_string
        ((this->types_len != types_len) || // first check if counts are equal
         !(coerce_flag ||  // need coercion or exact match
           (streql(type_string, types))))) {
        o2_drop_msg_data("of type mismatch", msg);
        return;
    }

    if (parse_args) {
        o2_extract_start(msg);
        O2string typ = type_string;
        if (!typ) { // if handler type_string is NULL, use message types
            typ = types;
        }
        while (*typ) {
            O2arg_ptr next = o2_get_next((O2type) (*typ++));
            if (!next) {
                o2_drop_msg_data("of type coercion failure", msg);
                return;
            }
        }
        if (type_string) {
            types = type_string; // so that handler gets coerced types
        }
    } else {
        o2_ctx->argv_data.clear();
    }
    (*handler)(msg, types, &o2_ctx->argv_data[0], o2_ctx->argv_data.size(),
               user_data);
}


#ifndef O2_NO_DEBUG
// debugging code to print o2_node and o2_info structures
void O2node::show(int indent)
{
    for (int i = 0; i < indent; i++) printf("  ");
    printf("%s@%p %s", o2_tag_to_string(tag), this, key);
}
#endif


// The hash function processes 4 bytes at a time and is based
// on the idea (and I think this is what Java uses) of repeatedly
// multiplying the hash by 5 and adding the next character until
// all characters are used. The SCRAMBLE number is (5 << 8) +
// ((5 * 5) << 16 + ..., so it is similar to doing the multiplies
// and adds all in parallel for 4 bytes at a time.
//
// In O2, O2string means "const char *" with zero padding to a
// 32-bit word boundary.
//
static int64_t get_hash(O2string key)
{
    int32_t *ikey = (int32_t *) key;
    uint64_t hash = 0;
    int32_t c;
    do {
        c = *ikey++;
        // in c, each zero must be followed by zero
        assert((((c & INT32_MASK0) != 0) || ((c & INT32_MASK1) == 0)) &&
	       (((c & INT32_MASK1) != 0) || ((c & INT32_MASK2) == 0)) &&
	       (((c & INT32_MASK2) != 0) || ((c & INT32_MASK3) == 0)));
        hash = ((hash + c) * SCRAMBLE) >> 32;
    } while (c & STRING_EOS_MASK);
    return hash;
}


// copy a string to the heap, result is 32-bit word aligned, has
//   at least one zero end-of-string byte and is
//   zero-padded to the next word boundary
O2string o2_heapify(const char *path)
{
    long len = o2_strsize(path);
    char *rslt = O2_MALLOCNT(len, char);
    strncpy(rslt, path, len); // zero fills
#if O2MEM_DEBUG > 1
    printf("    o2_heapify rslt: %p:%s\n", rslt, rslt);
#endif
    return rslt;
}


// o2_lookup returns a pointer to a pointer to the entry, if any.
// The hash table uses linked lists for collisions to make
// deletion simple. key must be aligned on a 32-bit word boundary
// and must be padded with zeros to a 32-bit boundary
O2node **Hash_node::lookup(O2string key)
{
    int n = children.size();
    // since a Hash_node can be initialized with no table, we might have to
    // create the table to proceed:
    if (n <= 0) {
        n = 2;
        table_init(n);
    }
    int64_t hash = get_hash(key);
    int index = hash % n;
    O2node **ptr = &children[index];
    while (*ptr) {
        if (streql(key, (*ptr)->key)) {
            break;
        }
        ptr = &(*ptr)->next;
    }
    return ptr;
}


// remove a child from a hash node. Then free the child
// (deleting its entire subtree, or if it is a leaf, removing the
// entry from the o2_ctx->full_path_table).
// ptr is the address of the pointer to the table entry to be removed.
// This ptr must be a value returned by o2_lookup or o2_service_find
// Often, we remove an entry to make room for an insertion, so
// we do not want to resize the table. The resize parameter must
// be true to enable resizing.
//
O2err Hash_node::entry_remove(O2node **child, bool resize)
{
    num_children--;
    O2node *entry = *child;
    *child = entry->next;
    entry->o2_delete();
    // if the table is too big, rehash to smaller table
    if (resize && (num_children * 5 < children.size()) && (num_children > 3)) {
        // See below on table resizing.
        // Once allocated, we do not make table size less than 3.
        return table_resize(children.size() / 2 - 1);
    }
    return O2_SUCCESS;
}


// o2_tree_insert_node -- insert a node for pattern matching.
// on entry, table points to a tree node pointer, initially it is the
// address of o2_ctx->path_tree. If key is already in the table and the
// entry is another node, then just return a pointer to the node address.
// Otherwise, if key is a handler, remove it, and then create a new node
// to represent this key.
//
// key is "owned" by caller and must be aligned to 4-byte word boundary
//
Hash_node *Hash_node::tree_insert_node(O2string key)
{
    assert(children.size() > 0);
    O2node **entry_ptr = lookup(key);
    // 3 outcomes: entry exists and is a O2TAG_HASH: return location
    //    entry exists but is something else: delete old and create one
    //    entry does not exist: create one
    if (*entry_ptr) {
        if (ISA_HASH(*entry_ptr)) {
            return TO_HASH_NODE(*entry_ptr);
        } else {
            // this node cannot be a handler (leaf) and a (non-leaf) node
            entry_remove(entry_ptr, false);
        }
    }
    // entry is a valid location. Insert a new node:
    Hash_node *new_entry = new Hash_node(key);
    entry_insert_at(entry_ptr, new_entry);
    return new_entry;
}


// o2_add_entry_at inserts an entry into the hash table. If the
// table becomes too full, a new larger table is created. 
// This function is called after o2_lookup() has been used to
// determine a pointer to the new entry. This pointer is
// passed in loc.
//
// Table resizing is interesting: we want a lot of hysteresis
// so that we have room to grow and shrink. Imagine if adding
// one element causes rehashing and then taking one element
// away rehashes back to a smaller size! So we somewhat
// arbitrarily say that when you expand, you want a factor of
// 2 growth before expanding again, and you want to be able
// to remove 1/2 the entries before rehashing to smaller size.
// These factors cause exponential growth which makes the
// amortized work linear. With factors of 2, the high-water
// mark is a factor of 4 greater than the low water mark.
// Arbitrarily, we chose these to be 0.2 and 0.8 the total
// table size, so the load factor is at least 0.2 and at
// most 0.8. This keeps the expected search time constant,
// and to enumerate everything in the hash table, we have to
// inspect 5 at most 5 buckets for every actual value, so
// that makes enumeration also linear time per value and
// reasonable.
//
O2err Hash_node::entry_insert_at(O2node **loc, O2node *entry)
{
    num_children++;
    entry->next = *loc;
    
    *loc = entry;
    // expand table if it is too small
    if (num_children * 5 > children.size() * 4) {
        return table_resize(num_children * 2);
    }
    return O2_SUCCESS;
}


// remove a dictionary entry by name
// when we remove an entry, we may resize the table to be smaller
// in which case node->children.table is written with a pointer to
// the new table and the old table is freed
//
O2err Hash_node::entry_remove_by_name(O2string key)
{
    O2node **ptr = lookup(key);
    if (*ptr) {
        return entry_remove(ptr, !o2_ctx->finishing);
    }
    return O2_FAIL;
}


// call this from send() method in a subclass, then send the message
// returns the message to send. Caller owns the message.
//
O2message_ptr Proxy_info::pre_send(bool *tcp_flag)
{
    O2message_ptr msg = o2_postpone_delivery();
    // caller now owns the "active" message
#ifndef O2_NO_DEBUG
    {
        O2msg_data_ptr mdp = &msg->data;
        bool sysmsg = mdp->address[1] == '_' || mdp->address[1] == '@';
        O2_DB(sysmsg ? O2_DBS_FLAG : O2_DBs_FLAG,
              const char *desc = (mdp->misc & O2_TCP_FLAG ?
                                  "queueing/sending TCP" : "sending UDP");
              o2_dbg_msg(desc, msg, mdp, "to", key));
    }
#endif
    *tcp_flag = msg->data.misc & O2_TCP_FLAG; // before byte swap
#if IS_LITTLE_ENDIAN
    if (fds_info || ISA_MQTT(this)) {  // needs network order
        o2_msg_swap_endian(&msg->data, true);
    }
#endif
    return msg;
}



// default deliver works for Proc and O2lite
//
// called from network.cpp when a message arrives by TCP or UDP.
// message is in network byte order.
// message must be removed from pending deliveries -- we own it now.
//
O2err Proxy_info::deliver(O2netmsg_ptr o2n_msg)
{
    O2message_ptr msg = (O2message_ptr) o2n_msg;
#if IS_LITTLE_ENDIAN
    o2_msg_swap_endian(&msg->data, false);
#endif
    O2_DB((msg->data.address[1] == '_' || msg->data.address[1] == '@') ?
                  O2_DBR_FLAG : O2_DBr_FLAG,
                  o2_dbg_msg("msg received", msg, &msg->data, "by",
                      o2_tag_to_string(tag)));
    // some handlers, especially internal ones, need to know the source
    // of the message, but generally we do not want handlers to know this
    // because clients of the O2 library should not be concerned with the
    // internal details and classes. Therefore, we have a "hidden"
    // O2 internal global variable to record the message source.
    o2_message_source = this;
    o2_message_send(msg);
    o2_message_source = NULL;
    return O2_SUCCESS;
}


#ifndef O2_NO_DEBUG
void o2_fds_info_debug_predelete(Fds_info *info)
{
    if (!info) return;  // maybe socket was deleted before this
    // this is called before someone closes a socket. It is just
    // for debugging. You can print info here.
}
#endif
