// o2_bridge.h -- headers to support extensions for non-IP transports
//
// Roger B. Dannenberg
// April 2018


typedef void (*bridge_poll_fn)(bridge_info_ptr info);

typedef void (*bridge_send_fn)(o2_msg_data_ptr msg, int tcp_flag,
                               bridge_info_ptr info);


typedef struct bridge_info { // "subclass" of o2_info
    int tag; // O2_BRIDGE
    bridge_poll_fn bridge_poll;
    bridge_send_fn bridge_send;
    void *info;
} bridge_info, *bridge_info_ptr;

extern bridges

int o2_create_bridge(bridge_send_fn bridge_send, void *info);

int o2_poll_bridges();

