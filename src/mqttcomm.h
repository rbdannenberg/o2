// mqttcomm.h -- headers for MQTT protocol implementation
//
// Roger B. Dannenberg
// October 2020

#ifndef O2_NO_MQTT

o2_err_t o2m_initialize(const char *server, int port);
o2_err_t o2m_subscribe(const char *topic);
void o2m_received(int n);
void o2_mqtt_received(o2n_info_ptr info);
o2_err_t o2_mqtt_publish(const char *subtopic,
                         const uint8_t *payload, int payload_len, int retain);
o2_err_t o2_mqtt_can_send();

#endif
