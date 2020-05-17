/* debug.h -- debug support */

/* Roger B. Dannenberg
 * April 2020
 */

#ifdef O2_NO_DEBUG
#define o2_debug_flags(x)
#define O2_DBc(x)
#define O2_DBr(x)
#define O2_DBs(x)
#define O2_DBR(x)
#define O2_DBS(x)
#define O2_DBk(x)
#define O2_DBd(x)
#define O2_DBh(x)
#define O2_DBt(x)
#define O2_DBT(x)
#define O2_DBm(x)
#define O2_DBn(x)
#define O2_DBo(x)
#define O2_DBO(x)
#define O2_DBg(x)
// special multiple category tests:
#define O2_DBoO(x)
#else
extern int o2_debug;
extern char *o2_debug_prefix;
void o2_dbg_msg(const char *src, o2_msg_data_ptr msg,
                const char *extra_label, const char *extra_data);
// macro to surround debug print statements:
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
#define O2_DBm_FLAG 0x400
#define O2_DBn_FLAGS (O2_DBr_FLAG | O2_DBR_FLAG | O2_DBs_FLAG | O2_DBS_FLAG)
#define O2_DBo_FLAG 0x800
#define O2_DBO_FLAG 0x1000
// All flags but malloc and schedulers, enabled by "A"
#define O2_DBA_FLAGS (0x1FFF-O2_DBm_FLAG-O2_DBt_FLAG-O2_DBT_FLAG)
// All flags but DBm (malloc/free) enabled by "a"
#define O2_DBa_FLAGS (0x1FFF-O2_DBm_FLAG)

#define O2_DB(flags, x) if (o2_debug & (flags)) { x; }
#define O2_DBc(x) O2_DB(O2_DBc_FLAG, x)
#define O2_DBr(x) O2_DB(O2_DBr_FLAG, x)
#define O2_DBs(x) O2_DB(O2_DBs_FLAG, x)
#define O2_DBR(x) O2_DB(O2_DBR_FLAG, x)
#define O2_DBS(x) O2_DB(O2_DBS_FLAG, x)
#define O2_DBk(x) O2_DB(O2_DBk_FLAG, x)
#define O2_DBd(x) O2_DB(O2_DBd_FLAG, x)
#define O2_DBh(x) O2_DB(O2_DBh_FLAG, x)
#define O2_DBt(x) O2_DB(O2_DBt_FLAG, x)
#define O2_DBT(x) O2_DB(O2_DBT_FLAG, x)
#define O2_DBm(x) O2_DB(O2_DBm_FLAG, x)
#define O2_DBn(x) O2_DB(O2_DBn_FLAGS, x)
#define O2_DBo(x) O2_DB(O2_DBo_FLAG, x)
#define O2_DBO(x) O2_DB(O2_DBO_FLAG, x)
// general debug msgs ('g') are printed if ANY other debugging enabled
#define O2_DBg_FLAGS (O2_DBa_FLAGS | O2_DBm_FLAG)
#define O2_DBg(x) O2_DB(O2_DBg_FLAGS, x)
// special multiple category tests:
#define O2_DBoO(x) O2_DB(O2_DBo_FLAG | O2_DBO_FLAG, x)
#define O2_DBdo(x) O2_DB(O2_DBd_FLAG | O2_DBo_FLAG, x)
#endif

