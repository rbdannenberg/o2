//
//  o2_message.c
//  O2
//
//  Created by 弛张 on 1/26/16.
//  Copyright © 2016 弛张. All rights reserved.
//

#include "o2.h"
#include "o2_dynamic.h"
#include "o2_socket.h"
#include "o2_search.h"
#include "o2_internal.h"
#include "o2_message.h"
#include "o2_discovery.h"

/// end of message must be zero to prevent strlen from running off the
/// end of malformed message
#define MSG_ZERO_END(msg, siz) *((int32_t *) &((char *) (msg))[(siz) - 4]) = 0

/// a free list of 240-byte (MESSAGE_DEFAULT_SIZE) o2_messages
o2_message_ptr message_freelist = NULL;

o2_message_ptr alloc_message()
{
	o2_message_ptr msg;
	if (!message_freelist) {
		msg = (o2_message_ptr)o2_malloc(MESSAGE_DEFAULT_SIZE);
		msg->allocated = MESSAGE_ALLOCATED_FROM_SIZE(MESSAGE_DEFAULT_SIZE);
		MSG_ZERO_END(msg, MESSAGE_DEFAULT_SIZE);
	}
	else {
		msg = message_freelist;
		message_freelist = message_freelist->next;
	}
	msg->length = sizeof(double); // skip over timestamp, point to address
	return msg;
}

/// append val of type typ to msg
#define MESSAGE_APPEND(msg, typ, val) \
    *((typ *) (((char *) &((msg)->data)) + (msg)->length)) = (val);  \
    (msg)->length += sizeof(typ)

/// append len bytes of data to msg
#define MESSAGE_APPEND_DATA(msg, address, len) \
    memcpy(((char *) &((msg)->data)) + (msg)->length, (address), (len))

/// append len bytes of data to msg, and pad with 1 to 4 zeros
#define MESSAGE_APPEND_PAD_DATA(msg, address, len) { \
    int new_len = ((msg)->length + (len) + 4) & ~3; \
    *((int32_t *) (((char *) &((msg)->data)) + new_len - 4)) = 0; \
    memcpy(((char *) &((msg)->data)) + (msg)->length, (address), (len)); \
    (msg)->length = new_len; }

/// append 1 byte to msg, and pad with 3 zeros
#define MESSAGE_APPEND_PAD_BYTE(msg, byte) \
    *((int32_t *) (((char *) &((msg)->data)) + (msg)->length)) = 0;  \
    *(((char *) &((msg)->data)) + (msg)->length) = byte; \
    (msg)->length += 4

/// make sure there is room to add needed bytes
#define MESSAGE_CHECK_LENGTH(msg, needed)  \
    if ((msg)->allocated < (msg)->length + (needed)) \
        (msg) = alloc_bigger_message((msg), (needed))


void o2_free_message(o2_message_ptr msg)
{
	if (msg->allocated == MESSAGE_ALLOCATED_FROM_SIZE(MESSAGE_DEFAULT_SIZE)) {
		msg->next = message_freelist;
		message_freelist = msg;
	}
	else {
		o2_free(msg);
	}
}


o2_message_ptr alloc_bigger_message(o2_message_ptr msg, int needed)
{
	int new_allocated = msg->allocated * 2;
	needed += msg->length;
	// if doubling is still not big enough, make it even bigger.
	// In this case, we probably are adding a giant blob. If
	// anything comes after the blob, we'll get room for 2 giant
	// blobs, so for the case where there are just a few more
	// small things to add, allocate an extra 64 bytes.
	if (new_allocated < needed) new_allocated = needed + 64;
	int size = MESSAGE_SIZE_FROM_ALLOCATED(new_allocated);
	o2_message_ptr newmsg = (o2_message_ptr)o2_malloc(size);
	newmsg->allocated = new_allocated;
	newmsg->length = msg->length;
	memcpy(&(newmsg->data), &(msg->data), msg->length);
	MSG_ZERO_END(newmsg, size);
	o2_free_message(msg);
	return newmsg;
}


