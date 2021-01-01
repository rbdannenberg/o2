// mqtt.c -- MQTT protocol extension
//
// Roger B. Dannenberg
// August 2020

/* This extension provides discovery and communication between O2 processes
that are not on the same LAN and are possibly behind NAT. See o2/doc/mqtt.txt
for design details */

#ifndef O2_NO_MQTT

#include "o2internal.h"
#include "discovery.h"
#include "message.h"
#include "services.h"
#include "msgsend.h"
#include "clock.h"
#include "pathtree.h"


static void o2_mqtt_discovery_handler(o2_msg_data_ptr msg, const char *types,
                           O2arg_ptr *argv, int argc, const void *user_data);

// discovered remote processes reachable by MQTT:
Vec<MQTT_info *> o2_mqtt_procs;
// address of the MQTT Broker:
Net_address mqtt_address;
// standard "dot" format IP address for broker:
char mqtt_broker_ip[O2_IP_LEN] = "";
// records that mqtt_enable() was called and should be initialized asap.
bool o2_mqtt_waiting_for_public_ip = false;
// connection to MQTT broker:
MQTT_info *mqtt_info = NULL;


class O2_MQTTcomm : public MQTTcomm {
public:
    O2err msg_send(o2n_message_ptr msg, bool block) {
        return mqtt_info->fds_info->send_tcp(block, msg); }
    // data is owned by caller, an MQTT publish message has arrived. Handle it:
    void deliver_mqtt_msg(const char *topic, int topic_len,
                          uint8_t *payload, int payload_len);
    void disc_handler(char *payload, int payload_len);
} mqtt_comm;


// broker is a domain name, localhost, or dot format
O2err o2_mqtt_enable(const char *broker, int port_num)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    if (!broker) broker = "mqtt.eclipseprojects.io";
    if (port_num == 0) port_num = 1883; // default for MQTT
    // look up the server to get the IP address. That way, we can get the
    // blocking call out of the way when the process starts up, and we
    // can return an error synchronously if the server cannot be found.
    // We cannot actually connect until we know our public IP address,
    // which we are getting from a stun server asynchronously since UDP
    // could could result in several retries and should be non-blocking.
    RETURN_IF_ERROR(mqtt_address.init(broker, 1883, true));
    if (!mqtt_address.to_dot(mqtt_broker_ip)) {
        perror("converting mqtt ip to string");
        return O2_FAIL;
    }
    O2_DBq(printf("o2_mqtt_enable %s with IP %s\n", broker, mqtt_broker_ip));
    o2_mqtt_procs.init(0);
    return o2_mqtt_initialize();
}


O2err o2_mqtt_send_disc()
{
    // send name to O2-<ensemblename>/disc, retain is off.
    if (!o2_ctx->proc->key) { // no name and no mqtt connection yet
        return O2_FAIL;
    }
    O2_DBq(printf("%s publishing to O2-%s/disc with payload %s\n",
                  o2_debug_prefix, o2_ensemble_name, o2_ctx->proc->key));
    return mqtt_comm.publish("disc", (const uint8_t *) o2_ctx->proc->key,
                      strlen(o2_ctx->proc->key),
                      o2_clock_is_synchronized ? "/cs" : "/dy", 0, false);
}

O2err o2_mqtt_initialize()
{
    if (!o2n_network_enabled) {
        return O2_NO_NETWORK;
    }
    if (!o2n_public_ip[0]) {
        o2_mqtt_waiting_for_public_ip = true;
        return O2_SUCCESS;
    }
    RETURN_IF_ERROR(o2_method_new_internal("/_o2/mqtt/dy", "s",
                         &o2_mqtt_discovery_handler, NULL, false, false));
    // make MQTT broker connection
    mqtt_info = new MQTT_info(NULL, O2TAG_MQTT);
    mqtt_info->fds_info = Fds_info::create_tcp_client(&mqtt_address);
    mqtt_info->fds_info->owner = mqtt_info;
    mqtt_info->fds_info->raw_flag = true;

    mqtt_comm.initialize(mqtt_broker_ip, mqtt_address.get_port());
    // subscribe to O2-<ensemblename>/disc
    mqtt_comm.subscribe("disc", false);  // topic is O2-<ensemblename>/disc
    o2_mqtt_send_disc();
    // subscribe to O2-<ensemble>/<public ip>:<local ip>:<port>
    return mqtt_comm.subscribe(o2_ctx->proc->key, false);
}


