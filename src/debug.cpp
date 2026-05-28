/* debug.c -- debugging support */

/* Roger B. Dannenberg
 * April 2020
 */

#include "o2internal.h"
#include "services.h" // for o2osc.h
#include "o2osc.h" // for tags
#include "message.h"

#ifndef O2_NO_DEBUG
const char *o2_debug_prefix = "O2";
int o2_debug = 0;
static const char *o2_logf_name = "o2debug.log";
static FILE *o2_logf = nullptr;


// basic print to console or log file, depending on o2_debug_flags
void dbprintf(const char *format, ...)
{
    FILE *outf = o2_logf ? o2_logf : stdout;
    va_list args;
    va_start(args, format);
    vfprintf(outf, format, args);
    va_end(args);
    fflush(outf);  // make sure log file is immediately visible
}


// fancy version of dbprintf with leading prefix and timestamp
void hdprintf(const char *format, ...)
{
    double time = o2_time_get();
    const char *islocal = "";
    if (time < 0) {
        time = o2_local_time();
        islocal = "L: ";
    }
    FILE *outf = o2_logf ? o2_logf : stdout;
    fprintf(outf, "%s [%s%4.3f]: ", o2_debug_prefix, islocal, time);
    va_list args;
    va_start(args, format);
    vfprintf(outf, format, args);
    va_end(args);
    fflush(outf);  // make sure log file is immediately visible
}


void o2_set_logfile_name(const char *name)
{
    o2_logf_name = name;
}


// WARNING: this string is in the exact bit order of debug flags, e.g.
// O2_DBB_FLAG is 1, O2_DBb_FLAG is 2, O2_DBc_FLAG is 4, etc., hence
// this string starts with "Bbc...":
static const char *debug_chars = "BbcdFghklmOopQqRrSsTtWwzL";
#endif

void o2_debug_flags(const char *flags)
{
#ifndef O2_NO_DEBUG
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
    if (strchr(flags, 'I')) o2n_internet_enabled = false;
    if (strchr(flags, 'L')) {
        // note that log file is reset each time o2_debug_flags is called
        o2_logf = fopen(o2_logf_name, "w");
    } else {
        if (o2_logf) {
            fclose(o2_logf);
            o2_logf = nullptr;
        }
    }
#endif
}

#ifndef O2_NO_DEBUG
void o2_dbg_msg(const char *src, O2message_ptr msg, O2msg_data_ptr data,
                const char *extra_label, const char *extra_data)
{
    hdprintf("%s ", src);
    if (msg) {
        dbprintf("(%p) ", msg);
    }
    dbprintf("at %gs (local %gs)", o2_time_get(), o2_local_time());
    if (extra_label)
        dbprintf(" %s: %s", extra_label, extra_data);
    dbprintf("\n    ");
    o2_msg_data_print(data);
    dbprintf("\n");
}


const char *o2_tag_to_string(int tag)
{
    switch (tag & O2TAG_TYPE_BITS) {
        case O2TAG_EMPTY:              return "EMPTY";
        case O2TAG_HASH:               return "HASH";
        case O2TAG_HANDLER:            return "HANDLER";
        case O2TAG_SERVICES:           return "SERVICES";
        case O2TAG_PROC_TCP_SERVER:    return "PROC_TCP_SERVER";
        case O2TAG_PROC:               return "PROC";
        case O2TAG_PROC_TEMP:          return "PROC_TEMP";
        case O2TAG_MQTT:               return "PROC_VIA_MQTT";
        case O2TAG_MQTT_CON:           return "MQTT_CLIENT";
        case O2TAG_OSC_UDP_SERVER:     return "OSC_UDP_SERVER";
        case O2TAG_OSC_TCP_SERVER:     return "OSC_TCP_SERVER";
        case O2TAG_OSC_UDP_CLIENT:     return "OSC_UDP_CLIENT";
        case O2TAG_OSC_TCP_CLIENT:     return "OSC_TCP_CLIENT";
        case O2TAG_OSC_TCP_CONNECTION: return "OSC_TCP_CONNECTION";
        case O2TAG_HTTP_SERVER:        return "HTTP_SERVER";
        case O2TAG_HTTP_READER:        return "HTTP_READER";
        case O2TAG_BRIDGE:             return "BRIDGE";
        case O2TAG_ZC:                 return "ZEROCONF";
        case O2TAG_STUN:               return "STUN_CLIENT";
        default:                       return Fds_info::tag_to_string(tag);
    }
}

// this wrapper allows test code to show some internals for debugging
void o2_print_path_tree()
{
    o2_ctx->show_tree();
}


void o2_print_bytes(const char *prefix, const char *bytes, int len)
{
    dbprintf("%s @ %g\n", prefix, o2_local_time());
    int i = 0;
    while (i < len) {
        for (int j = 0; j < 16; j++) {  // print hex chars
            if (i + j < len) {
                dbprintf(" %02x", (uint8_t)bytes[i + j]);
            }
            else {
                dbprintf("   ");
            }
        }
        printf("  ");
        for (int j = 0; j < 16; j++) {  // print ascii chars
            if (i + j < len) {
                uint8_t b = (uint8_t)bytes[i + j];
                dbprintf("%c", (b >= '!' && b <= '~' ? b : (uint8_t)'.'));
            }
        }
        dbprintf("\n");
        i += 16;
    }
}

#endif


