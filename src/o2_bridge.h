// o2_bridge.h -- headers to support extensions for non-IP transports
//
// Roger B. Dannenberg
// April 2018

struct bridge_entry;
typedef struct bridge_entry *bridge_entry_ptr;

typedef void (*bridge_poll_fn)(bridge_entry_ptr node);

typedef void (*bridge_send_fn)(o2_msg_data_ptr msg, int tcp_flag,
                               bridge_entry_ptr node);


typedef struct bridge_entry { // "subclass" of o2_node
    int tag; // O2_BRIDGE
    bridge_poll_fn bridge_poll;
    bridge_send_fn bridge_send;
    void *info;
} bridge_entry, *bridge_entry_ptr;

int o2_bridge_new(bridge_poll_fn bridge_poll, bridge_send_fn bridge_send, void *info);

int o2_bridge_remove(bridge_entry_ptr bridge);

int o2_poll_bridges();

