//
//  o2_message.c
//  O2
//
//  Created by 弛张 on 1/26/16.
//  Copyright © 2016 弛张. All rights reserved.
//
// This module constructs and deconstructs messages.
//
// For deconstruction, the "deluxe" result is an argument vector
// (argv) consisting of (essentially) one pointer per argument.
// These argv pointers point into the message when no type
// conversion is required, and into an argument data buffer when
// data must be copied from the message and converted. (We do not
// convert data in place because the message must be retained for
// possible delivery to another handler.)
//
// Deconstruction can also be incremental, fetching one argument
// at a time, but that still results in forming an argument vector.
//
// To simplify deconstruction, we allocate two buffers: One is
// for the argument vector (pointers). The other is for argument
// data. The problem is further simplified by allocating space for
// the worst case based on the total message length. The worst case
// number of arguments is message length / 4 assuming parameters
// take at least 4 bytes. Thus, the size for argv must be:
//   (length/4) * sizeof(o2_arg_ptr)
// However, vectors could have zero length yet have an arg
// vector element. This could be represented in the message as
// "[]..." so we should allocate 4 times the message length for
// the arg vector. To tighten this bound, notice that we only need
// 4 times the type string length for arrays and twice the
// remaining message length for vectors.
//
// The worst case data size happens when data is coerced to
// 64-bits, e.g. doubles, from 32-bit ints or floats. Another
// possibility is coercion from 0-length arrays to 0-length vectors.
// The type strings would be "[][]..." taking two bytes per element.
// The vectors take a length count, type, and pointer, which could total
// 16 bytes, resulting in a factor of 8 expansion. We can cut this
// down by storing only the length (32-bits) when it is 
// zero, again limiting the expansion factor to 2. If the arrays
// have one element, e.g. "[f][f]...", we require 7 bytes per array.
// If these are converted to vectors of doubles, we need 24 bytes
// per vector (len = 1, typ = 'f', pointer to vector, and the 
// double itself. Here, the memory requirement is 24/7 times 
// the message length. Again, we can tighten the bound: upper 
// bounds are  24/3 times the size of the type string, and 
// 24/4 times the size of the remaining message.
//
// The main motivation to pre-allocate storage before unpacking
// messages is that vectors can have pointers to coerced data
// so if we have to reallocate data, we have to scan the coerced
// data and adjust the pointers. This in turn, requires that we
// retain the types of all the arg vectors, requiring even more
// allocation and bookkeeping.

#include "o2_internal.h"
#include "o2_message.h"
#include "o2_discovery.h"
#include "o2_send.h"


// --------- PART 1 : SCRATCH AREAS FOR MESSAGE CONSTRUCTION --------
// Construct messages by writing type string to msg_types and data to
// msg_data. These arrays grow as needed, so they are dynamic arrays.
// These arrays are only freed when o2 is shut down. Since the storage
// is retained, message construction is NOT REENTRANT. You MUST finish
// construction and take away a message before starting the next message.
//     This approach potentially adds a copy operation from msg_data to
// the message itself, but except for cases where the type string is 
// known in advance, you have to copy anyway to place the data after the
// type string. Furthermore, even if you know the type string, you do 
// not necessarily know the data length -- if you start assembling the 
// message and have to "grow" the message, you'll have to copy anyway.
// Therefore, you can only avoid copies on short messages with known
// type strings, but if the message is short, the copy cost is probably
// insignificant compared to all the other work to send, schedule, and
// dispatch the message.

// msg_types is used to hold type codes as message args are accumulated
static dyn_array msg_types;

// msg_data is used to hold data as message args are accumulated
static dyn_array msg_data;


// make sure enough memory is allocated to add an element to msg_data
//
static void message_check_length(int needed)
{
    while (msg_data.length + needed > msg_data.allocated) {
        o2_da_expand(&msg_data, sizeof(char));
    }
}


static void add_type(char type_char)
{
    DA_APPEND(msg_types, char, type_char);
}


#define ADD_DATA(data_type, code, data)      \
    message_check_length(sizeof(data_type));                      \
    *((data_type *) (msg_data.array + msg_data.length)) = (data); \
    msg_data.length += sizeof(data_type);                         \
    DA_APPEND(msg_types, char, code);


// -------- PART 2 : SCRATCH AREA FOR MESSAGE EXTRACTION
// Messages are unpacked into an argv of pointer to union type
// o2_arg_ptr, to allow access according to type codes. There 
// is also storage pointed to by the argv pointers. When types
// are converted, we often need to copy from the message into
// this storage, but if possible we avoid copies by pointing
// into the message itself. o2_argv_data is a dynamic array for
// o2_argv, and o2_arg_data is a dynamic array for type-converted
// message data. Because of the pointers, it's very messy to
// reallocate the dynamic arrays. Instead, we precompute the 
// worst case based on the length of the message type string
// and the length of the message data, and we expand the 
// dynamic arrays to accommodate the worst case scenario.

o2_arg_ptr *o2_argv; // arg vector extracted by calls to o2_get_next()
int o2_argc; // length of argv

// o2_argv_data is used to create the argv for handlers. It is expanded as
// needed to handle the largest message and is reused.
dyn_array o2_argv_data;

// o2_arg_data holds parameters that are coerced from message data
// It is referenced by o2_argv_data and expanded as needed.
dyn_array o2_arg_data;

// make sure enough memory is allocated and initialize o2_argv and o2_argc
//
static void need_argv(int argv_needed, int arg_needed)
{
    while (o2_argv_data.allocated < argv_needed) {
        o2_da_expand(&o2_argv_data, 1);
    }
    while (o2_arg_data.allocated < arg_needed) {
        o2_da_expand(&o2_arg_data, 1);
    }
    o2_argv_data.length = 0; // initialize arrays to empty
    o2_arg_data.length = 0;
    o2_argv = DA_GET(o2_argv_data, o2_arg_ptr, 0);
    o2_argc = 0;
}


// call this once when o2 is initialized
void o2_argv_initialize()
{
    DA_INIT(o2_argv_data, o2_arg_ptr, 16);
    DA_INIT(o2_arg_data, char, 96);
    DA_INIT(msg_types, char, 16);
    DA_INIT(msg_data, char, 96);
}


// call this when o2 is finalized
void o2_argv_finish()
{
    DA_FINISH(o2_argv_data);
    DA_FINISH(o2_arg_data);
    DA_FINISH(msg_types);
    DA_FINISH(msg_data);
}


// update o2_arg_data to indicate the something has been appended
#define ARG_DATA_USED(data_type) o2_arg_data.length += sizeof(data_type)

// write a data item into o2_arg_data as part of message construction
#define ARG_DATA(rslt, data_type, data)         \
    *((data_type *) (rslt)) = (data);           \
    ARG_DATA_USED(data_type);

#define ARG_NEXT ((o2_arg_ptr) (o2_arg_data.array + o2_arg_data.length))


