/* debug.h -- debug support */

/* Roger B. Dannenberg
 * April 2020
 */

// o2_message_print() is included in debug versions automatically.
// It is also included if O2_MSGPRINT is defined.

#ifndef O2_NO_DEBUG
#ifndef O2_MSGPRINT
#define O2_MSGPRINT 1
#endif
#endif

#ifdef O2_NO_DEBUG
#define O2_DB(flags, x)
#define O2_DBc(x)
#define O2_DBr(x)
#define O2_DBs(x)
#define O2_DBsS(x)
#define O2_DBR(x)
#define O2_DBS(x)
#define O2_DBk(x)
#define O2_DBd(x)
#define O2_DBdo(x)
#define O2_DBh(x)
#define O2_DBt(x)
#define O2_DBT(x)
#define O2_DBl(x)
#define O2_DBm(x)
#define O2_DBn(x)
#define O2_DBo(x)
#define O2_DBO(x)
#define O2_DBoO(x)
#define O2_DBq(x)
#define O2_DBw(x)
#define O2_DBG(x)
// special multiple category tests:
#define O2_DBoO(x)
#else
const char *o2_tag_to_string(int tag);

extern int o2_debug;
extern const char *o2_debug_prefix;
void o2_dbg_msg(const char *src, O2message_ptr msg, o2_msg_data_ptr data,
                const char *extra_label, const char *extra_data);
void o2_print_path_tree();

// Note: The ordering of these flags is coordinated with debug_chars, 
// defined in debug.c. See o2_debug_flags() implementation there too.
#define O2_DBc_FLAG 1
#define O2_DBr_FLAG 2
#define O2_DBs_FLAG 4
#define O2_DBR_FLAG 8
#define O2_DBS_FLAG 0x10
#define O2_DBk_FLAG 0x20
#define O2_DBd_FLAG 0x40
#define O2_DBh_FLAG 0x80
#define O2_DBt_FLAG 0x100
#define O2_DBT_FLAG 0x200
#define O2_DBl_FLAG 0x400
#define O2_DBm_FLAG 0x800
#define O2_DBn_FLAGS (O2_DBr_FLAG | O2_DBR_FLAG | O2_DBs_FLAG | O2_DBS_FLAG)
#define O2_DBo_FLAG 0x1000
#define O2_DBO_FLAG 0x2000
#define O2_DBq_FLAG 0x4000
#define O2_DBw_FLAG 0x8000
#define O2_DBg_FLAG 0x10000
#define O2_DBG_FLAGS 0x1FFFF
// All flags but malloc, schedulers, o2_msg_deliver, enabled by "A"
#define O2_DBA_FLAGS (O2_DBG_FLAGS-O2_DBm_FLAG-O2_DBl_FLAG-O2_DBt_FLAG-O2_DBT_FLAG)
// All flags but DBm (malloc/free) and DBl (o2_msg_deliver) enabled by "a"
#define O2_DBa_FLAGS (O2_DBG_FLAGS-O2_DBm_FLAG-O2_DBl_FLAG)

// macro to surround debug print statements:
#define O2_DB(flags, x) if (o2_debug & (flags)) { x; }
#define O2_DBA(x) O2_DB(O2_DBA_FLAGS, x)
#define O2_DBa(x) O2_DB(O2_DBa_FLAGS, x)
#define O2_DBc(x) O2_DB(O2_DBc_FLAG | O2_DBo_FLAG, x)
#define O2_DBr(x) O2_DB(O2_DBr_FLAG, x)
#define O2_DBs(x) O2_DB(O2_DBs_FLAG, x)
#define O2_DBR(x) O2_DB(O2_DBR_FLAG, x)
#define O2_DBS(x) O2_DB(O2_DBS_FLAG, x)
#define O2_DBk(x) O2_DB(O2_DBk_FLAG, x)
#define O2_DBd(x) O2_DB(O2_DBd_FLAG, x)
#define O2_DBh(x) O2_DB(O2_DBh_FLAG, x)
#define O2_DBt(x) O2_DB(O2_DBt_FLAG, x)
#define O2_DBT(x) O2_DB(O2_DBT_FLAG, x)
#define O2_DBl(x) O2_DB(O2_DBl_FLAG, x)
#define O2_DBm(x) O2_DB(O2_DBm_FLAG, x)
#define O2_DBn(x) O2_DB(O2_DBn_FLAGS, x)
#define O2_DBo(x) O2_DB(O2_DBo_FLAG, x)
#define O2_DBO(x) O2_DB(O2_DBO_FLAG, x)
#define O2_DBq(x) O2_DB(O2_DBq_FLAG, x)
#define O2_DBw(x) O2_DB(O2_DBw_FLAG, x)

// O2_DBg is specifically NOT defined. Instead, 'g' is assumed
// if ANY debugging is enabled (including O2_DBg_FLAG).
// Instead of O2_DBg, use O2_DBG which prints if any flag is set.
#define O2_DBG(x) O2_DB(O2_DBG_FLAGS, x)
// special multiple category tests:
#define O2_DBoO(x) O2_DB(O2_DBo_FLAG | O2_DBO_FLAG, x)
#define O2_DBdo(x) O2_DB(O2_DBd_FLAG | O2_DBo_FLAG, x)
#define O2_DBsS(x) O2_DB(O2_DBs_FLAG | O2_DBS_FLAG, x)
#endif

