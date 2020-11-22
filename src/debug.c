/* debug.c -- debugging support */

/* Roger B. Dannenberg
 * April 2020
 */

#include "o2internal.h"
#include "services.h" // for o2osc.h
#include "o2osc.h" // for tags
#include "bridge.h"
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

void o2_dbg_msg(const char *src, o2_message_ptr msg, o2_msg_data_ptr data,
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

static const char *entry_tags[6] = { "NODE_HASH", "NODE_HANDLER",
        "NODE_SERVICES", "NODE_OSC_REMOTE_SERVICE", "NODE_BRIDGE_SERVICE",
        "NODE_EMPTY"
};

static const char *proc_tags[5] = { "PROC_TCP_SERVER", "PROC_NOMSGYET",
                            "PROC_NOCLOCK", "PROC_SYNCED", "PROC_TEMP" };

#ifndef O2_NO_OSC
static const char *osc_tags[5] = { "OSC_UDP_SERVER", "OSC_TCP_SERVER",
                                   "OSC_UDP_CLIENT", "OSC_TCP_CLIENT",
                                   "OSC_TCP_CONNECTION" };
#endif
#ifndef O2_NO_BRIDGES
static const char *bridge_tags[2]  = { "BRIDGE_NOCLOCK", "BRIDGE_SYNCED" };
#endif

const char *o2_tag_to_string(int tag)
{
    if (tag >= NODE_HASH && tag <= NODE_EMPTY)
        return entry_tags[tag - NODE_HASH];
    if (tag >= PROC_TCP_SERVER && tag <= PROC_TEMP)
        return proc_tags[tag - PROC_TCP_SERVER];
#ifndef O2_NO_OSC
    if (tag >= OSC_UDP_SERVER && tag <= OSC_TCP_CONNECTION)
        return osc_tags[tag - OSC_UDP_SERVER];
#endif
#ifndef O2_NO_BRIDGES
    if (tag >= BRIDGE_NOCLOCK && tag <= BRIDGE_SYNCED)
        return bridge_tags[tag - BRIDGE_NOCLOCK];
#endif
#ifndef O2_NO_MQTT
    if (tag == STUN_CLIENT) return "STUN_CLIENT";
    if (tag == MQTT_CLIENT) return "MQTT_CLIENT";
    if (tag == MQTT_NOCLOCK) return "MQTT_NOCLOCK";
    if (tag == MQTT_SYNCED) return "MQTT_SYNCED";
#endif
    else return o2n_tag_to_string(tag);
}

static const char *status_strings[] = {
    "O2_LOCAL_NOTIME",
    "O2_REMOTE_NOTIME",
    "O2_BRIDGE_NOTIME",
    "O2_TO_OSC_NOTIME",
    "O2_LOCAL",
    "O2_REMOTE",
    "O2_BRIDGE",
    "O2_TO_OSC" };

const char *o2_status_to_string(o2_status_t status)
{
    static char unknown[32];
    if (status >= 0 && status <= 7) {
        return status_strings[status];
    } else if (status == O2_UNKNOWN) {
        return "O2_FAIL";
    }
    sprintf(unknown, "UNKNOWN(%d)", status);
    return unknown;
}


#endif