O2err o2_mqtt_can_send()
{
    return (mqtt_info ?
            (mqtt_info->fds_info->out_message ? O2_BLOCKED : O2_SUCCESS) :
            O2_FAIL);
}


// send an O2 message to proc, which is an MQTT proc
// prerequisite: msg is in network byte order
// msg is freed before returning
//
O2err MQTT_info::send(bool block)
{
    O2err rslt;
    int tcp_flag;
    O2message_ptr msg = pre_send(&tcp_flag);
    // pre_send prints debugging info if DBs or DBS, so only print here if those
    // flags are not set, but DBq is set:
    O2_DB(!O2_DBs_FLAG && !O2_DBS_FLAG && O2_DBq_FLAG,
          o2_dbg_msg("sending via mqtt", msg, &msg->data, "to", key));
    if (!msg) {
        rslt = O2_NO_SERVICE;
    } else {
        int payload_len = msg->data.length;
        const uint8_t *payload = (const uint8_t *) &msg->data.misc;
        // O2_DBq(o2_dbg_msg("MQTT_send", msg, &msg->data, NULL, NULL));
        O2_DBq(printf("MQTT_send payload_len (msg len) %d\n", payload_len));
        rslt = mqtt_comm.publish(key, payload, payload_len, "", 0);
        O2_FREE(msg);
    }
    return rslt;
}


// There are two kinds of MQTT_info: (1) Every remote proc connected over
// MQTT has an MQTT_info reachable by full name as a service. (2) There is
// one MQTT_info pointed to by mqtt_info that represents the connection to
// the MQTT broker. This one has no key (it's NULL).
//
MQTT_info::~MQTT_info()
{
    O2_DBo(o2_fds_info_debug_predelete(fds_info));
    if (!key) {  // represents entire MQTT protocol
        while (o2_mqtt_procs.size() > 0) {
            // free the last one to avoid N^2 algorithm
            delete o2_mqtt_procs.pop_back();
        }
        o2_mqtt_procs.finish();  // deallocate storage too
        delete_fds_info();
    } // else represents a remote MQTT process
}


// connect to a remote process via MQTT. If from_disc is true, this
// connection request came through topic O2-<ensemble_name>, so we
// should send a /dy to the other process to get their services.
//
// name - the remote process name
//
O2err create_mqtt_connection(const char *name, bool from_disc)
{
    // if name already exists, then we've sent services to it and do not
    // need to do it again.
    Services_entry *services = *Services_entry::find(name);
    if (services) {
        return O2_SUCCESS;
    }
    MQTT_info *mqtt = new MQTT_info(name, O2TAG_MQTT | O2TAG_OWNED_BY_TREE);
    Services_entry::service_provider_new(mqtt->key, NULL, mqtt, mqtt);
    // add this process name to the list of mqtt processes
    o2_mqtt_procs.push_back(mqtt);

    if (from_disc) {
        // We can address this to _o2 instead of the full name because
        // we are sending it directly over MQTT to the destination process.
        o2_send_start();
        o2_add_string(o2_ctx->proc->key);
        O2message_ptr msg = o2_message_finish(0.0, "!_o2/mqtt/dy", true);
        assert(msg->data.length == 12 + o2_strsize("!_o2/mqtt/dy") +
               o2_strsize(",s") + o2_strsize(o2_ctx->proc->key));
        O2_DBq(o2_dbg_msg("create_mqtt_connection", msg, &msg->data,
                          NULL, NULL));
        o2_prepare_to_deliver(msg);
        RETURN_IF_ERROR(mqtt->send(false));
    }
    O2err err = O2_SUCCESS;
    if (!err) err = o2_send_clocksync_proc(mqtt);
    if (!err) err = o2_send_services(mqtt);
    return err;
}


static void o2_mqtt_discovery_handler(o2_msg_data_ptr msg, const char *types,
                           O2arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(msg);
    O2arg_ptr name_arg = o2_get_next(O2_STRING);
    if (!name_arg) {
        return;
    }
    create_mqtt_connection(name_arg->s, false);
}


