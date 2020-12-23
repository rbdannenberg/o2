// mqttcomm.c -- communication through MQTT protocol
//
// Roger B. Dannenberg
// Oct 2020

#ifndef O2_NO_MQTT

#include "o2internal.h"
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


#ifndef O2_NO_DEBUG
void print_bytes(const char *prefix, const char *bytes, int len)
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
#endif

// use o2_ctx->msg_data to build MQTT messages
// start by calling o2_send_start()
// add to message with:
static void mqtt_append_bytes(void *data, int length)
{
    o2_ctx->msg_data.append((char *) data, length);
}

static void mqtt_append_int16(int i)
{
    o2_ctx->msg_data.push_back((char)((i) >> 8)); \
    o2_ctx->msg_data.push_back((char)((i) & 0xFF));
}


static void mqtt_append_string(const char *s)
{
    int len = strlen(s);
    mqtt_append_int16(len);
    o2_ctx->msg_data.append(s, len);
}

// append the concatenation of "O2-", o2_ensemble_name, s1, s2
//   (to append a full topic string)
static void mqtt_append_topic(const char *s1)
{
    int len0 = strlen(o2_ensemble_name);
    int len1 = strlen(s1);
    int len = 4 + len0 + len1;
    mqtt_append_int16(len);
    o2_ctx->msg_data.append("O2-", 3);
    o2_ctx->msg_data.append(o2_ensemble_name, len0);
    o2_ctx->msg_data.push_back('/');
    o2_ctx->msg_data.append(s1, len1);
}


static o2n_message_ptr mqtt_finish_msg(int command)
{
    int len = o2_ctx->msg_data.size();
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
    len = o2_ctx->msg_data.size();
    // (this will allocate some unused bytes for flags and timestamp:)
    int msg_len = len + varlen_len + 1;
    o2n_message_ptr msg = O2N_MESSAGE_ALLOC(msg_len);
    msg->length = msg_len;
    // move data
    o2_ctx->msg_data.retrieve(msg->payload + varlen_len + 1);
    // insert new stuff
    msg->payload[0] = command;
    memcpy(msg->payload + 1, varlen, varlen_len);
    O2_DBq(print_bytes("mqtt_finish_msg", msg->payload, msg->length));
    return msg;
}


// server is in domain name, localhost, or dot format
O2err MQTTcomm::initialize(const char *server, int port_num)
{
    if (!*server) {
        return O2_BAD_ARGS; // possibly someone called o2_get_public_ip(),
        // but o2_mqtt_enable() was not called to set the mqtt broker.
    }
    mqtt_input.init(32);
    packet_id = 0;
    o2_send_start();
    mqtt_append_string("MQTT");
    uint8_t bytes[6] = {4, 2, 0, 60, 0, 0};
    mqtt_append_bytes(bytes, 6);
    o2n_message_ptr msg = mqtt_finish_msg(MQTT_CONNECT);
    connack_expected++;
    O2_DBq(printf("%s sending MQTT_CONNECT connack expected %d\n",
                  o2_debug_prefix, connack_expected));
    connack_time = o2_local_time();
    return msg_send(msg);
}


O2err MQTTcomm::subscribe(const char *topic, bool block)
{
    packet_id = (packet_id + 1) & 0xFFFF;
    o2_send_start();
    mqtt_append_int16(packet_id);
    mqtt_append_topic(topic);
    uint8_t byte = 0;
    mqtt_append_bytes(&byte, 1);
    o2n_message_ptr msg = mqtt_finish_msg(MQTT_SUBSCRIBE);
    suback_expected++;
    O2_DBq(printf("%s sending MQTT_SUBSCRIBE %s suback expected %d\n",
                  o2_debug_prefix, topic, suback_expected));
    suback_time = o2_local_time();
    return msg_send(msg, block);
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


bool MQTTcomm::handle_first_msg()
{
    uint8_t first = mqtt_input[0];
    int size = mqtt_input.size();
    // see if we have a whole message yet
    if ((first & 0xF0) == MQTT_PUBLISH) {
        int posn;
        uint8_t *inbuff = &mqtt_input[0];
        int len = mqtt_int_len(inbuff, size, &posn);
        if (len < 0 || posn + len > size) {
            goto incomplete; // need more bytes to make a complete message
        }
        int topic_len = (inbuff[posn] << 8) + inbuff[posn + 1];
        posn += 2;  // location of topic
        deliver_mqtt_msg((const char *) inbuff + posn, topic_len,
                        inbuff + posn + topic_len + 2, len - topic_len - 4);
        // remove this message from mqtt_input; len is the length starting
        // at the topic length, which is at posn - 2:
        mqtt_input.drop_front(posn - 2 + len);
    } else if (first == MQTT_CONNACK) {
        if (size < 4) {
            goto incomplete;
        }
        connack_count++;
        O2_DBq(printf("%s MQTT_CONNACK received, count %d\n",
                      o2_debug_prefix, connack_count));
        mqtt_input.drop_front(4);
    } else if (first == MQTT_SUBACK) {
        if (size < 5) {
            goto incomplete;
        }
        suback_count++;
        O2_DBq(printf("%s MQTT_SUBACK received, count %d\n",
                      o2_debug_prefix, suback_count));
        mqtt_input.drop_front(5);
    } else if (first == MQTT_PUBACK) {
        if (size < 4) {
            goto incomplete;
        }
        puback_count++;
        O2_DBq(printf("%s MQTT_PUBACK received, count %d\n",
                      o2_debug_prefix, puback_count));
        mqtt_input.drop_front(4);
    } else {
        printf("O2 Warning: could not parse incoming MQTT message\n");
        mqtt_input.drop_front(size); // empty input buffer and hope to resync
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
void MQTTcomm::deliver(const char *data, int len)
{
    // append the new bytes to the input buffer
    mqtt_input.append((uint8_t *) data, len);
    // done with message:
    O2_DBq(print_bytes("MQTTcomm::received", (const char *) &mqtt_input[0],
                       mqtt_input.size()));
    bool handled = handle_first_msg();
    while (handled && mqtt_input.size() > 0) {
        handled = handle_first_msg();
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


O2err MQTTcomm::publish(const char *subtopic, const uint8_t *payload,
                        int payload_len, int retain, bool block)
{
    packet_id = (packet_id + 1) & 0xFFFF;
    o2_send_start();
    mqtt_append_topic(subtopic);
    assert(o2_ctx->msg_data.size() ==
           6 + strlen(o2_ensemble_name) + strlen(subtopic));
    mqtt_append_int16(packet_id);
    assert(o2_ctx->msg_data.size() ==
           8 + strlen(o2_ensemble_name) + strlen(subtopic));
    mqtt_append_bytes((void *) payload, payload_len);
    assert(o2_ctx->msg_data.size() == 8 + strlen(o2_ensemble_name) +
                                      strlen(subtopic) + payload_len);
    O2_DBq(printf("MQTTcomm::publish payload_len %d\n", payload_len));
    o2n_message_ptr msg = mqtt_finish_msg(MQTT_PUBLISH | retain);
    O2_DBq(printf("MQTTcomm::publish message len %d\n", msg->length));
    puback_expected++;
    O2_DBq(printf("%s sending that msg via MQTT_PUBLISH puback expected %d\n",
                  o2_debug_prefix, puback_expected));
    O2err err = msg_send(msg, block);
    O2_DBq(if (err) printf("MQTTcomm::msg_send returns %s\n",
                           o2_error_to_string(err)););
    return err;
}


#endif