/// end of message must be zero to prevent strlen from running off the
/// end of malformed message
#define MSG_ZERO_END(msg, siz) *((int32_t *) &(PTR(msg))[(siz) - 4]) = 0


// ------- PART 3 : ADDING ARGUMENTS TO MESSAGE DATA
// These functions add data to msg_types and msg_data

static int is_bundle = FALSE;
static int is_normal = FALSE;

int o2_send_start()
{
    msg_types.length = 0;
    msg_data.length = 0;
    is_bundle = FALSE;
    is_normal = FALSE;
    add_type(',');
    return O2_SUCCESS;
}

int o2_add_float(float f)
{
    if (is_bundle) return O2_FAIL;
    is_normal = TRUE;
    ADD_DATA(float, 'f', f);
    return O2_SUCCESS;
}


int o2_add_int64(int64_t i)
{
    if (is_bundle) return O2_FAIL;
    is_normal = TRUE;
    ADD_DATA(int64_t, 'h', i);
    return O2_SUCCESS;
}


int o2_add_int32_or_char(o2_type code, int32_t i)
{
    if (is_bundle) return O2_FAIL;
    is_normal = TRUE;
    ADD_DATA(int32_t, code, i);
    return O2_SUCCESS;
}


int o2_add_double_or_time(o2_type code, double d)
{
    if (is_bundle) return O2_FAIL;
    is_normal = TRUE;
    ADD_DATA(double, code, d);
    return O2_SUCCESS;
}


int o2_add_only_typecode(o2_type code)
{
    if (is_bundle) return O2_FAIL;
    is_normal = TRUE;
    message_check_length(0);
    DA_APPEND(msg_types, char, code);
    return O2_SUCCESS;
}


int o2_add_string_or_symbol(o2_type code, const char *s)
{
    if (is_bundle) return O2_FAIL;
    is_normal = TRUE;
    // coerce to avoid compiler warning; o2 messages cannot be that long, but
    // this could overflow if user passed absurd data, but then the string would
    // be arbitrarily truncated. The message could then still be huge, so I'm not
    // sure what would happen.
    int s_len = (int) strlen(s);
    message_check_length(s_len + 4); // add 4 in case of padding
    char *dst = msg_data.array + msg_data.length;
    char *last = dst + s_len;
    size_t ilast = (((size_t) last + 4)) & ~3;
    *((int32_t *) (PTR(ilast) - 4)) = 0;
    memcpy(dst, s, s_len);
    msg_data.length += (s_len + 4) & ~3;
    DA_APPEND(msg_types, char, code);
    return O2_SUCCESS;
}    


int o2_add_blob_data(uint32_t size, void *data)
{
    if (is_bundle) return O2_FAIL;
    is_normal = TRUE;
    message_check_length(size + 8); // add 8 for length and padding
    o2_add_int32_or_char('b', size);
    char *dst = msg_data.array + msg_data.length;
    char *last = dst + size;
    size_t ilast = (((size_t) last + 3)) & ~3;
    if (size > 0) {
        *((int32_t *) (PTR(ilast) - 4)) = 0;
    }
    memcpy(dst, data, size);
    msg_data.length += (size + 3) & ~3;
    return O2_SUCCESS;
}


int o2_add_blob(o2_blob *b)
{
    return o2_add_blob_data(b->size, b->data);
}


int o2_add_midi(uint32_t m)
{
    return o2_add_int32_or_char(O2_MIDI, (int32_t) m);
}


int o2_add_vector(o2_type element_type, int32_t length, void *data)
{
    if (is_bundle) return O2_FAIL;
    is_normal = TRUE;
    if (!strchr("ihfd", element_type)) {
        return O2_BAD_TYPE;
    }
    int size = (element_type == 'd' || element_type == 'h') ?
               sizeof(double) : sizeof(int32_t);
    length *= size; // length is now the vector length in bytes
    // the message contains the number of bytes in the vector data
    message_check_length(sizeof(int32_t) + length);
    o2_add_int32_or_char('v', length);
    add_type(element_type);
    char *dst = msg_data.array + msg_data.length;
    memcpy(dst, data, length);
    msg_data.length += length;
    return O2_SUCCESS;
}


// add a message to a bundle
int o2_add_message(o2_message_ptr msg)
{
    if (is_normal) return O2_FAIL;
    is_bundle = TRUE;
    // add a length followed by data portion of msg
    int msg_len = msg->length + 4; // add 4 for length
    message_check_length(msg_len);
    char *src = PTR(&msg->data) - 4; // get length and data
    char *dst = PTR(msg_data.array) + msg_data.length;
    memcpy(dst, src, msg_len);
    msg_data.length += (msg_len + 3) & ~3;
    return O2_SUCCESS;
}


o2_message_ptr o2_message_finish(o2_time time, const char *address,
                                 int tcp_flag)
{
    return o2_service_message_finish(time, NULL, address, tcp_flag);
}


// finish building message, sending to service with address appended.
// to create a bundle, o2_service_message_finish(time, service, "", tcp_flag)
//
o2_message_ptr o2_service_message_finish(
        o2_time time, const char *service, const char *address, int tcp_flag)
{
    int addr_len = (int) strlen(address);
    // if service is provided, we'll prepend '/', so add 1 to string length
    int service_len = (service ? (int) strlen(service) + 1 : 0);
    // total service + address length with zero padding
    int addr_size = (service_len + addr_len + 4) & ~3;
    int types_len = msg_types.length;
    int types_size = (is_bundle ? 0 : ((types_len + 4) & ~3));
    int prefix = (is_bundle ? '#' : '/');
    int msg_size = sizeof(o2_time) + addr_size + types_size + msg_data.length;
    o2_message_ptr msg = o2_alloc_size_message(msg_size);
    if (!msg) return NULL;
    msg->next = NULL;
    msg->length = msg_size;
    msg->data.timestamp = time;
    char *dst = msg->data.address;
    int32_t *last_32 = (int32_t *) (dst + addr_size - sizeof(int32_t));
    *last_32 = 0; // fill last 32-bit word with zeros
    if (service) {
        *dst = prefix;
        memcpy(dst + 1, service, service_len);
        dst += service_len;
    }
    memcpy(dst, address, addr_len);
    dst = PTR(last_32 + 1);
    last_32 = (int32_t *) (dst + types_size - sizeof(int32_t));
    *last_32 = 0; // fil last 32-bit word with zeros
    // if building a bundle, types will be ',', and types will be
    // copied to the message, but types will be overwritten by the
    // first message of the bundle (or if the bundle has zero messages,
    // the type string will be written after the end of the message,
    // (and yes, there is room because small messages are allocated
    // from a list of fixed-size buffers).
    memcpy(dst, msg_types.array, types_len);
    dst += types_size;
    memcpy(dst, msg_data.array, msg_data.length);
    msg->tcp_flag = tcp_flag;
    return msg;
}