o2_message_ptr alloc_size_message(int size)
{
	if (size <= MESSAGE_ALLOCATED_FROM_SIZE(MESSAGE_DEFAULT_SIZE)) {
		// standard pre-allocated message is big enough so use one */
		return alloc_message();
	}
	else {
		return (o2_message_ptr)o2_malloc(MESSAGE_SIZE_FROM_ALLOCATED(size));
	}
}


int o2_strsize(const char *s)
{
	return (strlen(s) + 4) & ~3;
}

/*
size_t o2_arg_size(o2_type type, void *data)
{
switch (type) {
case O2_TRUE:
case O2_FALSE:
case O2_NIL:
case O2_INFINITUM:
return 0;

case O2_INT32:
case O2_FLOAT:
case O2_MIDI:
case O2_CHAR:
return 4;

case O2_INT64:
case O2_TIME:
case O2_DOUBLE:
return 8;

case O2_STRING:
case O2_SYMBOL:
return o2_strsize((char *) data);

case O2_BLOB:
return (sizeof(uint32_t) + ((o2_blob_ptr) data)->size + 3) & ~3;

default:
fprintf(stderr,
"O2 warning: unhandled OSC type '%c'\n", type);
return 0;
}
return 0;
}
*/


// o2_blob_new - allocate a blob
//
o2_blob_ptr o2_blob_new(uint32_t size)
{
	// allocate space for length and extend to word boundary:
	int64_t needed = WORD_OFFSET(sizeof(uint32_t) + size + 3);
	if (needed > 0xFFFFFF00) { // allow almost 2^32 byte blobs
		return NULL; // but leave a little extra room
	}
	o2_blob_ptr blob = (o2_blob_ptr)O2_MALLOC(needed);
	if (blob) {
		blob->size = needed;
	}
	return blob;
}


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
	char *pos = (char *)data;

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

/*
ssize_t o2_validate_arg(o2_type type, void *data, ssize_t size)
{
if (size < 0) {
return -1;
}
switch (type) {
case O2_TRUE:
case O2_FALSE:
case O2_NIL:
case O2_INFINITUM:
return 0;

case O2_INT32:
case O2_FLOAT:
case O2_MIDI:
case O2_CHAR:
return size >= 4 ? 4 : -O2_ESIZE;

case O2_INT64:
case O2_TIME:
case O2_DOUBLE:
return size >= 8 ? 8 : -O2_ESIZE;

case O2_STRING:
case O2_SYMBOL:
return o2_validate_string((char *) data, size);

case O2_BLOB:
return o2_validate_blob((o2_blob_ptr) data, size);

default:
return -O2_EINVALIDTYPE;
}
return -O2_INT_ERR;
}
*/

/* convert endianness of arg pointed to by data from network to host */
void o2_arg_swap_endian(o2_type type, void *data)
{
	switch (type) {
	case O2_INT32:
	case O2_FLOAT:
	case O2_BLOB:
	case O2_CHAR: {
		int32_t i = *(int32_t *)data;
		*(int32_t *)data = swap32(i);
		break;
	}
	case O2_TIME:
	case O2_INT64:
	case O2_DOUBLE: {
		int64_t i = *(int64_t *)data;
		*(int64_t *)data = swap64(i);
		break;
	}
	case O2_STRING:
	case O2_SYMBOL:
	case O2_MIDI:
	case O2_TRUE:
	case O2_FALSE:
	case O2_NIL:
	case O2_INFINITUM:
		/* these are fine */
		break;

	default:
		fprintf(stderr,
			"O2 warning: unhandled type '%c' at %s:%d\n", type,
			__FILE__, __LINE__);
		break;
	}
}


