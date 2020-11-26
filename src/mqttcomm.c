// mqttcomm.c -- communication through MQTT protocol
//
// Roger B. Dannenberg
// Oct 2020

#ifndef O2_NO_MQTT

#include "o2internal.h"
#include "mqttcomm.h"
#include "message.h"

#define MQTT_CONNECT 0x10
#define MQTT_CONNACK 0x20
#define MQTT_PUBLISH 0x30
// add this flag to MQTT_PUBLISH:
#define MQTT_RETAIN 1
#define MQTT_PUBACK 0x40
#define MQTT_SUBSCRIBE 0x82
#define MQTT_SUBACK 0x90
#define MQTT_DISCONNECT 0xE0
#define MQTT_QOS1 2
#define MQTT_MAX_MULT (128 * 128 * 128)
// how many seconds to wait for ACK before printing warning
#define MQTT_TIMEOUT 10

static int packet_id = 0;

typedef struct {
    int tag;
} mqtt_client, *mqtt_client_ptr;


// o2n application info for the MQTT broker connection:
static mqtt_client o2m_mqtt_client;
// o2n socket for connection to MQTT broker:
static o2n_info_ptr mqtt_info = NULL;
// input buffer for MQTT messages incoming:
static dyn_array mqtt_input;

static int connack_count = 0;
static int connack_expected = 0;
static o2_time connack_time = 0;
static int puback_count = 0;
static int puback_expected = 0;
static o2_time puback_time = 0;
static int suback_count = 0;
static int suback_expected = 0;
static o2_time suback_time = 0;


void print_bytes(const char *prefix, char *bytes, int len)
{
    printf("%s:\n", prefix);
    int i = 0;
    while (i < len) {
        for (int j = 0; j < 16; j++) {  // print hex chars
            if (i + j < len) {
                printf(" %02x", (uint8_t) bytes[i + j]);
            } else {
                printf("   ");
            }
        }
        printf("  ");
        for (int j = 0; j < 16; j++) {  // print ascii chars
            if (i + j < len) {
                uint8_t b = (uint8_t) bytes[i + j];
                printf("%c", (b >= '!' && b <= '~' ? b : (uint8_t) '.'));
            }
        }
        printf("\n");
        i += 16;
    }
}


// use o2_ctx->msg_data to build MQTT messages
// start by calling o2_send_start()
// add to message with:
static void mqtt_append_bytes(void *data, int length)
{
    o2_message_check_length(length);
    memcpy(o2_ctx->msg_data.array + o2_ctx->msg_data.length, data, length);
    o2_ctx->msg_data.length += length;
}


#define MQTT_APPEND_INT16(i) \
    o2_ctx->msg_data.array[o2_ctx->msg_data.length++] = ((i) >> 8); \
    o2_ctx->msg_data.array[o2_ctx->msg_data.length++] = ((i) & 0xFF);


static void mqtt_append_string(const char *s)
{
    int len = strlen(s);
    o2_message_check_length(len + 2);
    MQTT_APPEND_INT16(len);
    memcpy(o2_ctx->msg_data.array + o2_ctx->msg_data.length, s, len);
    o2_ctx->msg_data.length += len;
}

// append the concatenation of "O2-", o2_ensemble_name, s1, s2
//   (to append a full topic string)
static void mqtt_append_topic(const char *s1)
{
    int len0 = strlen(o2_ensemble_name);
    int len1 = strlen(s1);
    int len = 4 + len0 + len1;
    o2_message_check_length(len + 2);
    MQTT_APPEND_INT16(len);
    char *base = o2_ctx->msg_data.array + o2_ctx->msg_data.length;
    memcpy(base, "O2-", 3);
    memcpy(base + 3, o2_ensemble_name, len0);
    base[3 + len0] = '/';
    memcpy(base + 4 + len0, s1, len1);
    o2_ctx->msg_data.length += len;
}

static void mqtt_append_int16(int i)
{
    o2_message_check_length(2);
    MQTT_APPEND_INT16(i);
}