// ------- ADDENDUM: FUNCTIONS TO BUILD OSC BUNDLE FROM O2 BUNDLE ----

int o2_add_bundle_head(int64_t time)
{
    message_check_length(16);
    memcpy(msg_data.array + msg_data.length, "#bundle", 8);
#if IS_LITTLE_ENDIAN
    time = swap64(time);
#endif
    *((int64_t *) (msg_data.array + msg_data.length + 8)) = time;
    msg_data.length += 16;
    return O2_SUCCESS;
}


int *o2_msg_len_ptr()
{
    message_check_length(sizeof(int32_t));
    msg_data.length += sizeof(int32_t);
    return (int *) (msg_data.array + msg_data.length - sizeof(int32_t));
}


int o2_set_msg_length(int32_t *msg_len_ptr)
{
    int32_t len = (msg_data.array + msg_data.length) - PTR(msg_len_ptr + 1);
#if IS_LITTLE_ENDIAN
    len = swap32(len);
#endif
    *msg_len_ptr = len;
    return O2_SUCCESS;
}


int o2_add_raw_bytes(int32_t len, char *bytes)
{
    message_check_length(len);
    memcpy(msg_data.array + msg_data.length, bytes, len);
    msg_data.length += len;
    return O2_SUCCESS;
}


char *o2_msg_data_get(int32_t *len_ptr)
{
    *len_ptr = msg_data.length;
    return PTR(msg_data.array);
}


// ------- PART 4 : MESSAGE DECONSTRUCTION FUNCTIONS -------
// These functions build o2_argv and the data these pointers reference
// 
// For deconstruction, the "deluxe" result is an argument vector
// (argv) consisting of (essentially) one pointer per argument.
// These argv pointers point into the message when no type
// conversion is required, and into an argument data buffer when
// data must be copied from the message and converted. (We do not
// convert data in place because the message must be retained for
// possible delivery to another handler.)
//
// To simplify deconstruction, we allocate two buffers: One is
// for the argument vector (pointers). The other is for argument
// data. The problem is further simplified by allocating space for
// the worst case based on the total message length. The worst case
// number of arguments is message length / 4 assuming parameters
// take at least 4 bytes. Thus, the size for argv must be:
//   (length/4) * sizeof(o2_arg_ptr)
// However, vectors could have zero length yet have an arg
// vector element. This could be represented in the message as
// "[]..." so we should allocate 4 times the message length for
// the arg vector. To tighten this bound, notice that we only need
// 4 times the type string length for arrays and twice the
// remaining message length for vectors.
//
// The worst case data size happens when data is coerced to
// 64-bits, e.g. doubles, from 32-bit ints or floats. Another
// possibility is coercion from 0-length arrays to 0-length vectors.
// The type strings would be "[][]..." taking two bytes per element.
// The vectors take a length count and pointer, which could be
// 16 bytes, resulting in a factor of 8 expansion. We can cut this
// down by storing only the length (a 32-bit int) when the length is
// zero, again limiting the expansion factor to 2. If the arrays
// have one element, e.g. "[f][f]...", we require 7 bytes per array.
// If these are converted to vectors of doubles, we need 24 bytes
// per vector (assuming 4 bytes of padding after the 4-byte length
// plus a pointer to one double and the double itself. Here, the
// memory requirement is 24/7 times the message length. Again, we
// can tighten the bound: upper bounds are  24/3 times the size of
// the type string, and 24/4 times the size of the remaining
// message.
//
// The main motivation to pre-allocate storage before unpacking
// messages is that vectors can have pointers to coerced data
// so if we have to reallocate data, we have to scan the coerced
// data and adjust the pointers. This in turn, requires that we
// retain the types of all the arg vectors, requiring even more
// allocation and bookkeeping.

// State and macros for extracting data. A vector is requested by
// passing 'v' to o2_get_next(), an o2_arg_ptr with the vector
// length is returned. Then pass an element type code, one of "ihfd"
// to o2_get_next(). The same o2_arg_ptr will be returned, but this
// time the pointer will be valid (if the length is non-zero).
//
// For arrays, pass in '[', which returns o2_got_start_array if an
// array can be returned. Then pass in type codes for each element
// and an o2_arg_ptr for that element will be returned. Finally,
// pass in ']' and o2_get_end_array will be returned if you are at
// the end of an array or vector (otherwise NULL is returned to
// indicate error, as usual).

static o2_msg_data_ptr mx_msg = NULL; // the message we are extracting from
static char *mx_types = NULL;         // pointer to the type codes
static char *mx_type_next = NULL;     // pointer to the next type code
static char *mx_data_next = NULL;     // pointer to the next data item in mx_msg
static char *mx_barrier = NULL;       // pointer to end of message
static int mx_vector_to_vector_pending = FALSE; // expecting vector element
// type code, will return a whole vector
static int mx_array_to_vector_pending = FALSE;  // expecting vector element
// type code, will return whole vector from array elements
static int mx_vector_to_array = FALSE;   // when non-zero, we are extracting vector
// elements as array elements. The value will be one of "ihfd" depending
// on the vector element type
static int mx_vector_remaining = 0;  // when mx_vector_to_array is set, this
// counts how many vector elements remain to be retrieved

// macros to extract data:
// define functions to read different types from mx_data_next and increment it
#define MX_TYPE(fn, typ) typ fn() { typ x = *((typ *) mx_data_next);    \
        mx_data_next += sizeof(typ); return x; }

MX_TYPE(rd_float, float)
MX_TYPE(rd_double, double)
MX_TYPE(rd_int32, int32_t)
MX_TYPE(rd_int64, int64_t)

#define MX_FLOAT (*((float *) mx_data_next))
#define MX_DOUBLE (*((double *) mx_data_next))
#define MX_INT32 (*((int32_t *) mx_data_next))
#define MX_INT64 (*((int64_t *) mx_data_next))

#define MX_SKIP(n) mx_data_next += ((n) + 3) & ~3


// ------- PART 5 : GENERAL MESSAGE FUNCTIONS -------

/// a free list of 240-byte (MESSAGE_DEFAULT_SIZE) o2_messages
o2_message_ptr message_freelist = NULL;

// get a free message of size MESSAGE_DEFAULT_SIZE
static o2_message_ptr message_alloc()
{
    o2_message_ptr msg;
    if (!message_freelist) {
        msg = (o2_message_ptr) o2_malloc(MESSAGE_DEFAULT_SIZE);
        // printf("new msg %p\n", msg);
        msg->allocated = MESSAGE_ALLOCATED_FROM_SIZE(MESSAGE_DEFAULT_SIZE);
        MSG_ZERO_END(msg, MESSAGE_DEFAULT_SIZE);
    } else {
        msg = message_freelist;
        message_freelist = message_freelist->next;
        msg->length = 0;
    }
    return msg;
}