// Similar to strchr(s, c), but this function does not assume a terminating
// end-of-string zero because MQTT strings are not terminated. The end
// parameter is the address of the next character AFTER s
static char *find_before(char *s, const char c, const char *end)
{
    while (s < end) {
        if (*s == c) {
            return s;
        }
        s++;
    }
    return NULL;
}

void send_callback_via_mqtt(const char *name)
{
    O2message_ptr msg = o2_make_dy_msg(o2_ctx->proc, true,
                                        O2_DY_CALLBACK);
    mqtt_comm.publish(name, (const uint8_t *) O2_MSG_PAYLOAD(msg),
                      msg->data.length, "", 0);
}

// handler for mqtt discovery message
// payload should be of the form @xxxxxxxx:yyyyyyyy:ddddd/dy
// or @xxxxxxxx:yyyyyyyy:ddddd/cs
// payload may be altered and restored by this function, hence not const
//
void O2_MQTTcomm::disc_handler(char *payload, int payload_len)
{
    O2_DBq(printf("%s entered o2_mqtt_disc_handler\n", o2_debug_prefix));
    // need 3 strings: public IP, intern IP, port, clock status
    char *end = payload + payload_len;
    char *public_ip = payload + 1;
    char *internal_ip = NULL;
    char *port_num = NULL;
    char *action = NULL;
    char *end_ptr = find_before(public_ip, ':', end);
    if (end_ptr ) {
        *end_ptr  = 0;
        internal_ip = end_ptr  + 1;
        end_ptr  = find_before(internal_ip, ':', end);
        if (end_ptr ) {
            *end_ptr  = 0;
            port_num = end_ptr  + 1;
            end_ptr  = find_before(port_num, '/', end);
            if (end_ptr ) {
                *end_ptr = 0;
                action = end_ptr + 1;
            }
        }
    }
    char port_string[8];
    if (!action ||  // careful: "dy" and "cs" are not zero-terminated
        !((action[0] == 'd' && action[1] == 'y') ||   // "dy"
          (action[0] == 'c' && action[1] == 's'))) {  // "cs"
#ifndef NDEBUG
        // PRINT FOR DEBUGGING ONLY: payload IS NOT TERMINATED AND UNSAFE:
        printf("o2_mqtt_disc_handler could not parse payload:\n%s\n",
               payload);
#endif
        return;
    }
    // copy port field so it can be zero-terminated:
    memcpy(port_string, port_num, action - port_num);
    int port = o2_hex_to_int(port_string);
    O2_DBq(printf("%s o2_mqtt_disc_handler got %s %s %x\n", o2_debug_prefix,
                  public_ip, internal_ip, port));
    
    // we need the name for the remote process with zero padding for lookup
    char name[O2_MAX_PROCNAME_LEN];
    snprintf(name, O2_MAX_PROCNAME_LEN, "@%s:%s:%s%c%c%c%c",
             public_ip, internal_ip, port_string, 0, 0, 0, 0);
    assert(o2_ctx->proc->key);
    
    if (streql(name, o2_ctx->proc->key)) {
        return;  // we just received our own discovery message
    }
    
    // action is "cs" or "dy". Handle the "cs" case first:
    if (action[0] == 'c') {
        o2_send_cmd("/_o2/cs/cs", 0.0, "s", name);
        return;
    }
    // else action is "dy"...
    
    // CASE 1 (See doc/mqtt.txt): remote is not behind NAT
    // (public_ip == internal_ip) OR if remote and local share public IP
    // (public_ip == o2n_public_ip). Either way, remote ID is internal_ip
    int cmp = strcmp(o2_ctx->proc->key, name);
    if (streql(public_ip, internal_ip)) {
        // CASE 1A: we are the client
        if (cmp < 0) {
            O2_DBq(printf("%s o2_mqtt_disc_handler 1A\n", o2_debug_prefix));
            o2_discovered_a_remote_process(public_ip, internal_ip,
                                           port, O2_DY_INFO);
        } else if (cmp > 0) {  // CASE 1B: we are the server
            // CASE 1B1: we can receive a connection request
            if (streql(o2n_public_ip, o2n_internal_ip)) {
                O2_DBq(printf("%s o2_mqtt_disc_handler 1B1\n",
                              o2_debug_prefix));
                o2_discovered_a_remote_process(public_ip, internal_ip,
                                               port, O2_DY_INFO);
            } else {  // CASE 1B2: must create an MQTT connection
                O2_DBq(printf("%s o2_mqtt_disc_handler 1B2\n",
                              o2_debug_prefix));
                create_mqtt_connection(name, true);
            }
        } else {  // we got our own name
            O2_DBq(printf("%s we \"discovered\" our own name; ignored.\n",
                          o2_debug_prefix));
            return;
        }
        
    } else { // CASE 2: process is behind NAT
        // CASE 2A: we are the client
        if (cmp < 0) {
            create_mqtt_connection(name, true);
        } else if (cmp > 0) {  // CASE 2B: we are the server
            if (streql(o2n_public_ip, o2n_internal_ip)) {
                // CASE 2B1: send O2_DY_CALLBACK via MQTT
                O2_DBq(printf("%s o2_mqtt_disc_handler 2B1\n",
                              o2_debug_prefix));
                send_callback_via_mqtt(name);
            } else { // CASE 2B2: we are behind NAT
                O2_DBq(printf("%s o2_mqtt_disc_handler 2B2\n",
                              o2_debug_prefix));
                create_mqtt_connection(name, true);
            }
        } else {  // we got our own name
            return;
        }
    }
    // reconstruct payload just to be non-destructive:
    if (internal_ip) internal_ip[-1] = ':';
    if (port_num) port_num[-1] = ':';
}