o2_message_ptr o2_build_message(o2_time timestamp, const char *service_name,
	const char *path, const char *typestring, va_list ap)
{
	int ret = 0;
	int s;

	o2_message_ptr msg = alloc_message();
	if (!msg) return O2_FAIL;
	msg->data.timestamp = timestamp;

	// special case: if service name is given, prepend it to the path
	int service_len = 0;
	if (service_name) {
		service_len = 1 + strlen(service_name);
	}
	int path_len = strlen(path);
	int type_len = strlen(typestring) + 1;
	int need = ((service_len + path_len + 4) & ~3) + ((type_len + 4) & ~3);

	MESSAGE_CHECK_LENGTH(msg, need);

	// add service name if any
	if (service_name) {
		MESSAGE_APPEND(msg, char, '/');
		MESSAGE_APPEND_DATA(msg, service_name, service_len - 1);
	}

	// add the path
	MESSAGE_APPEND_PAD_DATA(msg, path, path_len);

	// add type string
	int comma_index = msg->length++; // leave space for comma
	MESSAGE_APPEND_PAD_DATA(msg, typestring, type_len - 1);
	// It would be simpler to simply append ',' as a string, then
	// append the typestring, but if typestring's length is less
	// than 3, 4 bytes will be zeroed, including the comma. To
	// solve this problem, we write the comma AFTER the typestring
	// and zero pad.
	((char *)& (msg->data))[comma_index] = ',';
	// add data
	while (typestring && *typestring) {
		switch (*typestring++) {
		case O2_INT32: {
			int32_t i = va_arg(ap, int32_t);
			MESSAGE_CHECK_LENGTH(msg, sizeof(int32_t));
			MESSAGE_APPEND(msg, int32_t, i);
			break;
		}

		case O2_FLOAT: {
			float f = (float)va_arg(ap, double);
			MESSAGE_CHECK_LENGTH(msg, sizeof(float));
			MESSAGE_APPEND(msg, float, f);
			break;
		}

		case O2_SYMBOL:
		case O2_STRING: {
			char *string = va_arg(ap, char *);
#ifndef USE_ANSI_C
			if (string == (char *)O2_MARKER_A) {
				fprintf(stderr,
					"o2 error: o2_send or o2_message_add called with "
					"invalid string pointer, probably arg mismatch.\n");
			}
#endif
			s = strlen(string);
			MESSAGE_CHECK_LENGTH(msg, s + 4);
			MESSAGE_APPEND_PAD_DATA(msg, string, s);
			break;
		}

		case O2_BLOB: {
			o2_blob b = va_arg(ap, o2_blob);
			MESSAGE_CHECK_LENGTH(msg, sizeof(b.size));
			MESSAGE_APPEND(msg, int32_t, b.size);
			MESSAGE_CHECK_LENGTH(msg, b.size + 4);
			MESSAGE_APPEND_PAD_DATA(msg, b.data, b.size);
			break;
		}

		case O2_INT64: {
			int64_t i64 = va_arg(ap, int64_t);
			MESSAGE_CHECK_LENGTH(msg, sizeof(int64_t));
			MESSAGE_APPEND(msg, int64_t, i64);
			break;
		}

		case O2_TIME:
		case O2_DOUBLE: {
			double d = va_arg(ap, double);
			MESSAGE_CHECK_LENGTH(msg, sizeof(double));
			MESSAGE_APPEND(msg, double, d);
			break;
		}

		case O2_CHAR: {
			char c = va_arg(ap, int);
			MESSAGE_CHECK_LENGTH(msg, 4);
			MESSAGE_APPEND_PAD_BYTE(msg, c);
			break;
		}

		case O2_MIDI: {
			uint8_t *m = va_arg(ap, uint8_t *);
			MESSAGE_CHECK_LENGTH(msg, 4);
			MESSAGE_APPEND_DATA(msg, m, 4);
			break;
		}

		case O2_TRUE:
		case O2_FALSE:
		case O2_NIL:
		case O2_INFINITUM:
			break;

		case '$':
			if (*typestring == '$') {
				// type strings ending in '$$' indicate not to perform
				// O2_MARKER checking
				va_end(ap);
				return 0;
			}

			// fall through to unknown type
		default: {
			ret = -1;       // unknown type
			fprintf(stderr,
				"o2 warning: unknown type '%c'",
				*(typestring - 1));
			break;
		}
		}
	}

#ifndef USE_ANSI_C
	void *i = va_arg(ap, void *);
	if (((unsigned long)i & 0xFFFFFFFFUL)
		!= ((unsigned long)O2_MARKER_A & 0xFFFFFFFFUL)) {
		ret = -2;               // bad format/args
		fprintf(stderr,
			"o2 error: o2_send, o2_message_add, or o2_message_add_varargs called with mismatching types and data at\n exiting.\n");
		va_end(ap);
		return NULL;
	}
	i = va_arg(ap, void *);
	if ((((unsigned long)i) & 0xFFFFFFFFUL) !=
		(((unsigned long)O2_MARKER_B) & 0xFFFFFFFFUL)) {
		fprintf(stderr,
			"o2 error: o2_send, o2_message_add, or o2_message_add_varargs called with mismatching types and data at\n exiting.\n");
		return NULL;
	}
#endif
	va_end(ap);

	return msg;
}