void o2_message_free(o2_message_ptr msg)
{
    assert(msg->length != -1);  // check if message is already freed
    msg->length = -1;
    if (msg->allocated == MESSAGE_ALLOCATED_FROM_SIZE(MESSAGE_DEFAULT_SIZE)) {
        msg->next = message_freelist;
        message_freelist = msg;
    } else {
        O2_FREE(msg);
    }
}


void o2_message_list_free(o2_message_ptr msg)
{
    while (msg) {
        o2_message_ptr next = msg->next;
        o2_message_free(msg);
        msg = next;
    }
}


o2_message_ptr o2_alloc_size_message(int size)
{
    if (size <= MESSAGE_ALLOCATED_FROM_SIZE(MESSAGE_DEFAULT_SIZE)) {
        // standard pre-allocated message is big enough so use one */
        return message_alloc();
    } else {
        o2_message_ptr msg = (o2_message_ptr)
            o2_malloc(MESSAGE_SIZE_FROM_ALLOCATED(size));
        msg->allocated = size;
        return msg;
    }
}


int o2_strsize(const char *s)
{
    // coerce to int to avoid compiler warning, O2 messages can't be that long
    return (int) ((strlen(s) + 4) & ~3);
}

// o2_blob_new - allocate a blob
//
o2_blob_ptr o2_blob_new(uint32_t size)
{
    // allocate space for length and extend to word boundary:
    int64_t needed = WORD_OFFSET(sizeof(uint32_t) + size + 3);
    if (needed > 0xFFFFFF00) { // allow almost 2^32 byte blobs
        return NULL; // but leave a little extra room
    }
    // int64_t could be bigger than size_t. Avoid compiler warning by coercing:
    o2_blob_ptr blob = (o2_blob_ptr) O2_MALLOC((size_t) needed);
    if (blob) {
        // coerce to avoid compiler warning; we tested so this will not overflow
        blob->size = (int) needed;
    }
    return blob;
}


#ifdef VALIDATION_FUNCTIONS
// o2_validate_string - test if data is a valid string whose
//     representation is less than or equal to size
// returns length of representation (including all zero padding)
//

ssize_t o2_validate_string(void *data, ssize_t size)
{
    ssize_t i = 0, len = 0;
    char *pos = data;
    
    if (size < 0) {
        return -O2_ESIZE;       // invalid size
    }
    for (i = 0; i < size; ++i) {
        if (pos[i] == '\0') {
            len = 4 * (i / 4 + 1);
            break;
        }
    }
    if (0 == len) {
        return -O2_ETERM;       // string not terminated
    }
    if (len > size) {
        return -O2_ESIZE;       // would overflow buffer
    }
    for (; i < len; ++i) {
        if (pos[i] != '\0') {
            return -O2_EPAD;    // non-zero char found in pad area
        }
    }
    return len;
}

ssize_t o2_validate_blob(void *data, ssize_t size)
{
    ssize_t i, end, len;
    uint32_t dsize;
    char *pos = (char *) data;
    
    if (size < 0) {
        return -O2_ESIZE;       // invalid size
    }
    dsize = ntohl(*(uint32_t *)data);
    if (dsize > O2_MAX_MSG_SIZE) {      // avoid int overflow in next step
        return -O2_ESIZE;
    }
    end = sizeof(uint32_t) + dsize;     // end of data
    len = 4 * ((end + 3) / 4);  // full padded size
    if (len > size) {
        return -O2_ESIZE;       // would overflow buffer
    }
    for (i = end; i < len; ++i) {
        if (pos[i] != '\0') {
            return -O2_EPAD;    // non-zero char found in pad area
        }
    }
    return len;
}

ssize_t o2_validate_bundle(void *data, ssize_t size)
{
    ssize_t len = 0, remain = size;
    char *pos = data;
    ssize_t elem_len;
    len = o2_validate_string(data, size);
    if (len < 0) {
        return -O2_ESIZE;       // invalid size
    }
    if (!streql(data, "#bundle")) {
        return -O2_EINVALIDBUND;        // not a bundle
    }
    pos += len;
    remain -= len;
    
    // time tag
    if (remain < 8) {
        return -O2_ESIZE;
    }
    pos += 8;
    remain -= 8;
    
    while (remain >= 4) {
        elem_len = ntohl(*((uint32_t *)pos));
        pos += 4;
        remain -= 4;
        if (elem_len > remain) {
            return -O2_ESIZE;
        }
        pos += elem_len;
        remain -= elem_len;
    }
    if (0 != remain) {
        return -O2_ESIZE;
    }
    return size;
}

#endif // VALIDATION_FUNCTIONS

#define PREPARE_TO_ACCESS(typ) \
    char *end = data_next + sizeof(typ); \
    if (end > end_of_msg) return O2_INVALID_MSG;