static o2n_message_ptr mqtt_finish_msg(int command)
{
    int len = o2_ctx->msg_data.length;
    uint8_t varlen[4];
    int varlen_len = 0;
    do {
        int encoded = len & 0x7F;
        len >>= 7;
        if (len > 0) {
            encoded |= 0x80;
        }
        varlen[varlen_len++] = encoded;
    } while (len > 0);
    len = o2_ctx->msg_data.length;
    // (this will allocate some unused bytes for flags and timestamp:)
    int msg_len = len + varlen_len + 1;
    o2n_message_ptr msg = O2N_MESSAGE_ALLOC(msg_len);
    msg->length = msg_len;
    // move data
    memcpy(msg->payload + varlen_len + 1, o2_ctx->msg_data.array, len);
    // insert new stuff
    msg->payload[0] = command;
    memcpy(msg->payload + 1, varlen, varlen_len);
    print_bytes("mqtt_finish_msg", msg->payload, msg->length);
    return msg;
}


o2_err_t o2m_initialize(const char *server, int port_num)
{
    if (!*server) {
        return O2_BAD_ARGS; // possibly someone called o2_get_public_ip(),
        // but o2_mqtt_enable() was not called to set the mqtt broker.
    }
    DA_INIT(mqtt_input, uint8_t, 32);
    o2m_mqtt_client.tag = MQTT_CLIENT;
    packet_id = 0;
    mqtt_info = o2n_connect(server, port_num, (void *) &o2m_mqtt_client);
    mqtt_info->raw_flag = true;
    o2_send_start();
    mqtt_append_string("MQTT");
    uint8_t bytes[6] = {4, 2, 0, 60, 0, 0};
    mqtt_append_bytes(bytes, 6);
    o2n_message_ptr msg = mqtt_finish_msg(MQTT_CONNECT);
    connack_expected++;
    O2_DBq(printf("%s sending MQTT_CONNECT connack expected %d\n",
                  o2_debug_prefix, connack_expected));
    connack_time = o2_local_time();
    return o2n_send_tcp(mqtt_info, false, msg);
}


o2_err_t o2m_subscribe(const char *topic)
{
    packet_id = (packet_id + 1) & 0xFFFF;
    o2_send_start();
    mqtt_append_int16(packet_id);
    mqtt_append_string(topic);
    uint8_t byte = 0;
    mqtt_append_bytes(&byte, 1);
    o2n_message_ptr msg = mqtt_finish_msg(MQTT_SUBSCRIBE);
    suback_expected++;
    O2_DBq(printf("%s sending MQTT_SUBSCRIBE %s suback expected %d\n",
                  o2_debug_prefix, topic, suback_expected));
    suback_time = o2_local_time();
    return o2n_send_tcp(mqtt_info, false, msg);
}


// See if we have a whole message yet
// m is a message, len is the number of bytes we have so far
// returns actual length of first message, or -1 if there is
//    no complete message yet
// Also, *posn is set to the byte after this length specification
static int mqtt_int_len(const uint8_t *m, int len, int *posn)
{
    int multiplier = 1;
    *posn = 1;
    int length = 0;
    bool done = false;
    while (!done) {
        if (*posn > len) {
            return -1;
        }
        uint8_t byte = m[*posn];
        *posn = *posn + 1;  // careful: *posn++ doesn't increment *posn
        length = length + (byte & 0x7F) * multiplier;
        multiplier <<= 7;
        done = byte < 128 || multiplier > MQTT_MAX_MULT;
    }
    return length;
}


void o2m_received(int n)
{
    if (n < mqtt_input.length) {
        char *inbuff = mqtt_input.array;
        memmove(inbuff, inbuff + n, mqtt_input.length - n);
    }
    mqtt_input.length -= n;
}


bool handle_first_mqtt_msg()
{
    uint8_t first = mqtt_input.array[0];
    // see if we have a whole message yet
    if ((first & 0xF0) == MQTT_PUBLISH) {
        int posn;
        uint8_t *inbuff = (uint8_t *) mqtt_input.array;
        int len = mqtt_int_len(inbuff, mqtt_input.length, &posn);
        if (len < 0 || posn + len > mqtt_input.length) {
            goto incomplete; // need more bytes to make a complete message
        }
        int topic_len = (inbuff[posn] << 8) + inbuff[posn + 1];
        posn += 2;
        o2m_deliver_mqtt_msg((const char *) inbuff + posn, topic_len,
                  inbuff + posn + topic_len + 2, len - topic_len - 4);
        // remove this message from mqtt_input
        posn += len - 2;
        o2m_received(posn);
    } else if (first == MQTT_CONNACK) {
        if (mqtt_input.length < 4) {
            goto incomplete;
        }
        connack_count++;
        O2_DBq(printf("%s MQTT_CONNACK received, count %d\n",
                      o2_debug_prefix, connack_count));
        o2m_received(4);
    } else if (first == MQTT_SUBACK) {
        if (mqtt_input.length < 5) {
            goto incomplete;
        }
        suback_count++;
        O2_DBq(printf("%s MQTT_SUBACK received, count %d\n",
                      o2_debug_prefix, suback_count));
        o2m_received(5);
    } else if (first == MQTT_PUBACK) {
        if (mqtt_input.length < 4) {
            goto incomplete;
        }
        puback_count++;
        O2_DBq(printf("%s MQTT_PUBACK received, count %d\n",
                      o2_debug_prefix, puback_count));
        o2m_received(4);
    } else {
        printf("O2 Warning: could not parse incoming MQTT message\n");
        mqtt_input.length = 0; // empty input buffer and hope to resync
    }
    return true;
  incomplete:
    return false;
}