/* low level message construction */

// temp_* is data about the message under construction
// temp_msg is where we build the message
//     the typestring is built starting at data.address
// temp_type_end contains the address at which to store next type code
//     note that we leave a gap between the typestring and the data so
//     that we can insert type codes without moving the data
// temp_start is where the data starts, this is 1/5 of the way through
//     the allocated message memory, assuming most arguments are floats
//     and int32, so we need about 4x as much memory for data
// temp_end is where the data ends, where to append the next argument
// temp_barrier is a pointer to the end of the allocated data area
static o2_message_ptr temp_msg = NULL;
static char *temp_type_end = NULL;
static char *temp_start = NULL;
static char *temp_end = NULL;
static char *temp_barrier = NULL;

// allocate a message and prepare to add args
int o2_start_send()
{
	temp_msg = alloc_message();
	if (!temp_msg) return O2_FAIL;
	temp_type_end = temp_msg->data.address;
	*temp_type_end++ = ','; // first character of typestring
	// offset of data is about 1/5 total message size, but rounded to
	// multiple of 4 bytes
	temp_msg->length = (temp_msg->allocated / 5) & ~3;
	temp_start = ((char *)& (temp_msg->data)) + temp_msg->length;
	temp_end = temp_start;
	return O2_SUCCESS;
}


int add_argument(int size, void *data, char typecode)
{
	int realsize = (size + 3) & ~3;
	int needed = temp_msg->length + realsize;
	// expand if there is no room for either types or data
	if ((temp_msg->allocated < needed) || (temp_type_end >= temp_start)) {
		int new_allocated = temp_msg->allocated * 2;
		if (new_allocated < needed) new_allocated = needed + 64;
		o2_message_ptr newmsg = (o2_message_ptr)
			O2_MALLOC(MESSAGE_SIZE_FROM_ALLOCATED(new_allocated));
		newmsg->allocated = new_allocated;
		// copy typestring
		memcpy(newmsg->data.address, temp_msg->data.address,
			temp_type_end - temp_msg->data.address);
		// copy data
		void *new_start = newmsg->data.address + ((new_allocated / 5) & ~3);
		memcpy(new_start, temp_start, temp_end - temp_start);
		// update temp pointers
		temp_type_end = newmsg->data.address +
			(temp_type_end - temp_msg->data.address);
		temp_start = new_start;
		temp_end = newmsg->data.address +
			((char *)temp_end - temp_msg->data.address);
		// free temp_msg
		o2_free_message(temp_msg);
		temp_msg = newmsg;
	}
	// add the typecode
	*temp_type_end++ = typecode;
	// add the parameter if not empty
	if (realsize > 0) {
		*((int32_t *)(temp_end + realsize - 4)) = 0;
		memcpy(temp_end, data, size);
		temp_end += realsize;
		temp_msg->length += realsize;
	}
	return O2_SUCCESS;
}


int o2_add_int32(int32_t i)
{
	return add_argument(sizeof(int32_t), &i, 'i');
}

int o2_add_float(float f)
{
	return add_argument(sizeof(float), &f, 'f');
}

int o2_add_symbol(char *s)
{
	return add_argument(strlen(s) + 1, s, 'S');
}

int o2_add_string(char *s)
{
	return add_argument(strlen(s) + 1, s, 's');
}

int o2_add_blob_data(uint32_t size, void *data)
{
	// blobs have 2 parts: size and data; we need to use add_argument
	// for each to make sure we do not overflow the message buffer, but
	// we want to add only one type code. The solution is simply decrement
	// temp_type_end after adding the size to overwrite the first typecode.
	int rslt = add_argument(sizeof(size), &size, 'b');
	if (rslt != O2_SUCCESS) return rslt;
	temp_type_end--;
	return add_argument(size, data, 'b');
}