/* convert endianness of a message */
int o2_msg_swap_endian(o2_msg_data_ptr msg, int is_host_order)
{
    char *types = O2_MSG_TYPES(msg);
    int types_len = (int) strlen(types);
    char *data_next = WORD_ALIGN_PTR(types + types_len + 4);

    int64_t i64_time = *(int64_t *)(&(msg->timestamp));
    i64_time = swap64(i64_time);
    msg->timestamp = *(o2_time *)(&i64_time);
    
    if (IS_BUNDLE(msg)) {
        FOR_EACH_EMBEDDED(msg,
            int32_t *len_ptr = ((int32_t *) embedded) - 1;
            if (is_host_order) len = *len_ptr;
            *len_ptr = swap32(*len_ptr);
            if (!is_host_order) len = *len_ptr;
            if (PTR(msg) + len > end_of_msg) {
                return O2_FAIL;
            }
            o2_msg_swap_endian(embedded, is_host_order));
        return O2_SUCCESS;
    }
    // do not write beyond barrier (message may be malformed)
    char *end_of_msg = PTR(msg) + MSG_DATA_LENGTH(msg);
    while (*types) {
        if (data_next >= end_of_msg) {
            return O2_FAIL;
        }
        switch (*types) {
            case O2_INT32:
            case O2_MIDI:
            case O2_FLOAT:
            case O2_CHAR: {
                PREPARE_TO_ACCESS(int32_t);
                int32_t i = *(int32_t *)data_next;
                *(int32_t *)data_next = swap32(i);
                data_next = end;
                break;
            }
            case O2_BLOB: {
                int32_t size;
                PREPARE_TO_ACCESS(int32_t);
                // this is a bit tricky: size gets the
                // blob length field either before swapping or after
                // swapping, depending on whether the message starts
                // out in host order or not.
                int32_t *len_ptr = (int32_t *) data_next;
                if (is_host_order) size = *len_ptr;
                *len_ptr = swap32(*len_ptr);
                if (!is_host_order) size = *len_ptr;
                // now skip the blob data
                end += size;
                if (end > end_of_msg) return O2_INVALID_MSG;
                data_next = end;
                break;
            }
            case O2_TIME:
            case O2_INT64:
            case O2_DOUBLE: {
                PREPARE_TO_ACCESS(int64_t);
                int64_t i = *(int64_t *)data_next;
                *(int64_t *)data_next = swap64(i);
                data_next = end;
                break;
            }
            case O2_STRING:
            case O2_SYMBOL: {
                char *end = data_next + o2_strsize(data_next);
                if (end > end_of_msg) return O2_INVALID_MSG;
                data_next = end;
                break;
            }
            case O2_TRUE:
            case O2_FALSE:
            case O2_NIL:
            case O2_INFINITUM:
                /* these are fine, no data to modify */
                break;
            case O2_VECTOR: {
                PREPARE_TO_ACCESS(int32_t);
                int32_t *len_ptr = (int32_t *) data_next;
                int len;
                if (is_host_order) len = *len_ptr;
                *len_ptr = swap32(*len_ptr);
                if (!is_host_order) len = *len_ptr;
                data_next = end;
                // now test for vector data within end_of_msg
                end += len;
                if (end > end_of_msg) return O2_INVALID_MSG;
                // swap each vector element
                len /= 4; // assuming 32-bit elements
                char vtype = *types++;
                if (vtype == O2_DOUBLE || vtype == O2_INT64) {
                    len /= 2; // half as many elements if they are 64-bits
                }
                for (int i = 0; i < len; i++) { // for each vector element
                    if (i > 0) printf(" ");
                    if (vtype == O2_INT32 || vtype == O2_FLOAT) {
                        *(int32_t *)data_next =
                                swap32(*(int32_t *)data_next);
                        data_next += sizeof(int32_t);
                    } else if (vtype == O2_INT64 || vtype == O2_DOUBLE) {
                        *(int64_t *)data_next =
                                swap64(*(int64_t *)data_next);
                        data_next += sizeof(int64_t);
                    }
                }
                break;
            }
            default:
                fprintf(stderr,
                        "O2 warning: unhandled type '%c' at %s:%d\n", *types,
                        __FILE__, __LINE__);
                return O2_INVALID_MSG;
        }
        types++;
    }
    return O2_SUCCESS;
}


int o2_message_build(o2_message_ptr *msg, o2_time timestamp,
                     const char *service_name, const char *path,
                     const char *typestring, int tcp_flag, va_list ap)
{
    o2_send_start();
    
    // add data, a NULL typestring or "" means "no arguments"
    while (typestring && *typestring) {
        switch (*typestring++) {
            case O2_INT32: // get int in case int32 was promoted to int64
                o2_add_int32(va_arg(ap, int));
                break;
                
            case O2_FLOAT:
                o2_add_float((float) va_arg(ap, double));
                break;
                
            case O2_SYMBOL:
                o2_add_symbol(va_arg(ap, char *));
                break;
                
            case O2_STRING: {
                char *string = va_arg(ap, char *);
                o2_add_string(string);
#ifndef USE_ANSI_C
                if (string == (char *) O2_MARKER_A) {
                    fprintf(stderr,
                            "o2 error: o2_send or o2_message_add called with "
                            "invalid string pointer, probably arg mismatch.\n");
                }
#endif
                break;
            }
                
            case O2_BLOB:
                // argument should be a pointer to an o2_blob!
                o2_add_blob(va_arg(ap, o2_blob_ptr));
                break;
                
            case O2_INT64:
                o2_add_int64(va_arg(ap, int64_t));
                break;
                
            case O2_TIME:
                o2_add_time(va_arg(ap, double));
                break;
                
            case O2_DOUBLE:
                o2_add_double(va_arg(ap, double));
                break;
                
            case O2_CHAR:
                o2_add_char(va_arg(ap, int));
                break;
                
            case O2_MIDI:
                o2_add_midi(va_arg(ap, uint8_t *));
                break;
                
            case O2_BOOL:
                o2_add_bool(va_arg(ap, int));
                break;
                
            case O2_TRUE:
            case O2_FALSE:
            case O2_NIL:
            case O2_INFINITUM:
                add_type(typestring[-1]);
                break;
                
                // fall through to unknown type
            default: {
                fprintf(stderr, "o2 warning: unknown type '%c'\n", *(typestring - 1));
                break;
            }
        }
    }
    
#ifndef USE_ANSI_C
    void *i = va_arg(ap, void *);
    if ((((unsigned long) i) & 0xFFFFFFFFUL) !=
        (((unsigned long) O2_MARKER_A) & 0xFFFFFFFFUL)) {
        // bad format/args
        goto error_exit;
    }
    i = va_arg(ap, void *);
    if ((((unsigned long) i) & 0xFFFFFFFFUL) !=
        (((unsigned long) O2_MARKER_B) & 0xFFFFFFFFUL)) {
        goto error_exit;
    }
#endif
    va_end(ap);
    *msg = o2_service_message_finish(timestamp, service_name, path, tcp_flag);
    return (*msg ? O2_SUCCESS : O2_FAIL);
  error_exit:
    fprintf(stderr, "o2 error: o2_send or o2_send_cmd called with mismatching types and data.\n");
    va_end(ap);
    return O2_BAD_ARGS;    
}



// state and macros for message construction
// TODO CLEANUP:
// use WR_ macros to write data, so 
//     start is where the data starts, this is 1/5 of the way through
//     the allocated message memory, assuming most arguments are floats
//     and int32, so we need about 4x as much memory for data.
// next is where the data ends, where to append the next argument.
// TODO REMOVE static char *mc_barrier = NULL;  // end of allocated message data


int o2_send_finish(o2_time time, const char *address, int tcp_flag)
{
    o2_message_ptr msg = o2_message_finish(time, address, tcp_flag);
    if (!msg) return O2_FAIL;
    return o2_message_send2(msg, TRUE);
}


/// get ready to extract args with o2_get_next
/// returns length of type string (not including ',') in message
//
int o2_extract_start(o2_msg_data_ptr msg)
{
    mx_msg = msg;
    // point temp_type_end to the first type code byte.
    // skip over padding and ','
    mx_types = O2_MSG_TYPES(msg);
    mx_type_next = mx_types;
    
    // argv needs 4 * type string length + 2 * remaining length
    // coerce to int to avoid compiler warning; o2 messages can't be that long
    int types_len = (int) strlen(mx_types);
    // mx_types + types_len points to the end-of-typestring byte and there can
    // be up to 3 more zero-pad bytes to the next word boundary
    mx_data_next = WORD_ALIGN_PTR(mx_types + types_len + 4);
    // now, mx_data_next points to the first byte of real data
    // subtract pointer to get length of real data, coerce to int to avoid
    // compiler warning; message cannot be that big
    int msg_data_len = (int) (PTR(msg) + MSG_DATA_LENGTH(msg) - mx_data_next);
    // add 2 for safety
    int argv_needed = types_len * 4 + msg_data_len * 2 + 2;
    
    // o2_arg_data needs at most 24/3 times type string and at most 24/4
    // times remaining data.
    int arg_needed = types_len * 8;
    if (arg_needed > msg_data_len * 6) arg_needed = msg_data_len * 6;
    arg_needed += 16; // add some space for safety
    need_argv(argv_needed, arg_needed);
    
    mx_barrier = WORD_ALIGN_PTR(PTR(msg) + MSG_DATA_LENGTH(msg));
    // use WR_ macros to write coerced parameters
    
    mx_vector_to_array = FALSE;
    mx_vector_remaining = 0;
    mx_vector_to_vector_pending = FALSE;
    
    return types_len;
}


