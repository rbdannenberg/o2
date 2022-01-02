/* message.h -- message creation */

/* Roger B. Dannenberg
 * April 2020
 */

#ifndef message_h
#define message_h

extern thread_local O2_context *o2_ctx;

// prevent infinite tap loops using a "time too live" algorithm:
// The value of 3 allows using a tap to implement "publish/subscribe"
// and then a debugger tapping the subscriber and then one more tap
// indirection for good measure. The problem with these arbitrary limits
// is anticipating and allowing for all applications. Since this value is
// stored in a char, the value can be as high as 127, but the risk of high
// values is that a cycle will actually cause messages to be forwarded
// all O2_MAX_TAP_FORWARDING times.
#define O2_MAX_TAP_FORWARDING 3

// you can OR these together to make message flags
#define O2_UDP_FLAG 0   // UDP, not TCP
#define O2_TCP_FLAG 1   // TCP, not UDP
#define O2_TAP_FLAG 2   // this is a message to a tap

#define MAX_SERVICE_LEN 64

#ifdef WIN32
#define ssize_t long long
#endif


/* MESSAGE CONSTRUCTION */

#ifndef O2_NO_BUNDLES
int o2_add_bundle_head(int64_t time);
#endif

int32_t *o2_msg_len_ptr(void);

int o2_set_msg_length(int32_t *msg_len_ptr);

int o2_add_raw_bytes(int32_t len, char *bytes);

char *o2_msg_data_get(int32_t *len_ptr);

void o2_msg_data_print_2(O2msg_data_ptr msg);


/* GENERAL MESSAGE FUNCIONS */

#ifndef O2_NO_BUNDLES
#define IS_BUNDLE(msg)((msg)->address[0] == '#')

// Iterate over elements of a bundle. msg is an O2msg_data_ptr, and
// code is the code to execute. When code is entered, embedded is an
// O2msg_data_ptr pointing to each element of msg. code MUST assign
// the length of embedded to len. (This is not built-in because 
// embedded may be byte-swapped.) Generally, code will include a
// recursive call to process embedded, which may itself be a bundle.
//
#define FOR_EACH_EMBEDDED(msg, code)                     \
    char *end_of_msg = O2_MSG_DATA_END(msg); \
    O2msg_data_ptr embedded = (O2msg_data_ptr) (o2_msg_data_types(msg) - 1); \
    while (PTR(embedded) < end_of_msg) { int32_t len; \
        code; \
        embedded = (O2msg_data_ptr) (PTR(embedded) + len + sizeof(int32_t)); }
#endif

/* allocate message structure with at least size bytes in the data portion */
#define o2_message_new(size) ((O2message_ptr) O2netmsg_new(size))

void o2_message_list_free(O2message_ptr *msg);

/**
 * Convert endianness of a message
 *
 * @param msg The message
 *
 * @return O2_SUCCESS unless the message is malformed
 */
O2err o2_msg_swap_endian(O2msg_data_ptr msg, int is_host_order);

O2err o2_message_build(O2message_ptr *msg, O2time timestamp,
                       const char *service_name,
                       const char *path, const char *typestring,
                       bool tcp_flag, va_list ap);

#endif /* message_h */
