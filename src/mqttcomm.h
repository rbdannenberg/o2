// mqttcomm.h -- headers for MQTT protocol implementation
//
// Roger B. Dannenberg
// October 2020

/* 
mqttcomm abstracts details of mqtt message formation and parsing.
The user should subclass the abstract MQTTcomm class and implement
methods for handling received messages.
*/

#ifndef O2_NO_MQTT

class MQTTcomm : O2obj {
    // input buffer for MQTT messages incoming:
    Vec<uint8_t> mqtt_input;
    int connack_count;
    int connack_expected;
    O2time connack_time;
    int puback_count;
    int puback_expected;
    O2time puback_time;
    int suback_count;
    int suback_expected;
    O2time suback_time;

    int packet_id;

public:
    MQTTcomm() {
        connack_count = 0;
        connack_expected = 0;
        connack_time = 0;
        puback_count = 0;
        puback_expected = 0;
        puback_time = 0;
        suback_count = 0;
        suback_expected = 0;
        suback_time = 0;

        packet_id = 0;
    }
    
    O2err initialize(const char *server, int port);
    void finish() { mqtt_input.finish(); }
    O2err subscribe(const char *topic, bool block);
    O2err subscribe(const char *topic) { return subscribe(topic, true); }
    void deliver(const char *data, int len);  // incoming bytes from TCP
    bool handle_first_msg();  // process next MQTT message from input stream
    O2err publish(const char *subtopic, const uint8_t *payload,
                  int payload_len, int retain, bool block);
    O2err publish(const char *subtopic, const uint8_t *payload,
                  int payload_len, int retain) {
        return publish(subtopic, payload, payload_len, retain, true);
    }
    
    // msg owned by callee, send by TCP to MQTT broker:
    virtual O2err msg_send(o2n_message_ptr msg, bool block) = 0;
    O2err msg_send(o2n_message_ptr msg) { return msg_send(msg, true); }
    // data is owned by caller, an MQTT publish message has arrived. Handle it:
    virtual void deliver_mqtt_msg(const char *topic, int topic_len,
                                  uint8_t *payload, int payload_len) = 0;
};

#endif