int o2_add_blob(o2_blob *b)
{
	return o2_add_blob_data(b->size, b->data);
}

int o2_add_int64(int64_t i)
{
	return add_argument(sizeof(int64_t), &i, 'h');
}

int o2_add_time(o2_time t)
{
	return add_argument(sizeof(o2_time), &t, 't');
}

int o2_add_double(double d)
{
	return add_argument(sizeof(double), &d, 'd');
}

int o2_add_char(char c)
{
	return add_argument(sizeof(char), &c, 'c');
}

int o2_add_midi(uint8_t *m)
{
	return add_argument(4, m, 'm');
}

int o2_add_true()
{
	return add_argument(0, 0, 'T');
}

int o2_add_false()
{
	return add_argument(0, 0, 'F');
}

int o2_add_bool(int i)
{
	return add_argument(0, 0, (i ? 'T' : 'F'));
}

int o2_add_nil()
{
	return add_argument(0, 0, 'N');
}

int o2_add_infinitum()
{
	return add_argument(0, 0, 'I');
}


int add_time_address(o2_time time, char *address)
{
	// there are 3 cases:
	// (1) we have room for address and might need to move
	//     data to a lower address
	// (2) we need more room and must allocate a bigger message
	// (3) we need more room and can move data to a higher address
	// Note that we could, in principle, move typestring to make it
	// adjacent to data and copy address to the "left" of typestring
	// and then we'd have a contiguous message without moving the
	// largest part of the message, which is the data, but then there
	// would be a gap at the beginning of the message, and it would
	// be complicated if the start of the message itself was at some
	// random offset from the start of the allocated message memory.
	//
	int addrlen = strlen(address);
	int addrspace = (addrlen + 4) & ~3; // length of address + 1 to 4 zero bytes
	// the type string (including initial comma) is located at
	//     temp_msg->data.address, so the type string length can be computed by:
	int typelen = temp_type_end - temp_msg->data.address;
	// the space needed for the type string including zero padding:
	int typespace = (typelen + 4) & ~3;
	// how much space is available for the address string? Take the position
	//     of the payload (temp_start) and subtract the end of the type info:
	int space = (char *)temp_start - WORD_ALIGN_PTR(temp_type_end + 4);
	// what is the minimum space that must be allocated for the message?
	int new_allocated = temp_msg->length + addrspace - space;
	if (space >= addrspace) { // we have room for the address
		// move data to lower address (or not at all)
		// move typecodes up to make room for address
		// begin with zero pad at end of typestring
		*((int32_t *)(temp_msg->data.address + addrspace + typespace - 4)) = 0;
		memmove(temp_msg->data.address + addrspace, temp_msg->data.address,
			temp_type_end - temp_msg->data.address);
		// insert the address
		// begin with zero pad at end of address
		*((int32_t *)(temp_msg->data.address + addrspace - 4)) = 0;
		memcpy(temp_msg->data.address, address, addrlen);
		// move the data after typestring
		memmove(temp_msg->data.address + addrspace + typespace, temp_start,
			temp_end - temp_start);
		temp_msg->length = (temp_msg->data.address - (char *)& (temp_msg->data)) +
			addrspace + typespace + (temp_end - temp_start);
	}
	else if (new_allocated > temp_msg->allocated) {
		// need a bigger message. We now know what we need, so allocate it
		// and copy everything into place
		o2_message_ptr newmsg = (o2_message_ptr)
			o2_malloc(MESSAGE_SIZE_FROM_ALLOCATED(new_allocated));
		if (!newmsg) return O2_FAIL;
		newmsg->allocated = new_allocated;
		*((int32_t *)(newmsg->data.address + addrspace - 4)) = 0;
		memcpy(newmsg->data.address, address, addrlen);
		*((int32_t *)(newmsg->data.address + addrspace + typespace - 4)) = 0;
		memcpy(newmsg->data.address + addrspace, temp_msg->data.address,
			typelen);
		memcpy(newmsg->data.address + addrspace + typespace, temp_start,
			temp_end - temp_start);
		o2_free_message(temp_msg);
		newmsg->length = (newmsg->data.address - (char *)& (newmsg->data)) +
			addrspace + typespace + (temp_end - temp_start);
		temp_msg = newmsg;
	}
	else { // move data to higher address
		memmove(temp_msg->data.address + addrspace + typespace, temp_start,
			temp_end - temp_start);
		*((int32_t *)(temp_msg->data.address + addrspace + typespace - 4)) = 0;
		memcpy(temp_msg->data.address + addrspace, temp_msg->data.address,
			typelen);
		*((int32_t *)(temp_msg->data.address + addrspace - 4)) = 0;
		memcpy(temp_msg->data.address, address, addrlen);
		temp_msg->length = (temp_msg->data.address - (char *)& (temp_msg->data)) +
			addrspace + typespace + (temp_end - temp_start);
	}
	temp_msg->data.timestamp = time;
	return O2_SUCCESS;
}

