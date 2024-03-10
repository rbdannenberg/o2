/* debug.h -- debug support */

/* Roger B. Dannenberg
 * April 2020
 */

#ifdef __cplusplus
extern "C" {
#endif

// o2_message_print() is included in debug versions automatically.
// It is also included if O2_MSGPRINT is defined.

#ifndef O2_NO_DEBUG
#ifndef O2_MSGPRINT
#define O2_MSGPRINT 1
#endif
#endif

#ifdef O2_NO_DEBUG
#define O2_DB(flags, x)
#define O2_DBb(x)
#define O2_DBc(x)
#define O2_DBd(x)
#define O2_DBG(x)
// #define O2_DBg(x) -- O2_DBg_FLAG is special
#define O2_DBh(x)
#define O2_DBk(x)
#define O2_DBl(x)
#define O2_DBm(x)
#define O2_DBn(x)
#define O2_DBO(x)
#define O2_DBo(x)
#define O2_DBp(x)
#define O2_DBQ(x)
#define O2_DBq(x)
#define O2_DBR(x)
#define O2_DBr(x)
#define O2_DBS(x)
#define O2_DBs(x)
#define O2_DBt(x)
#define O2_DBT(x)
#define O2_DBW(x)
#define O2_DBw(x)
#define O2_DBz(x)
// special multiple category tests:
#define O2_DBoO(x)
#define O2_DBsS(x)
#define O2_DBdo(x)
#define O2_DBbw(x)
#else
void dbprintf(const char *format, ...);

const char *o2_tag_to_string(int tag);

O2_EXPORT int o2_debug;
O2_EXPORT const char *o2_debug_prefix;
void o2_dbg_msg(const char *src, O2message_ptr msg, O2msg_data_ptr data,
                const char *extra_label, const char *extra_data);
O2_EXPORT void o2_print_path_tree(void);
O2_EXPORT void o2_print_bytes(const char* prefix, const char* bytes, int len);

// Note: The ordering of these flags is coordinated with debug_chars, 
// defined in debug.c. See o2_debug_flags() implementation there too.
#define O2_DBB_FLAG 1
#define O2_DBb_FLAG 2
#define O2_DBc_FLAG 4
#define O2_DBd_FLAG 8
#define O2_DBF_FLAG 0x10
#define O2_DBg_FLAG 0x20
#define O2_DBh_FLAG 0x40
#define O2_DBk_FLAG 0x80
#define O2_DBl_FLAG 0x100
#define O2_DBm_FLAG 0x200
#define O2_DBO_FLAG 0x400
#define O2_DBo_FLAG 0x800
#define O2_DBp_FLAG 0x1000
#define O2_DBQ_FLAG 0x2000
#define O2_DBq_FLAG 0x4000
#define O2_DBR_FLAG 0x8000
#define O2_DBr_FLAG 0x10000
#define O2_DBS_FLAG 0x20000
#define O2_DBs_FLAG 0x40000
#define O2_DBT_FLAG 0x80000
#define O2_DBt_FLAG 0x100000
#define O2_DBW_FLAG 0x200000
#define O2_DBw_FLAG 0x400000
#define O2_DBz_FLAG 0x800000
// IMPORTANT: if you add a flag here or reorder flags, you must also modify
// debug_chars in debug.cpp and probably define O2_DB*? below. Also, update
// O2_DBG_FLAGS here:
// All flag bits are defined here except DBF. O2_DBG_FLAG is used for
// "general" debug output, which is enabled if any flag within DBG is set:
#define O2_DBG_FLAGS (0xFFFFFF - (O2_DBF_FLAG))
// All flags but m (malloc/free) and l (o2_msg_deliver) and
// F (force-MQTT) enabled by "A":
#define O2_DBA_FLAGS (O2_DBG_FLAGS - O2_DBm_FLAG - O2_DBl_FLAG - O2_DBF_FLAG)
// "a" is like "A" but also omits t and T (scheduled messages) and Q:
#define O2_DBa_FLAGS (O2_DBG_FLAGS - O2_DBm_FLAG - O2_DBl_FLAG - \
                      O2_DBt_FLAG - O2_DBT_FLAG - O2_DBF_FLAG - O2_DBQ_FLAG)
// All message sends and receives enabled by "n"
#define O2_DBn_FLAGS (O2_DBr_FLAG | O2_DBR_FLAG | O2_DBs_FLAG | O2_DBS_FLAG)

// macro to surround debug print statements:
#define O2_DB(flags, x) if (o2_debug & (flags)) { x; }
#define O2_DBA(x) O2_DB(O2_DBA_FLAGS, x)
#define O2_DBa(x) O2_DB(O2_DBa_FLAGS, x)
#define O2_DBB(x) O2_DB(O2_DBB_FLAG, x)
#define O2_DBb(x) O2_DB(O2_DBb_FLAG, x)
#define O2_DBc(x) O2_DB(O2_DBc_FLAG | O2_DBo_FLAG, x)
#define O2_DBd(x) O2_DB(O2_DBd_FLAG, x)
#define O2_DBF(x) O2_DB(O2_DBF_FLAG, x)
#define O2_DBh(x) O2_DB(O2_DBh_FLAG, x)
#define O2_DBk(x) O2_DB(O2_DBk_FLAG, x)
#define O2_DBl(x) O2_DB(O2_DBl_FLAG, x)
#define O2_DBm(x) O2_DB(O2_DBm_FLAG, x)
#define O2_DBn(x) O2_DB(O2_DBn_FLAGS, x)
#define O2_DBO(x) O2_DB(O2_DBO_FLAG, x)
#define O2_DBo(x) O2_DB(O2_DBo_FLAG, x)
#define O2_DBp(x) O2_DB(O2_DBp_FLAG, x)
#define O2_DBQ(x) O2_DB(O2_DBQ_FLAG, x)
#define O2_DBq(x) O2_DB(O2_DBq_FLAG | O2_DBQ_FLAG, x)
#define O2_DBR(x) O2_DB(O2_DBR_FLAG, x)
#define O2_DBr(x) O2_DB(O2_DBr_FLAG, x)
#define O2_DBS(x) O2_DB(O2_DBS_FLAG, x)
#define O2_DBs(x) O2_DB(O2_DBs_FLAG, x)
#define O2_DBT(x) O2_DB(O2_DBT_FLAG, x)
#define O2_DBt(x) O2_DB(O2_DBt_FLAG, x)
#define O2_DBW(x) O2_DB(O2_DBW_FLAG, x)
#define O2_DBw(x) O2_DB(O2_DBw_FLAG | O2_DBW_FLAG, x)
#define O2_DBz(x) O2_DB(O2_DBz_FLAG, x)

// O2_DBG(x) runs x if ANY debugging is enabled.
// O2_DBg_FLAG is special (there is no DBg(x) macro).
// Setting O2_DBg_FLAG enables O2_DBG(x) actions
// without enabling any more specific OS_DB? macros.
#define O2_DBG(x) O2_DB(O2_DBG_FLAGS, x)
// special multiple category tests:
#define O2_DBoO(x) O2_DB(O2_DBo_FLAG | O2_DBO_FLAG, x)
#define O2_DBdo(x) O2_DB(O2_DBd_FLAG | O2_DBo_FLAG, x)
#define O2_DBsS(x) O2_DB(O2_DBs_FLAG | O2_DBS_FLAG, x)
#define O2_DBbw(x) O2_DB(O2_DBb_FLAG | O2_DBw_FLAG, x)
#endif

#ifdef __cplusplus
}
#endif
