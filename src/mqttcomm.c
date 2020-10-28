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


static void mqtt_append_int16(int i)
{
    o2_message_check_length(2);
    MQTT_APPEND_INT16(i);
}


static o2n_message_ptr mqtt_finish_msg(int command)
{
    int len = o2_ctx->msg_data.length;
    uint8_t varlen[4];
    varlen[0] = 0;
    int varlen_len = 1;
    int varlen_index = 0;
    while (len > 0) {
        varlen[varlen_index++] = len & 0x7F;
        len >>= 7;
        varlen_len = varlen_index;
    }
    len = o2_ctx->msg_data.length;
    // (this will allocate some unused bytes for flags and timestamp:)
    int msg_len = len + varlen_len + 1;
    o2n_message_ptr msg = (o2n_message_ptr) o2_message_new(msg_len);
    // move data
    memcpy(msg->data + varlen_len + 1, o2_ctx->msg_data.array, len);
    // insert new stuff
    msg->data[0] = command;
    memcpy(msg->data + 1, varlen, varlen_len);
    return msg;
}


o2_err_t o2m_initialize(const char *server, int port_num)
{
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
    suback_time = o2_local_time();
    return o2n_send_tcp(mqtt_info, false, msg);
}


// See if we have a whole message yet
// m is a message, len is the number of bytes we have so far
// returns actual length of first message, or -1 if there is
//    no complete message yet
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
        uint8_t byte = m[*posn++];
        length = length + (byte & 0x7F) * multiplier;
        multiplier <<= 7;
        done = byte < 128 || multiplier > MQTT_MAX_MULT;
    }
    return length;
}


void o2m_received(int n)
{
    if (n > mqtt_input.length) {
        char *inbuff = mqtt_input.array;
        memmove(inbuff, inbuff + n, mqtt_input.length - n);
    }
    mqtt_input.length -= n;
}


// handle an incoming message from network. application is MQTT_CLIENT.
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
    // done with message:
    O2_FREE(msg);
    uint8_t first = mqtt_input.array[0];
    // see if we have a whole message yet
    if ((first & 0xF0) == MQTT_PUBLISH) {
        int posn;
        uint8_t *inbuff = (uint8_t *) mqtt_input.array;
        len = mqtt_int_len(inbuff, mqtt_input.length, &posn);
        if (len < 0 || posn + len < mqtt_input.length) {
            return; // need more bytes to make a complete message
        }
        int topic_len = (inbuff[posn] << 8) + inbuff[posn + 1];
        posn += 2;
        o2m_deliver_mqtt_msg((const char *) inbuff + posn, topic_len,
                             inbuff + posn + topic_len, len - topic_len);
        // remove this message from mqtt_input
        posn += len;
        o2m_received(posn);
    } else if (first == MQTT_CONNACK) {
        if (mqtt_input.length < 4) {
            return;
        }
        connack_count++;
        o2m_received(4);
    } else if (first == MQTT_SUBACK) {
        if (mqtt_input.length < 5) {
            return;
        }
        suback_count++;
        o2m_received(5);
    } else if (first == MQTT_PUBACK) {
        if (mqtt_input.length < 4) {
            return;
        }
        puback_count++;
        o2m_received(4);
    } else {
        printf("O2 Warning: could not parse incoming MQTT message\n");
        mqtt_input.length = 0; // empty input buffer and hope to resync
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


o2_err_t o2_mqtt_publish(const char *topic, const uint8_t *payload,
                         int payload_len, int retain)
{
    packet_id = (packet_id + 1) & 0xFFFF;
    o2_send_start();
    mqtt_append_string(topic);
    mqtt_append_int16(packet_id);
    mqtt_append_bytes((void *) payload, payload_len);
    o2n_message_ptr msg = mqtt_finish_msg(MQTT_PUBLISH |
                                                            retain);
    return o2n_send_tcp(mqtt_info, false, msg);
}

#endif
