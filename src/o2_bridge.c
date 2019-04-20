// o2_bridge.c -- support extensions to non-IP transports
//
// Roger B. Dannenberg
// April 2018

#include "o2.h"
#include "bridge.h"


int o2_create_bridge(bridge_send_fn bridge_send, void *info, const char *name, )
{
    bridge_info_ptr bridge = (bridge_info_ptr) o2_malloc(sizeof(bridge_info));
    bridge->bridge_send = bridge_send;
    bridge->info = info;
}


int o2_delete_bridge(bridge_info_ptr bridge);
