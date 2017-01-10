//
//  o2_internal.h
//  o2
//
//  Created by 弛张 on 1/24/16.
//  Copyright © 2016 弛张. All rights reserved.
//
/// \cond INTERNAL

#ifndef o2_INTERNAL_H
#define o2_INTERNAL_H

#include "o2.h"
#include "o2_dynamic.h"
#include "o2_socket.h"
#include "o2_search.h"

// Configuration:
#define IP_ADDRESS_LEN 32

/** Note: No struct literals in MSVC. */
#ifdef _MSC_VER

#ifndef USE_ANSI_C
#define USE_ANSI_C
#endif

#ifndef _CRT_SECURE_NO_WARNINGS
// Preclude warnings for string functions
#define _CRT_SECURE_NO_WARNINGS
#endif

// OS X and Linux call it "snprintf":
// snprintf seems to be defined Visual Studio now,
//#define snprintf _snprintf

#else    // Linux or OS X

#define ioctlsocket ioctl
#define closesocket close

#endif   // _MSC_VER

#ifdef O2_NO_DEBUGGING
#define o2_debug_flags(x)
#define O2_DBC(x)
#define O2_DBr(x)
#define O2_DBs(x)
#define O2_DBR(x)
#define O2_DBS(x)
#define O2_DBK(x)
#define O2_DBD(x)
#define O2_DBt(x)
#define O2_DBT(x)
#define O2_DBM(x)
#define O2_DBo(x)
#define O2_DBO(x)
#define O2_DBG(x)
// speical multiple category tests:
#define O2_DBoO(x)
#else
extern int o2_debug;
void o2_dbg_msg(const char *src, o2_msg_data_ptr msg,
                const char *extra_label, const char *extra_data);
// macro to surround debug print statements:
#define O2_DBC_FLAG 1
#define O2_DBr_FLAG 2
#define O2_DBs_FLAG 4
#define O2_DBR_FLAG 8
#define O2_DBS_FLAG 0x10
#define O2_DBK_FLAG 0x20
#define O2_DBD_FLAG 0x40
#define O2_DBt_FLAG 0x80
#define O2_DBT_FLAG 0x100
#define O2_DBM_FLAG 0x200
#define O2_DBo_FLAG 0x400
#define O2_DBO_FLAG 0x800
#define O2_DBG_FLAG 0x1000
// All flags but DBM (malloc/free) enabled by "a"
#define O2_DBA_FLAGS (0x1FFF-0x200)

#define O2_DBC(x) if (o2_debug & O2_DBC_FLAG) { x; }
#define O2_DBr(x) if (o2_debug & O2_DBr_FLAG) { x; }
#define O2_DBs(x) if (o2_debug & O2_DBs_FLAG) { x; }
#define O2_DBR(x) if (o2_debug & O2_DBR_FLAG) { x; }
#define O2_DBS(x) if (o2_debug & O2_DBS_FLAG) { x; }
#define O2_DBK(x) if (o2_debug & O2_DBK_FLAG) { x; }
#define O2_DBD(x) if (o2_debug & O2_DBD_FLAG) { x; }
#define O2_DBt(x) if (o2_debug & O2_DBt_FLAG) { x; }
#define O2_DBT(x) if (o2_debug & O2_DBT_FLAG) { x; }
#define O2_DBM(x) if (o2_debug & O2_DBM_FLAG) { x; }
#define O2_DBo(x) if (o2_debug & O2_DBo_FLAG) { x; }
#define O2_DBO(x) if (o2_debug & O2_DBO_FLAG) { x; }
// general debug msgs ('g') are printed if ANY other debugging enabled
#define O2_DBG_FLAGS (O2_DBA_FLAGS|O2_DBM_FLAG)
#define O2_DBG(x) if (o2_debug & O2_DBG_FLAGS) { x; }
// special multiple category tests:
#define O2_DBoO(x) if (o2_debug & (O2_DBo_FLAG | O2_DBO_FLAG)) { x; }
#endif

#define RETURN_IF_ERROR(expr) { int err = (expr); if (err) return err; }

// define IS_BIG_ENDIAN, IS_LITTLE_ENDIAN, and swap64(i),
// swap32(i), and swap16(i)
#if WIN32
// WIN32 requires predefinition of IS_BIG_ENDIAN=1 or IS_BIG_ENDIAN=0
#else
 #ifdef __APPLE__
  #include "machine/endian.h" // OS X endian.h is in MacOSX10.8.sdk/usr/include/machine/endian.h
  #define LITTLE_ENDIAN __DARWIN_LITTLE_ENDIAN
 #else
  #include <endian.h>
  #define LITTLE_ENDIAN __LITTLE_ENDIAN
  #define BYTE_ORDER __BYTE_ORDER
 #endif
 #define IS_BIG_ENDIAN (BYTE_ORDER != LITTLE_ENDIAN)
#endif
#define IS_LITTLE_ENDIAN (!(IS_BIG_ENDIAN))
#define swap16(i) ((((i) >> 8) & 0xff) | (((i) & 0xff) << 8))
#define swap32(i) ((((i) >> 24) & 0xff) | (((i) & 0xff0000) >> 8) | \
                   (((i) & 0xff00) << 8) | (((i) & 0xff) << 24))
#define swap64(i) ((((uint64_t) swap32(i)) << 32) | swap32((i) >> 32))
#define O2_DEF_TYPE_SIZE 8
#define O2_DEF_DATA_SIZE 8

#define WORD_OFFSET(i) ((i) & ~3)

#define streql(a, b) (strcmp(a, b) == 0)

/**
 *  Common head for both Windows and Unix.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <assert.h>

extern char *o2_debug_prefix;
extern SOCKET local_send_sock; // socket for sending all UDP msgs

extern o2_time o2_local_now;
extern o2_time o2_global_now;
extern int o2_gtsched_started;

#define DEFAULT_DISCOVERY_PERIOD 4.0
extern o2_time o2_discovery_period;

#define O2_ARGS_END O2_MARKER_A, O2_MARKER_B
/** Default max send and recieve buffer. */
#define MAX_BUFFER 1024

/* \brief Maximum length of UDP messages in bytes
 */
#define O2_MAX_MSG_SIZE 32768

// macro to make a byte pointer
#define PTR(addr) ((char *) (addr))

/// how many bytes are used by next and length fields before data and by
/// 4 bytes of zero pad after the data?

#define MESSAGE_EXTRA ((PTR(&((o2_message_ptr) 0)->data.timestamp) - \
                        PTR(&((o2_message_ptr) 0)->next)) + 4)

/// how big should whole o2_message be to leave len bytes for the data part?
#define MESSAGE_SIZE_FROM_ALLOCATED(len) ((len) + MESSAGE_EXTRA)

/// how many bytes of data are left if the whole o2_message is size bytes?
#define MESSAGE_ALLOCATED_FROM_SIZE(size) ((size) - MESSAGE_EXTRA)

#define MESSAGE_DEFAULT_SIZE 240

// The structure of the local service, needed to construct discovery messages
typedef struct service_table {
    const char *name;
} service_table;

// global variables
extern fds_info_ptr o2_process;
extern o2_arg_ptr *o2_argv; // arg vector extracted by calls to o2_get_next()
extern int o2_argc; // length of argv

// shared internal functions
void o2_notify_others(const char *service_name, int added);

#endif /* O2_INTERNAL_H */
/// \endcond