// Two kinds of incoming MQTT messages: From O2-<ensemble>/disc, we get
// the full O2 name. From O2-<full_O2_name>, we get whole O2 messages.
//
void O2_MQTTcomm::deliver_mqtt_msg(const char *topic, int topic_len,
                                   uint8_t *payload, int payload_len)
{
    O2_DBq(printf("%s deliver_mqtt_msg topic %s payload_len %d\n",
                  o2_debug_prefix, topic, payload_len));
    // see if topic matches O2-ensemble/@public:intern:port string
    if (strncmp("O2-", topic, 3) == 0) {
        size_t o2_ens_len = 3 + strlen(o2_ensemble_name);  // counts "O2-"
        // test for topic == O2-ens/@pip:iip:port
        if (o2_ens_len < strlen(topic) &&
            memcmp(o2_ensemble_name, topic + 3, o2_ens_len - 3) == 0 &&
            topic[o2_ens_len] == '/' &&
            strncmp(o2_ctx->proc->key, topic + o2_ens_len + 1,
                    topic_len - (o2_ens_len + 1)) == 0 &&
            // last check insures topic contains all of proc->key:
            o2_ens_len + 1 + strlen(o2_ctx->proc->key) == topic_len)  {
            // match, so send the message.
            O2message_ptr msg = o2_message_new(payload_len);
            memcpy(O2_MSG_PAYLOAD(msg), payload, payload_len);
#if IS_LITTLE_ENDIAN
            o2_msg_swap_endian(&msg->data, false);
#endif
            O2_DBq(o2_dbg_msg("deliver_mqtt_msg", msg, &msg->data,
                              NULL, NULL));
            o2_message_send(msg);
        } else if (strncmp(o2_ensemble_name, topic + 3, topic_len - 8) == 0 &&
                   strncmp(topic + topic_len - 5, "/disc", 5) == 0) {
            O2_DBq(printf("%s deliver_mqtt_msg (disc)\n", o2_debug_prefix));
            // discovered a process through MQTT bridge
            disc_handler((char *) payload, payload_len);
        }
    } else {
        // TODO: DEBUGGING ONLY: FIX THIS
        printf("Unexpected MQTT message.");
    }
}


O2err MQTT_info::deliver(o2n_message_ptr o2n_msg)
{
    O2message_ptr msg = (O2message_ptr) o2n_msg;
    char *data = (char *) &msg->data.misc;
    int len = msg->data.length;
    mqtt_comm.deliver(data, len);
    O2_FREE(msg);
    return O2_SUCCESS;
}    


#endif
