// mqtt.h -- MQTT protocol extension
//
// Roger B. Dannenberg
// August 2020

/* This extension provides discovery and communication between O2 processes
that are not on the same LAN and are possibly behind NAT. See o2/doc/mqtt.txt
for design details. */

#ifndef O2_NO_MQTT

class MQTT_info : public Proxy_info {
public:
    MQTT_info(const char *key, int tag) : Proxy_info(key, tag) { }
    ~MQTT_info();
    
    // Implement the Net_interface:
    // do nothing, just start receiving messages:
    virtual O2err connected() { return O2_SUCCESS; }
    virtual O2err accepted(Fds_info *conn) { return O2_FAIL; }  // not a server
    virtual O2err deliver(o2n_message_ptr msg);

    bool local_is_synchronized() { o2_send_clocksync_proc(this);
                                   return IS_SYNCED(this); }
    virtual O2status status(const char **process) {
        if (process) {
            *process = get_proc_name();
        }
        return (o2_clock_is_synchronized && IS_SYNCED(this)) ?
                O2_REMOTE : O2_REMOTE_NOTIME;
    }
    virtual O2err send(bool block);
};

extern Vec<MQTT_info *> o2_mqtt_procs;

extern bool o2_mqtt_waiting_for_public_ip;

O2err o2_mqtt_send_disc();

O2err o2_mqtt_initialize();

void o2_mqtt_disc_handler(char *payload, int payload_len);

O2err o2_mqtt_can_send();

#endif
