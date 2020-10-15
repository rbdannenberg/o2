// mqtt.h -- MQTT protocol extension
//
// Roger B. Dannenberg
// August 2020

/* This extension provides discovery and communication between O2 processes
that are not on the same LAN and are possibly behind NAT. See o2/doc/mqtt.txt
for design details */

#ifndef O2_NO_MQTT

#define MQTT_CLIENT 80
#define ISA_MQTT_CLIENT(p) ((p) && (p)->tag == MQTT_CLIENT)

extern dyn_array o2_mqtt_procs;
extern char o2_full_name[40];

o2_err_t o2_mqtt_enable(const char *broker, int port_num);

void o2_mqtt_initialize(const char *public_ip);

void o2_mqtt_send(proc_info_ptr proc, o2_message_ptr msg);

void o2_mqtt_free(proc_info_ptr proc);

void o2_mqtt_disc_handler(char *payload);

void o2m_deliver_mqtt_msg(const char *topic, int topic_len,
                          uint8_t *payload, int payload_len);

#endif
