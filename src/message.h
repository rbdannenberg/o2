/* message.h -- message creation */

/* Roger B. Dannenberg
 * April 2020
 */

#ifndef message_h
#define message_h

extern thread_local o2_ctx_ptr o2_ctx;

// you can OR these together to make message flags
#define O2_UDP_FLAG 0   // UDP, not TCP
#define O2_TCP_FLAG 1   // TCP, not UDP
#define O2_TAP_FLAG 2   // this is a message to a tap

#define MAX_SERVICE_LEN 64

#ifdef WIN32
#define ssize_t long long
#endif

/* MESSAGE EXTRACTION */
void o2_argv_initialize(void);

void o2_argv_finish(void);

/* MESSAGE CONSTRUCTION */
void o2_message_check_length(int needed);

#ifndef O2_NO_BUNDLES
int o2_add_bundle_head(int64_t time);
#endif

int32_t *o2_msg_len_ptr(void);

int o2_set_msg_length(int32_t *msg_len_ptr);

int o2_add_raw_bytes(int32_t len, char *bytes);

char *o2_msg_data_get(int32_t *len_ptr);

void o2_msg_data_print_2(o2_msg_data_ptr msg);


/* GENERAL MESSAGE FUNCIONS */

#ifndef O2_NO_BUNDLES
#define IS_BUNDLE(msg)((msg)->address[0] == '#')

// Iterate over elements of a bundle. msg is an o2_msg_data_ptr, and
// code is the code to execute. When code is entered, embedded is an
// o2_msg_data_ptr pointing to each element of msg. code MUST assign
// the length of embedded to len. (This is not built-in because 
// embedded may be byte-swapped.) Generally, code will include a
// recursive call to process embedded, which may itself be a bundle.
//
#define FOR_EACH_EMBEDDED(msg, code)                     \
    char *end_of_msg = O2_MSG_DATA_END(msg); \
    o2_msg_data_ptr embedded = (o2_msg_data_ptr) (o2_msg_data_types(msg) - 1); \
    while (PTR(embedded) < end_of_msg) { int32_t len; \
        code; \
        embedded = (o2_msg_data_ptr) (PTR(embedded) + len + sizeof(int32_t)); }
#endif

/* allocate message structure with at least size bytes in the data portion */
#define o2_message_new(size) ((o2_message_ptr) o2n_message_new(size))

void o2_message_list_free(o2_message_ptr msg);

/**
 * Convert endianness of a message
 *
 * @param msg The message
 *
 * @return O2_SUCCESS unless the message is malformed
 */
o2_err_t o2_msg_swap_endian(o2_msg_data_ptr msg, int is_host_order);

o2_err_t o2_message_build(o2_message_ptr *msg, o2_time timestamp,
                          const char *service_name,
                          const char *path, const char *typestring,
                          int tcp_flag, va_list ap);

#endif /* message_h */
