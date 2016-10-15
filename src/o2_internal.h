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

// Configuration:
#define IP_ADDRESS_LEN 32

/** Note: No struct literals in MSVC. */
#ifdef _MSC_VER
#ifndef USE_ANSI_C
#define USE_ANSI_C
#endif

// Preclude warnings for string functions
#define _CRT_SECURE_NO_WARNINGS

// OS X and Linux call it "snprintf":
// #define snprintf _snprintf

#endif   // WIN32

#include "o2_error.h"

#ifndef O2_NO_DEBUGGING
// macro to surround debug print statements:
#define O2_DB(x) if (o2_debug) { (x); }
#define O2_DB2(x) if (o2_debug > 1) { (x); }
#define O2_DB3(x) if (o2_debug > 2) { (x); }
#define O2_DB4(x) if (o2_debug > 3) { (x); }
#else
#define O2_DB(x)
#define O2_DB2(x)
#define O2_DB3(x)
#define O2_DB4(x)
#endif

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

#define WORD_ALIGN_PTR(p) ((char *) (((size_t) (p)) & ~3))
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

extern char *debug_prefix;
extern SOCKET local_send_sock; // socket for sending all UDP msgs

extern o2_time o2_local_now;
extern o2_time o2_global_now;
extern int o2_gtsched_started;


#define O2_ARGS_END O2_MARKER_A, O2_MARKER_B
/** Default max send and recieve buffer. */
#define MAX_BUFFER 1024

/* \brief Maximum length of UDP messages in bytes
 */
#define O2_MAX_MSG_SIZE 32768

/* \brief A set of macros to represent different communications transports
 */
#define O2_DEFAULT 0x0
#define O2_UDP     0x1
#define O2_UNIX    0x2
#define O2_TCP     0x4

#ifdef SWAP64
/**
 *  Endian part.
 */
typedef union {
    uint64_t all;
    struct {
        uint32_t a;
        uint32_t b;
    } part;
} o2_split64;

#ifdef _MSC_VER
#define O2_INLINE __inline
#else
#define O2_INLINE inline
#endif
static O2_INLINE uint64_t o2_hn64(uint64_t x)
{
    o2_split64 in, out;
    
    in.all = x;
    out.part.a = htonl(in.part.b);
    out.part.b = htonl(in.part.a);
    
    return out.all;
}
#undef O2_INLINE
#endif

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
    char *name;
} service_table;


/// used for discover, udp and tcp sockets
typedef struct o2_socket {
	char c;
} o2_socket;

// global variables
extern process_info o2_process;
extern o2_arg_ptr *o2_argv; // arg vector extracted by calls to o2_get_next()
extern int o2_argc; // length of argv

// shared internal functions

o2_time o2_local_time();

int ping(o2_message_ptr msg, const char *types,
         o2_arg_ptr *argv, int argc, void *user_data);

void o2_sched_init();

#endif /* O2_INTERNAL_H */
/// \endcond
