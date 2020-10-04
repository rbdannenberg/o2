/* hashnode.c -- implement hash tables */

/* Roger B. Dannenberg
 * April 2020
 */

#include "o2internal.h"
#include "services.h"
#include "o2osc.h"
#include "bridge.h"

#if IS_LITTLE_ENDIAN
// for little endian machines
#define STRING_EOS_MASK 0xFF000000
#else
#define STRING_EOS_MASK 0x000000FF
#endif
#define SCRAMBLE 2686453351680

#define MAX_SERVICE_NUM  1024


static int resize_table(hash_node_ptr node, int new_locs);


int o2_strsize(const char *s)
{
    // coerce to int to avoid compiler warning, O2 messages can't be that long
    return (int) ((strlen(s) + 4) & ~3);
}


static int initialize_hashtable(dyn_array_ptr table, int locations)
{
    DA_INIT_ZERO(*table, o2_node_ptr, locations);
    if (!table->array) return O2_FAIL;
    table->length = locations; // this is a hash table, all locations are used
    // printf("init htable array %p bytes %ld\n", table->array,
    //        table->length * sizeof(o2_node_ptr));
    return O2_SUCCESS;
}


// o2_node_add inserts an entry into the hash table. If the table becomes
// too full, a new larger table is created.
//
int o2_node_add(hash_node_ptr hnode, o2_node_ptr entry)
{
    o2_node_ptr *ptr = o2_lookup(hnode, entry->key);
    if (*ptr) { // if we found it, this is a replacement
        // splice out existing entry and delete it
        o2_hash_entry_remove(hnode, ptr, false);
    }
    return o2_add_entry_at(hnode, ptr, entry);
}


