// o2_bridge.h -- headers to support extensions for non-IP transports
//
// Roger B. Dannenberg
// April 2018

#ifndef O2_NO_BRIDGES

#define BRIDGE_INST (BRIDGE_SYNCED + 1)

#define ISA_BRIDGE(b) (((b)->tag >= BRIDGE_NOCLOCK) && \
                       ((b)->tag <= BRIDGE_SYNCED))
#define ISA_BRIDGE_INST(b) ((b)->tag >= BRIDGE_INSTANCE)

#ifdef O2_NO_DEBUG
#define TO_BRIDGE_INST(node) ((bridge_inst_ptr) (node))
#else
#define TO_BRIDGE_INST(node) (assert(ISA_BRIDGE(((bridge_inst_ptr) (node)))),\
                              ((bridge_inst_ptr) (node)))
#endif


int o2_bridge_find_protocol(const char *protocol_name,
                            bridge_protocol_ptr *protocol);

int o2_poll_bridges(void);

void o2_bridges_initialize(void);

void o2_bridges_finish(void);

void o2_bridge_inst_free(bridge_inst_ptr bi);

bridge_inst_ptr o2_bridge_inst_new(bridge_protocol_ptr proto, void *info);

extern bridge_protocol_ptr o2lite_bridge;

#endif