static o2_arg_ptr convert_int(char to_type, int64_t i, int siz)
{
    o2_arg_ptr rslt = ARG_NEXT;
    switch (to_type) {
        case O2_INT32:
            // coerce to int to avoid compiler warning; O2 can in fact lose
            // data coercing from type O2_INT64 to O2_INT32
            ARG_DATA(rslt, int32_t, (int32_t) i);
            break;
        case O2_INT64:
            ARG_DATA(rslt, int64_t, i);
            break;
        case O2_FLOAT:
            // coerce to float to avoid compiler warning; O2 can in fact lose
            // data coercing from type O2_INT64 to O2_FLOAT
            ARG_DATA(rslt, float, (float) i);
            break;
        case O2_DOUBLE:
        case O2_TIME:
            // coerce to double to avoid compiler warning; O2 can in fact lose
            // data coercing from type O2_INT64 to O2_DOUBLE:
            ARG_DATA(rslt, double, (double) i);
            break;
        case O2_BOOL:
            ARG_DATA(rslt, int32_t, i != 0);
            break;
        case O2_TRUE:
            if (!i) rslt = NULL;
            break;
        case O2_FALSE:
            if (i) rslt = NULL;
            break;
        default:
            return NULL;
    }
    return rslt;
}


static o2_arg_ptr convert_float(char to_type, double d, int siz)
{
    o2_arg_ptr rslt = (o2_arg_ptr) (o2_arg_data.array + o2_arg_data.length);
    switch (to_type) {
        case O2_INT32:
            // coerce to int32_t to avoid compiler warning; O2 can in fact lose
            // data coercing from type O2_DOUBLE to O2_INT32
            ARG_DATA(rslt, int32_t, (int32_t) d);
            break;
        case O2_INT64:
            // coerce to int64_t to avoid compiler warning; O2 can in fact lose
            // data coercing from type O2_DOUBLE to O2_INT64
            ARG_DATA(rslt, int64_t, (int64_t) d);
            break;
        case O2_FLOAT:
            // coerce to float to avoid compiler warning; O2 can in fact lose
            // data coercing from type O2_DOUBLE to O2_FLOAT
            ARG_DATA(rslt, float, (float) d);
            break;
        case O2_DOUBLE:
        case O2_TIME:
            ARG_DATA(rslt, double, d);
            break;
        case O2_BOOL:
            ARG_DATA(rslt, int32_t, d != 0.0);
            break;
        case O2_TRUE:
            if (d == 0.0) rslt = NULL;
            break;
        case O2_FALSE:
            if (d != 0.0) rslt = NULL;
            break;
        default:
            return NULL;
    }
    return rslt;
}


static o2_arg ea, sa;
o2_arg_ptr o2_got_end_array = &ea;
o2_arg_ptr o2_got_start_array = &sa;