static int resize_table(hash_node_ptr node, int new_locs)
{
    dyn_array old = node->children; // copy whole dynamic array
    if (initialize_hashtable(&node->children, new_locs))
        return O2_FAIL;
    // now, old array is in old, node->children is newly allocated
    // copy all entries from old to node->children
    assert(node->children.array != NULL);
    o2_mem_check(node->children.array);
    enumerate enumerator;
    o2_enumerate_begin(&enumerator, &old);
    o2_node_ptr entry;
    while ((entry = o2_enumerate_next(&enumerator))) {
        o2_mem_check(old.array);
        o2_node_add(node, entry);
    }
    // now we have moved all entries into the new table and we can free the
    // old one
    DA_FINISH(old);
    o2_mem_check(node->children.array);
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
// internal entry (NODE_HASH) or a leaf entry (NODE_HANDLER)
//
void o2_node_free(o2_node_ptr entry)
{
    if (entry->tag == NODE_HASH) {
        o2_hash_node_finish(TO_HASH_NODE(entry));
    } else if (entry->tag == NODE_HANDLER) {
        o2_handler_entry_finish((handler_entry_ptr) entry);
    } else if (entry->tag == NODE_SERVICES) {
        o2_services_entry_finish(TO_SERVICES_ENTRY(entry));
#ifndef O2_NO_BRIDGES
    } else if (ISA_BRIDGE(entry)) {
        bridge_inst_ptr inst = (bridge_inst_ptr) entry;
        (*(inst->proto->bridge_inst_finish))(inst);
#endif
    } else if (entry->tag == NODE_EMPTY) {
        ;
    } else assert(false); // nothing else should be freed
    O2_FREE(entry);
}

#ifndef O2_NO_DEBUG
// debugging code to print o2_node and o2_info structures
void o2_node_show(o2_node_ptr node, int indent)
{
    for (int i = 0; i < indent; i++) printf("  ");
    printf("%s@%p", o2_tag_to_string(node->tag), node);
    if (node->tag == NODE_HASH || node->tag == NODE_HANDLER ||
        node->tag == NODE_SERVICES) {
        if (node->key) printf(" key=%s", node->key);
    }
    if (node->tag == NODE_HASH) {
        printf("\n");
        hash_node_ptr hn = TO_HASH_NODE(node);
        enumerate en;
        o2_enumerate_begin(&en, &hn->children);
        o2_node_ptr entry;
        while ((entry = o2_enumerate_next(&en))) {
            // see if each entry can be found
#ifdef NDEBUG
            o2_lookup(hn, entry->key);
#else
            o2_node_ptr *ptr = o2_lookup(hn, entry->key);
            if (*ptr != entry)
                printf("ERROR: *ptr %p != entry %p\n", *ptr, entry);
#endif
            o2_node_show(entry, indent + 1);
        }
    } else if (node->tag == NODE_SERVICES) {
        printf("\n");
        o2_services_entry_show(TO_SERVICES_ENTRY(node), indent + 1);
    } else if (node->tag == NODE_HANDLER) {
        o2_handler_entry_show(TO_HANDLER_ENTRY(node));
        printf("\n");
    } else if (ISA_PROC(node)) {
        o2_proc_info_show(TO_PROC_INFO(node));
        printf("\n");
#ifndef O2_NO_OSC
    } else if (ISA_OSC(node)) {
        o2_osc_info_show(TO_OSC_INFO(node));
        printf("\n");
#endif
#ifndef O2_NO_BRIDGES
    } else if (ISA_BRIDGE(node)) {
        o2_bridge_show((bridge_inst_ptr) node);
#endif
    } else {
        printf("\n");
    }
}
#endif



// The hash function processes 4 bytes at a time and is based
// on the idea (and I think this is what Java uses) of repeatedly
// multiplying the hash by 5 and adding the next character until
// all characters are used. The SCRAMBLE number is (5 << 8) +
// ((5 * 5) << 16 + ..., so it is similar to doing the multiplies
// and adds all in parallel for 4 bytes at a time.
//
// In O2, o2string means "const char *" with zero padding to a
// 32-bit word boundary.
//
static int64_t get_hash(o2string key)
{
    int32_t *ikey = (int32_t *) key;
    uint64_t hash = 0;
    int32_t c;
    do {
        c = *ikey++;
        // c must be either all non-zero, or each zero must be followed by zero
        // and the last character is zero
        assert(((c & 0xff) && (c & 0xff00) &&
                (c & 0xff0000) && (c & 0xff000000)) ||
               ((((c & 0xff) != 0) || ((c & 0xff00) == 0)) &&
                (((c & 0xff00) != 0) || ((c & 0xff0000) == 0)) &&
                ((c & 0xff000000) == 0)));
        hash = ((hash + c) * SCRAMBLE) >> 32;
    } while (c & STRING_EOS_MASK);
    return hash;
}

void o2_hash_node_finish(hash_node_ptr node)
{
    for (int i = 0; i < node->children.length; i++) {
        o2_node_ptr e = DA_GET(node->children, o2_node_ptr, i);
        while (e) {
            o2_node_ptr next = e->next;
            o2_node_free(e);
            e = next;
        }
    }
    // not all nodes have keys, top-level nodes have key == NULL
    if (node->key) O2_FREE((void *) node->key);
    DA_FINISH(node->children);
}


// copy a string to the heap, result is 32-bit word aligned, has
//   at least one zero end-of-string byte and is
//   zero-padded to the next word boundary
o2string o2_heapify(const char *path)
{
    long len = o2_strsize(path);
    char *rslt = O2_MALLOCNT(len, char);
    // zero fill last 4 bytes
    int32_t *end_ptr = (int32_t *) O2MEM_BIT32_ALIGN_PTR(rslt + len - 1);
    *end_ptr = 0;
    strcpy(rslt, path);
    assert(*path == 0 || *rslt);
    return rslt;
}


// set fields for a node in the path tree
//
// key is "owned" by the caller
//
hash_node_ptr o2_node_initialize(hash_node_ptr node, const char *key)
{
    node->tag = NODE_HASH;
    node->key = key;
    if (key) { // note: top level and services don't have keys
        node->key = o2_heapify(key);
        if (!node->key) {
            O2_FREE(node);
            return NULL;
        }
    }
    node->num_children = 0;
    initialize_hashtable(&node->children, 2);
    return node;
}


// o2_lookup returns a pointer to a pointer to the entry, if any.
// The hash table uses linked lists for collisions to make
// deletion simple. key must be aligned on a 32-bit word boundary
// and must be padded with zeros to a 32-bit boundary
o2_node_ptr *o2_lookup(hash_node_ptr node, o2string key)
{
    int n = node->children.length;
    int64_t hash = get_hash(key);
    int index = hash % n;
    o2_node_ptr *ptr = DA_GET_ADDR(node->children, o2_node_ptr, index);
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
int o2_hash_entry_remove(hash_node_ptr node, o2_node_ptr *child, int resize)
{
    node->num_children--;
    o2_node_ptr entry = *child;
    *child = entry->next;
    o2_node_free(entry);
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


// o2_tree_insert_node -- insert a node for pattern matching.
// on entry, table points to a tree node pointer, initially it is the
// address of o2_ctx->path_tree. If key is already in the table and the
// entry is another node, then just return a pointer to the node address.
// Otherwise, if key is a handler, remove it, and then create a new node
// to represent this key.
//
// key is "owned" by caller and must be aligned to 4-byte word boundary
//
hash_node_ptr o2_tree_insert_node(hash_node_ptr node, o2string key)
{
    assert(node->children.length > 0);
    o2_node_ptr *entry_ptr = o2_lookup(node, key);
    // 3 outcomes: entry exists and is a NODE_HASH: return location
    //    entry exists but is something else: delete old and create one
    //    entry does not exist: create one
    if (*entry_ptr) {
        if ((*entry_ptr)->tag == NODE_HASH) {
            return TO_HASH_NODE(*entry_ptr);
        } else {
            // this node cannot be a handler (leaf) and a (non-leaf) node
            o2_hash_entry_remove(node, entry_ptr, false);
        }
    }
    // entry is a valid location. Insert a new node:
    hash_node_ptr new_entry = o2_hash_node_new(key);
    o2_add_entry_at(node, entry_ptr, (o2_node_ptr) new_entry);
    return new_entry;
}


// o2_add_entry_at inserts an entry into the hash table. If the
// table becomes too full, a new larger table is created. 
// This function is called after o2_lookup() has been used to
// determine a pointer to the new entry. This pointer is
// passed in loc.
//
int o2_add_entry_at(hash_node_ptr node, o2_node_ptr *loc,
                    o2_node_ptr entry)
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


// remove a dictionary entry by name
// when we remove an entry, we may resize the table to be smaller
// in which case node->children.table is written with a pointer to
// the new table and the old table is freed
//
int o2_remove_hash_entry_by_name(hash_node_ptr node, o2string key)
{
    o2_node_ptr *ptr = o2_lookup(node, key);
    if (*ptr) {
        return o2_hash_entry_remove(node, ptr, true);
    }
    return O2_FAIL;
}
