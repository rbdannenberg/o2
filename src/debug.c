/* debug.c -- debugging support */

/* Roger B. Dannenberg
 * April 2020
 */

#include "o2internal.h"
#include "message.h"

#ifndef O2_NO_DEBUG
char *o2_debug_prefix = "O2:";
int o2_debug = 0;

void o2_debug_flags(const char *flags)
{
    o2_debug = 0;
    if (strchr(flags, 'c')) o2_debug |= O2_DBc_FLAG;
    if (strchr(flags, 'r')) o2_debug |= O2_DBr_FLAG;
    if (strchr(flags, 's')) o2_debug |= O2_DBs_FLAG;
    if (strchr(flags, 'R')) o2_debug |= O2_DBR_FLAG;
    if (strchr(flags, 'S')) o2_debug |= O2_DBS_FLAG;
    if (strchr(flags, 'k')) o2_debug |= O2_DBk_FLAG;
    if (strchr(flags, 'd')) o2_debug |= O2_DBd_FLAG;
    if (strchr(flags, 'h')) o2_debug |= O2_DBh_FLAG;
    if (strchr(flags, 't')) o2_debug |= O2_DBt_FLAG;
    if (strchr(flags, 'T')) o2_debug |= O2_DBT_FLAG;
    if (strchr(flags, 'm')) o2_debug |= O2_DBm_FLAG;
    if (strchr(flags, 'n')) o2_debug |= O2_DBn_FLAGS;
    if (strchr(flags, 'o')) o2_debug |= O2_DBo_FLAG;
    if (strchr(flags, 'O')) o2_debug |= O2_DBO_FLAG;
    if (strchr(flags, 'g')) o2_debug |= O2_DBg_FLAGS;
    if (strchr(flags, 'a')) o2_debug |= O2_DBa_FLAGS;
    if (strchr(flags, 'A')) o2_debug |= O2_DBA_FLAGS;
}

void o2_dbg_msg(const char *src, o2_msg_data_ptr msg,
                const char *extra_label, const char *extra_data)
{
    printf("%s %s at %gs (local %gs)", o2_debug_prefix,
           src, o2_time_get(), o2_local_time());
    if (extra_label)
        printf(" %s: %s ", extra_label, extra_data);
    printf("\n    ");
    o2_msg_data_print(msg);
    printf("\n");
}
#endif