/// get the next argument from the message. If the to_type
/// does not match the actual type in the message, convert
/// if possible; otherwise, return NULL for the o2_arg_ptr.
/// Note that if coerce_flag was false, the type checking
/// will have compared types for an exact match, so if we
/// make it this far and we are constructing argv, then
/// no type coercion will take place (and the tests for type
/// matching are all redundant because they will all fail).
/// If client code is calling this, then there is no way to
/// turn off type coercion except that you can compare your
/// desired to_type to the character in the actual type
/// string and if there is no match, do not call o2_get_next().
///
// todo: vector to array
//       array to vector
//       vector to vector - done
//       array to array
o2_arg_ptr o2_get_next(char to_type)
{
    o2_arg_ptr rslt = (o2_arg_ptr) mx_data_next;
    if (mx_type_next >= mx_barrier) return NULL; // overrun
    if (*mx_type_next == 0) return NULL; // no more args, end of type string
    if (mx_vector_to_vector_pending) {
        mx_vector_to_vector_pending = FALSE;
        // returns pointer to a vector descriptor with typ, len, and vector
        //   address; this descriptor is always allocated in o2_arg_data
        // mx_data_next points to vector in message
        // allowed types for target are i, h, f, t, d
        rslt = ARG_NEXT;
        ARG_DATA_USED(o2_arg);
        // get pointer to the vector (pointed to type doesn't actually matter)
        // so this code is common to all the different type cases
        if (to_type == *mx_type_next) {
            rslt->v.vi = (int32_t *) mx_data_next;
        } else {
            rslt->v.vi = (int32_t *) ARG_NEXT;
        }
        if (mx_data_next + rslt->v.len > mx_barrier) {
            mx_vector_to_vector_pending = FALSE;
            return NULL; // bad message
        }
        switch (*mx_type_next++) { // switch on actual (in message) type
            case O2_INT32:
                rslt->v.len >>= 2; // byte count / 4
                if (to_type != O2_INT32) {
                    for (int i = 0; i < rslt->v.len; i++) {
                        if (!convert_int(to_type, MX_INT32, sizeof(int32_t)))
                            return NULL;
                        mx_data_next += sizeof(int32_t);
                    }
                } else {
                    MX_SKIP(sizeof(int32_t) * rslt->v.len);
                }
                break;
            case O2_INT64:
                rslt->v.len >>= 3; // byte count / 8
                if (to_type != O2_INT64) {
                    // we'll need len int64s of free space
                    for (int i = 0; i < rslt->v.len; i++) {
                        if (!convert_int(to_type, MX_INT64, sizeof(int32_t)))
                            return NULL;
                        mx_data_next += sizeof(int64_t);
                    }
                } else {
                    MX_SKIP(sizeof(int64_t) * rslt->v.len);
                }
                break;
            case O2_FLOAT:
                rslt->v.len >>= 2; // byte count / 4
                if (to_type != O2_FLOAT) {
                    for (int i = 0; i < rslt->v.len; i++) {
                        if (!convert_float(to_type, MX_FLOAT, sizeof(float)))
                            return NULL;
                        mx_data_next += sizeof(float);
                    }
                } else {
                    MX_SKIP(sizeof(float) * rslt->v.len);
                }
                break;
            case O2_DOUBLE:
                rslt->v.len >>= 3; // byte count / 8
                if (to_type != O2_DOUBLE) {
                    for (int i = 0; i < rslt->v.len; i++) {
                        if (!convert_float(to_type, MX_DOUBLE, sizeof(double)))
                            return NULL;
                        mx_data_next += sizeof(double);
                    }
                } else {
                    MX_SKIP(sizeof(double) * rslt->v.len);
                }
                break;
            default:
                return NULL;
                break;
        }
        o2_argc--; // argv already has pointer to vector
    } else if (mx_vector_to_array) {
        // return vector elements as array elements
        if (to_type == O2_ARRAY_END) {
            if (mx_vector_remaining == 0) {
                rslt = o2_got_end_array;
                mx_vector_to_array = FALSE;
            } else {
                return NULL;
            }
        } else {
            int siz = ((mx_vector_to_array == 'h' ||
                        mx_vector_to_array == 'd') ? 8 : 4);
            mx_vector_remaining -= siz;
            if (mx_vector_remaining < 0) {
                return NULL; // perhaps message was invalid
            }
        }
        switch (mx_vector_to_array) {
            case O2_INT32:
                if (to_type != O2_INT32) {
                    rslt = convert_int(to_type, MX_INT32, sizeof(int32_t));
                }
                mx_data_next += sizeof(int32_t);
                break;
            case O2_INT64:
                if (to_type != O2_INT64) {
                    rslt = convert_int(to_type, MX_INT64, sizeof(int64_t));
                }
                mx_data_next += sizeof(int64_t);
                break;
            case O2_FLOAT:
                if (to_type != O2_FLOAT) {
                    rslt = convert_float(to_type, MX_FLOAT, sizeof(float));
                }
                mx_data_next += sizeof(float);
                break;
            case O2_DOUBLE:
                if (to_type != O2_DOUBLE) {
                    rslt = convert_float(to_type, MX_DOUBLE, sizeof(double));
                }
                mx_data_next += sizeof(double);
                break;
            default: // this happens when we reach the end of the vector
                break;
        }
        if (mx_data_next > mx_barrier) {
            mx_vector_to_array = FALSE;
            return NULL; // badly formatted message
        }
    } else if (mx_array_to_vector_pending) { // to_type is desired vector type
        // array types are in mx_type_next
        rslt = ((o2_arg_ptr) ARG_NEXT) - 1; // already allocated the vector header
        o2_argv_data.length--; // "vi" should get just one element in the arg vector
        //     we already added one and will add another (below), so decrement length
        //     so that rslt will not be written to a new location
        // of size sizeof(o2_arg), so -1 gets us back to the address of the header
        rslt->v.vi = (int32_t *) ARG_NEXT;
        rslt->v.typ = to_type; // now we know what the element type will be
        while (*mx_type_next != O2_ARRAY_END) {
            switch (*mx_type_next++) {
                case O2_INT32:
                    convert_int(to_type, MX_INT32, sizeof(int32_t));
                    mx_data_next += sizeof(int32_t);
                    break;
                case O2_INT64:
                    convert_int(to_type, MX_INT64, sizeof(int64_t));
                    mx_data_next += sizeof(int64_t);
                    break;
                case O2_FLOAT:
                    convert_float(to_type, MX_FLOAT, sizeof(float));
                    mx_data_next += sizeof(float);
                    break;
                case O2_DOUBLE:
                    convert_float(to_type, MX_DOUBLE, sizeof(double));
                    mx_data_next += sizeof(double);
                    break;
                default:
                    return NULL; // bad type string (no ']') or bad types
            }
            rslt->v.len++;
            if (mx_data_next > mx_barrier) {
                mx_array_to_vector_pending = FALSE;
                return NULL; // badly formatted message
            }
        }
        mx_array_to_vector_pending = FALSE;
    } else {
        switch (*mx_type_next++) {
            case O2_INT32:
                if (to_type != O2_INT32) {
                    rslt = convert_int(to_type, MX_INT32, sizeof(int32_t));
                }
                mx_data_next += sizeof(int32_t);
                break;
            case O2_TRUE:
                if (to_type != O2_TRUE) {
                    rslt = convert_int(to_type, 1, sizeof(int32_t));
                }
                break;
            case O2_FALSE:
                if (to_type != O2_TRUE) {
                    rslt = convert_int(to_type, 0, sizeof(int32_t));
                }
                break;
            case O2_BOOL:
                if (to_type != O2_BOOL) {
                    rslt = convert_int(to_type, MX_INT32, sizeof(int32_t));
                }
                mx_data_next += sizeof(int32_t);
                break;
            case O2_FLOAT:
                if (to_type != O2_FLOAT) {
                    rslt = convert_float(to_type, MX_FLOAT, sizeof(float));
                }
                mx_data_next += sizeof(float);
                break;
            case O2_SYMBOL:
            case O2_STRING:
                if (to_type != O2_SYMBOL && to_type != O2_STRING) {
                    rslt = NULL; // type error
                } // otherwise the requested type is suitable
                MX_SKIP(strlen(mx_data_next) + 1); // add one for end-of-string
                break;
            case O2_CHAR:
                if (to_type != O2_CHAR) {
                    rslt = NULL;
                }
                mx_data_next += sizeof(int32_t); // char stored as int32_t
                break;
            case O2_BLOB:
                if (to_type != O2_BLOB) {
                    rslt = NULL; // type mismatch
                }
                MX_SKIP(sizeof(uint32_t) + rslt->b.size);
                break;
            case O2_INT64:
                if (to_type != O2_INT64) {
                    rslt = convert_int(to_type, MX_INT64, sizeof(int64_t));
                }
                mx_data_next += sizeof(int64_t);
                break;
            case O2_DOUBLE:
            case O2_TIME:
                if (to_type != O2_DOUBLE && to_type != O2_TIME) {
                    rslt = convert_float(to_type, MX_DOUBLE, sizeof(double));
                } // otherwise the requested type is suitable
                mx_data_next += sizeof(double);
                break;
            case O2_MIDI:
                if (to_type != O2_MIDI) {
                    rslt = NULL; // type mismatch
                }
                MX_SKIP(4);
                break;
            case O2_NIL:
            case O2_INFINITUM:
                if (to_type != mx_type_next[-1]) {
                    rslt = NULL;
                }
                break;
            case O2_ARRAY_START:
                if (to_type == O2_ARRAY_START) {
                    rslt = o2_got_start_array;
                } else if (to_type == O2_VECTOR) {
                    // see if we can extract a vector next time
                    // when we get an element type
                    mx_array_to_vector_pending = TRUE;
                    rslt = (o2_arg_ptr) ARG_NEXT;
                    ARG_DATA_USED(o2_arg);
                    // initially, the vector type is the type of the first element
                    // in the array, or double if the array is empty
                    rslt->v.typ = *mx_type_next;
                    if (rslt->v.typ == ']') rslt->v.typ = 'd';
                    rslt->v.len = 0; // unkknown
                    rslt->v.vi = NULL; // pointer to data is not valid yet
                } else {
                    rslt = NULL;
                }
                break;
            case O2_ARRAY_END:
                if (to_type == O2_ARRAY_END) {
                    rslt = o2_got_end_array;
                } else {
                    rslt = NULL;
                }
                break;
            case O2_VECTOR:
                if (to_type == O2_ARRAY_START) {
                    // extract the vector as array elements
                    mx_vector_to_array = *mx_type_next++;
                    mx_vector_remaining = rd_int32();
                    // assuming 'v' was followed by a type, we have a vector
                    rslt = mx_vector_to_array ? o2_got_start_array : NULL;
                } else if (to_type == O2_VECTOR) {
                    // next call to o2_get_next() will get special processing
                    mx_vector_to_vector_pending = TRUE;
                    rslt = (o2_arg_ptr) ARG_NEXT;
                    // do not call ARG_DATA_USED() because we will get
                    //    this address again on next call to o2_get_next()
                    rslt->v.typ = *mx_type_next;
                    // do not increment mx_type_next because we will use
                    //    it again (and increment on next call to o2_get_next()
                    rslt->v.len = rd_int32();
                    rslt->v.vi = NULL; // pointer to data is not valid yet
                } else {
                    rslt = NULL;
                }
                break;
            default: // could be O2_ARRAY_END or EOS among others
                fprintf(stderr, "O2 warning: unhandled OSC type '%c'\n",
                        *mx_type_next);
                return NULL;
        }
        if (mx_data_next > mx_barrier) {
            mx_data_next = mx_barrier; // which points to 4 zero bytes at end
            return NULL;         // of the message
        }
    }
    // This is equivalent to DA_APPEND, but since we've already allocated the
    // space, we don't need to check for space, and it would be an error if
    // we did expand the space because pointers would be wrong
    o2_argv_data.length++;
    o2_argv[o2_argc++] = rslt; // o2_argv is o2_argv_data.array as o2_arg_ptr.
    return rslt;
}


