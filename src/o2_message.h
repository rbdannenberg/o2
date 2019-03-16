//
//  O2_message.h
//  O2
//
//  Created by 弛张 on 1/26/16.
//  Copyright © 2016 弛张. All rights reserved.
//

#ifndef o2_message_h
#define o2_message_h

extern thread_local o2_context_ptr o2_context;

#define MAX_SERVICE_LEN 64

#ifdef WIN32
#define ssize_t long long
#endif

/* MESSAGE EXTRACTION */
void o2_argv_initialize(void);

void o2_argv_finish(void);

/* MESSAGE CONSTRUCTION */
int o2_add_bundle_head(int64_t time);

int32_t *o2_msg_len_ptr(void);

int o2_set_msg_length(int32_t *msg_len_ptr);

int o2_add_raw_bytes(int32_t len, char *bytes);

char *o2_msg_data_get(int32_t *len_ptr);


/* GENERAL MESSAGE FUNCIONS */

#define IS_BUNDLE(msg)((msg)->address[0] == '#')

// Iterate over elements of a bundle. msg is an o2_msg_data_ptr, and
// code is the code to execute. When code is entered, embedded is an
// o2_msg_data_ptr pointing to each element of msg. code MUST assign
// the length of embedded to len. (This is not built-in because 
// embedded may be byte-swapped.) Generally, code will include a
// recursive call to process embedded, which may itself be a bundle.
//
#define FOR_EACH_EMBEDDED(msg, code)                     \
    char *end_of_msg = PTR(msg) + MSG_DATA_LENGTH(msg); \
    o2_msg_data_ptr embedded = (o2_msg_data_ptr) \
            ((msg)->address + o2_strsize((msg)->address) + sizeof(int32_t)); \
    while (PTR(embedded) < end_of_msg) { int32_t len; \
        code; \
        embedded = (o2_msg_data_ptr) (PTR(embedded) + len + sizeof(int32_t)); }


/* allocate message structure with at least size bytes in the data portion */
o2_message_ptr o2_alloc_size_message(int size);


/* free a message and all the messages it links to */
void o2_message_list_free(o2_message_ptr msg);


/* compute the size of a string including EOS and padding to next word */
int o2_strsize(const char *s);


/**
 * Convert endianness of a message
 *
 * @param msg The message
 *
 * @return O2_SUCCESS unless the message is malformed
 */
int o2_msg_swap_endian(o2_msg_data_ptr msg, int is_host_order);

int o2_message_build(o2_message_ptr *msg, o2_time timestamp,
                     const char *service_name,
                     const char *path, const char *typestring,
                     int tcp_flag, va_list ap);

/**
 * Print o2_msg_data to stdout
 *
 * @param msg The message to print
 */
void o2_msg_data_print(o2_msg_data_ptr msg);


/**
 * Print an O2 message to stdout
 *
 * @param msg The message to print
 */
void o2_message_print(o2_message_ptr msg);

#endif /* O2_message_h */