o2_message_ptr o2_finish_message(o2_time time, char *address)
{
	int rslt = add_time_address(time, address);
	if (rslt != O2_SUCCESS) {
		o2_free_message(temp_msg);
		temp_msg = NULL;
		return NULL;
	}
	return temp_msg;
}

int o2_finish_send(o2_time time, char *address)
{
	if (o2_finish_message(time, address)) {
		return o2_send_message(temp_msg, FALSE);
	}
	else {
		return O2_FAIL;
	}
}


int o2_finish_send_cmd(o2_time time, char *address)
{
	if (o2_finish_message(time, address)) {
		return o2_send_message(temp_msg, TRUE);
	}
	else {
		return O2_FAIL;
	}
}


/// get ready to extract args with o2_get_next
/// returns number of arguments in message
//
int o2_start_extract(o2_message_ptr msg)
{
	temp_msg = msg;
	// point temp_type_end to the first type code byte.
	// skip over padding and ','
	temp_type_end = WORD_ALIGN_PTR(msg->data.address +
		strlen(msg->data.address) + 4) + 1;
	// point temp_end to the first argument in message
	int n_args = strlen(temp_type_end);
	temp_end = WORD_ALIGN_PTR(temp_type_end + n_args + 4);
	temp_barrier = WORD_ALIGN_PTR(((char *)& (msg->data)) + msg->length);
	return n_args;
}


o2_arg o2_coerced_value;

o2_arg_ptr convert_int(char type_code, int64_t i)
{
	switch (type_code) {
	case O2_INT32:
		o2_coerced_value.i32 = (int32_t)i;
		break;
	case O2_INT64:
		o2_coerced_value.i64 = i;
		break;
	case O2_FLOAT:
		o2_coerced_value.f = (float)i;
		break;
	case O2_DOUBLE:
	case O2_TIME:
		o2_coerced_value.d = (double)i;
		break;
	case O2_BOOL:
		o2_coerced_value.B = (i != 0);
		break;
	default:
		return NULL;
	}
	return &o2_coerced_value;
}

o2_arg_ptr convert_float(char type_code, double d)
{
	switch (type_code) {
	case O2_INT32:
		o2_coerced_value.i32 = (int32_t)d;
		break;
	case O2_INT64:
		o2_coerced_value.i64 = (int64_t)d;
		break;
	case O2_FLOAT:
		o2_coerced_value.f = (float)d;
		break;
	case O2_DOUBLE:
	case O2_TIME:
		o2_coerced_value.d = d;
		break;
	case O2_BOOL:
		o2_coerced_value.B = (d != 0);
		break;
	default:
		return NULL;
	}
	return &o2_coerced_value;
}