// o2_print_msg_data - print message as text to stdout
//
// It would be most convenient to use o2_extract_start() and o2_get_next()
// here, but this would overwrite extracted parameters if called from a
// message handler, so here we duplicate some code to pull parameters from
// messages (although the code is simple since there's no coercion).
//
static void o2_print_msg_data_2(o2_msg_data_ptr msg, int tcp_flag)
{
    int i;
    printf("%s @ %g", msg->address, msg->timestamp);
    if (tcp_flag >= 0) {
        printf(" by %s", tcp_flag ? "TCP" : "UDP");
    }
    if (msg->timestamp > 0.0) {
        if (msg->timestamp > o2_global_now) {
            printf(" (now+%gs)", msg->timestamp - o2_global_now);
        } else {
            printf(" (%gs late)", o2_global_now - msg->timestamp);
        }
    }
    
    if (IS_BUNDLE(msg)) {
        FOR_EACH_EMBEDDED(msg, printf(" <ELEM ");
                          o2_print_msg_data_2(embedded, -1);
                          printf(" >");
                          len = MSG_DATA_LENGTH(embedded))
        return;
    }

    char *types = O2_MSG_TYPES(msg);
    int types_len = (int) strlen(types);
    char *data_next = WORD_ALIGN_PTR(types + types_len + 4);
    
    while (*types) {
        switch (*types) {
            case O2_INT32:
                printf(" %d", *((int32_t *) data_next));
                data_next += sizeof(int32_t);
                break;
            case O2_FLOAT:
                printf(" %gf", *((float *) data_next));
                data_next += sizeof(float);
                break;
            case O2_STRING:
                printf(" \"%s\"", data_next);
                data_next += o2_strsize(data_next);
                break;
            case O2_BLOB: {
                int size = *((int32_t *) data_next);
                data_next += sizeof(int32_t);
                if (size > 12) {
                    printf(" (%d byte blob)", size);
                } else {
                    printf(" (");
                    for (i = 0; i < size; i++) {
                        if (i > 0) printf(" ");
                        printf("%#02x", (unsigned char) (data_next[i]));
                    }
                    printf(")");
                }
                data_next += ((size + 3) & ~3);
                break;
            }
            case O2_INT64:
                printf(" %lld", *((int64_t *) data_next));
                data_next += sizeof(int64_t);
                break;
            case O2_DOUBLE:
                printf(" %g", *((double *) data_next));
                data_next += sizeof(double);
                break;
            case O2_TIME:
                printf(" %gs", *((double *) data_next));
                data_next += sizeof(double);
                break;
            case O2_SYMBOL:
                printf(" '%s", data_next);
                data_next += o2_strsize(data_next);
                break;
            case O2_CHAR:
                printf(" '%c'", *((int32_t *) data_next));
                data_next += sizeof(int32_t);
                break;
            case O2_MIDI:
                printf(" <MIDI: ");
                for (i = 0; i < 4; i++) {
                    if (i > 0) printf(" "); 
                    printf("0x%02x", data_next[i]);
                }
                printf(">");
                data_next += 4;
                break;
            case O2_TRUE:
                printf(" #T");
                break;
            case O2_FALSE:
                printf(" #F");
                break;
            case O2_NIL:
                printf(" Nil");
                break;
            case O2_INFINITUM:
                printf(" Infinitum");
                break;
            case O2_ARRAY_START:
                printf(" [");
                break;
            case O2_ARRAY_END:
                printf(" ]");
                break;
            case O2_VECTOR: {
                int len = *((int32_t *) data_next);
                data_next += sizeof(int32_t);
                printf(" <");
                char vtype = *types++;
                for (i = 0; i < len; i++) {
                    if (i > 0) printf(" ");
                    if (vtype == O2_INT32) {
                        printf(" %d", *((int32_t *) data_next));
                        data_next += sizeof(int32_t);
                    } else if (vtype == O2_INT64) {
                        printf(" %lld", *((int64_t *) data_next));
                        data_next += sizeof(int64_t);
                    } else if (vtype == O2_FLOAT) {
                        printf(" %gf", *((float *) data_next));
                        data_next += sizeof(float);
                    } else if (vtype == O2_DOUBLE) {
                        printf(" %g", *((double *) data_next));
                        data_next += sizeof(double);
                    }
                    // note: vector of O2_TIME is not valid
                }
                break;
            }
            default:
                printf(" O2 WARNING: unhandled type: %c\n", *types);
                break;
        }
        types++;
    }
}


void o2_message_print(o2_message_ptr msg)
{
    o2_print_msg_data_2(&msg->data, msg->tcp_flag ? TRUE : FALSE);
    fflush(stdout);
}


void o2_msg_data_print(o2_msg_data_ptr msg)
{
    o2_print_msg_data_2(msg, -1);
    fflush(stdout);
}