// handle an incoming message from network. application is MQTT_CLIENT.
// 
// Append incoming bytes to mqtt_input. Multiple messages can arrive at
// once, and handle_first_mqtt_msg() looks for only the first message.
// If a one message is found and handled, the message is removed from 
// the front of mqtt_input. If true is returned and there are more bytes,
// repeat the call to handle_first_mqtt_msg() until we have either an
// incomplete message or nothing at all.
//
void o2_mqtt_received(o2n_info_ptr info)
{
    o2_message_ptr msg = o2_postpone_delivery();
    char *data = (char *) &msg->data.flags;
    int len = msg->data.length;
    // make input buffer big enough to add all these new bytes
    while (mqtt_input.length + len > mqtt_input.allocated) {
        o2_da_expand(&mqtt_input, sizeof(uint8_t));
    }
    // append the new bytes to the input buffer
    char *dst = mqtt_input.array + mqtt_input.length;
    memcpy(dst, data, len);
    mqtt_input.length += len;
    // done with message:
    O2_FREE(msg);
    print_bytes("o2_mqtt_received", mqtt_input.array, mqtt_input.length);
    bool handled = handle_first_mqtt_msg();
    while (handled && mqtt_input.length > 0) {
        handled = handle_first_mqtt_msg();
    }
    // check for expected acks after every message comes in:
    if (connack_count < connack_expected &&
        connack_time < o2_local_time() - MQTT_TIMEOUT) {
        printf("WARNING: Did not receive expected MQTT CONNACK\n");
        connack_count++; // only warn once per lost ack
    }
    if (suback_count < suback_expected &&
        suback_time < o2_local_time() - MQTT_TIMEOUT) {
        printf("WARNING: Did not receive expected MQTT SUBACK\n");
        suback_count++; // only warn once per lost ack
    }
    if (puback_count < puback_expected &&
        puback_time < o2_local_time() - MQTT_TIMEOUT) {
        printf("WARNING: Did not receive expected MQTT PUBACK\n");
        puback_count++; // only warn once per lost ack
    }
}


o2_err_t o2_mqtt_can_send()
{
    return (mqtt_info ?
            (mqtt_info->out_message ? O2_BLOCKED : O2_SUCCESS) :
            O2_FAIL);
}

o2_err_t o2_mqtt_publish(const char *subtopic,
                         const uint8_t *payload, int payload_len, int retain)
{
    packet_id = (packet_id + 1) & 0xFFFF;
    o2_send_start();
    mqtt_append_topic(subtopic);
    assert(o2_ctx->msg_data.length ==
           6 + strlen(o2_ensemble_name) + strlen(subtopic));
    mqtt_append_int16(packet_id);
    assert(o2_ctx->msg_data.length ==
           8 + strlen(o2_ensemble_name) + strlen(subtopic));
    mqtt_append_bytes((void *) payload, payload_len);
    assert(o2_ctx->msg_data.length == 8 + strlen(o2_ensemble_name) +
                                      strlen(subtopic) + payload_len);
    printf("o2_mqtt_publish payload_len %d\n", payload_len);
    o2n_message_ptr msg = mqtt_finish_msg(MQTT_PUBLISH | retain);
    printf("o2_mqtt_publish message len %d\n", msg->length);
    puback_expected++;
    O2_DBq(printf("%s sending that msg via MQTT_PUBLISH puback expected %d\n",
                  o2_debug_prefix, puback_expected));
    o2_err_t err = o2n_send_tcp(mqtt_info, false, msg);
    O2_DBq(if (err)
               printf("o2n_send_tcp returns %s\n", o2_error_to_string(err)););
    return err;
}

#endif