/// get the next argument from the message. If the type_code
/// does not match the actual type in the message, convert
/// if possible; otherwise, return NULL for the o2_arg_ptr.
/// Note that if coerce_flag was false, the type checking
/// will have compared types for an exact match, so if we
/// make it this far and we are constructing argv, then
/// no type coercion will take place (and the tests for type
/// matching are all redundant because they will all fail).
/// If client code is calling this, then there is no way to
/// turn off type coercion except that you can compare your
/// desired type_code to the character in the actual type
/// string and if there is no match, do not call o2_get_next().
//
o2_arg_ptr o2_get_next(char type_code)
{
    o2_arg_ptr rslt = (o2_arg_ptr)temp_end;
    if (temp_type_end >= temp_barrier) return NULL; // overrun
    if (*temp_type_end == 0) return NULL; // no more args
    switch (*temp_type_end++) {
      case O2_INT32:
       	if (type_code != O2_INT32) {
    	    rslt = convert_int(type_code, *((int32_t *)temp_end));
	}
        temp_end += sizeof(int32_t);
        break;
      case O2_FLOAT:
        if (type_code != O2_FLOAT) {
            rslt = convert_int(type_code, *((float *)temp_end));
        }
        temp_end += sizeof(float);
        break;
      case O2_SYMBOL:
      case O2_STRING:
        if (type_code != O2_SYMBOL && type_code != O2_STRING) {
            rslt = NULL; // type error
        } // otherwise the requested type is suitable
        temp_end += ((strlen(temp_end) + 4) & ~3);
        break;
      case O2_CHAR:
        if (type_code != O2_CHAR) {
            rslt = NULL;
        }
        temp_end += sizeof(int32_t); // char uses 4 bytes
        break;
      case O2_BLOB:
        if (type_code != O2_BLOB) {
            rslt = NULL; // type mismatch
        }
        temp_end += sizeof(uint32_t) + rslt->b.size;
        break;
      case O2_INT64:
        if (type_code != O2_INT64) {
            rslt = convert_int(type_code, *((int64_t *)temp_end));
        }
        temp_end += sizeof(int64_t);
        break;
      case O2_DOUBLE:
      case O2_TIME:
        if (type_code != O2_DOUBLE && type_code != O2_TIME) {
            rslt = convert_int(type_code, *((double *)temp_end));
        } // otherwise the requested type is suitable
        temp_end += sizeof(double);
        break;
      case O2_MIDI:
        if (type_code != O2_MIDI) {
            rslt = NULL; // type mismatch
        }
        temp_end += 4;
        break;
      case O2_TRUE:
        rslt = convert_int(type_code, 1);
        break;
      case O2_FALSE:
        rslt = convert_int(type_code, 0);
        break;
      case O2_NIL:
      case O2_INFINITUM:
      	break;
      default:
        fprintf(stderr, "O2 warning: unhandled OSC type '%c'\n",
                *temp_type_end);
        return NULL;
    }
    if (temp_end > temp_barrier) {
        temp_end = temp_barrier; // which points to 4 zero bytes at end
        return NULL;             // of the message
    }
    return rslt;
}


void o2_print_msg(o2_message_ptr msg)
{
    int i;
    printf("%s @ %g", msg->data.address, msg->data.timestamp);
    if (msg->data.timestamp > 0.0) {
        if (msg->data.timestamp > o2_global_now) {
            printf("(now+%gs)", msg->data.timestamp - o2_global_now);
        } else {
            printf("(%gs late)", o2_global_now - msg->data.timestamp);
        }
    }
    o2_start_extract(msg);
    char *types = temp_type_end;
    while (*types) {
        o2_arg_ptr arg = o2_get_next(*types);
        switch (*types) {
          case O2_INT32:
            printf(" %d", arg->i32);
            break;
          case O2_FLOAT:
            printf(" %f", arg->f);
            break;
          case O2_STRING:
            printf(" \"%s\"", arg->s);
            break;
          case O2_BLOB:
            printf(" [");
            if (arg->b.size > 12) {
                printf("%d byte blob", arg->b.size);
            } else {
                for (i = 0; i < arg->b.size; i++) {
                    if (i > 0) printf(" ");
                    printf("%#02x", *((unsigned char *)(arg->b.data)+4 + i));
                }
                printf("]");
            }
            break;
          case O2_INT64:
            printf(" %lld", arg->i64);
            break;
          case O2_TIME:
            printf(" %g", arg->d);
            break;
          case O2_DOUBLE:
            printf(" %g", arg->d);
            break;
          case O2_SYMBOL:
            printf(" '%s", arg->s);
            break;
          case O2_CHAR:
            printf(" '%c'", arg->c);
            break;
          case O2_MIDI:
            printf(" MIDI [");
            for (i = 0; i < 4; i++) {
                if (i > 0) printf(" "); 
                printf("0x%02x", arg->m[i]);
            }
            printf("]");
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
          default:
            printf(" O2 WARNING: unhandled type: %c\n", *types);
            break;
        }
        types++;
    }
}
