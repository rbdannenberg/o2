// o2_bridge.c -- support extensions to non-IP transports
//
// Roger B. Dannenberg
// April 2018

#include "o2.h"
#include "bridge.h"


int o2_bridge_new(bridge_poll_fn bridge_poll, bridge_send_fn bridge_send, void *info)
{
    bridge_entry_ptr bridge = (bridge_entry_ptr) O2_MALLOC(sizeof(bridge_entry));
    bridge->bridge_poll = bridge_poll;
    bridge->bridge_send = bridge_send;
    bridge->info = info;
    return O2_SUCCESS;
}
