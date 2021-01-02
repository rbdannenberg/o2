/* debug.c -- debugging support */

/* Roger B. Dannenberg
 * April 2020
 */

#include "o2internal.h"
#include "services.h" // for o2osc.h
#include "o2osc.h" // for tags
#include "message.h"

#ifndef O2_NO_DEBUG
const char *o2_debug_prefix = "O2:";
int o2_debug = 0;

static const char *debug_chars = "crsRSkdhtTlmoOqg";

void o2_debug_flags(const char *flags)
{
    o2_debug = 0;
    int flag = 1;
    const char *dcptr = debug_chars;
    while (*dcptr) {
        if (strchr(flags, *dcptr)) {
            o2_debug |= flag;
        }
        flag <<= 1;
        dcptr++;
    }
    if (strchr(flags, 'n')) o2_debug |= O2_DBn_FLAGS;
    if (strchr(flags, 'a')) o2_debug |= O2_DBa_FLAGS;
    if (strchr(flags, 'A')) o2_debug |= O2_DBA_FLAGS;
    if (strchr(flags, 'N')) o2n_network_enabled = false;
}

void o2_dbg_msg(const char *src, O2message_ptr msg, o2_msg_data_ptr data,
                const char *extra_label, const char *extra_data)
{
    printf("%s %s ", o2_debug_prefix, src);
    if (msg) {
        printf("(%p) ", msg);
    }
    printf("at %gs (local %gs)", o2_time_get(), o2_local_time());
    if (extra_label)
        printf(" %s: %s", extra_label, extra_data);
    printf("\n    ");
    o2_msg_data_print(data);
    printf("\n");
}


const char *o2_tag_to_string(int tag)
{
    switch (tag & O2TAG_TYPE_BITS) {
        case O2TAG_EMPTY:              return "EMPTY";
        case O2TAG_HASH:               return "HASH";
        case O2TAG_HANDLER:            return "HANDLER";
        case O2TAG_SERVICES:           return "SERVICES";
        case O2TAG_PROC_TCP_SERVER:    return "PROC_TCP_SERVER";
        case O2TAG_PROC_NOMSGYET:      return "PROC_NOMSGYET";
        case O2TAG_PROC:               return "PROC";
        case O2TAG_PROC_TEMP:          return "PROC_TEMP";
        case O2TAG_MQTT:               return "MQTT";
        case O2TAG_OSC_UDP_SERVER:     return "OSC_UDP_SERVER";
        case O2TAG_OSC_TCP_SERVER:     return "OSC_TCP_SERVER";
        case O2TAG_OSC_UDP_CLIENT:     return "OSC_UDP_CLIENT";
        case O2TAG_OSC_TCP_CLIENT:     return "OSC_TCP_CLIENT";
        case O2TAG_OSC_TCP_CONNECTION: return "OSC_TCP_CONNECTION";
        case O2TAG_BRIDGE:             return "BRIDGE";
        case O2TAG_STUN:               return "STUN";
        default:                       return Fds_info::tag_to_string(tag);
    }
}


#endif

