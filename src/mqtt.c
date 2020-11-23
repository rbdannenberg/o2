// mqtt.c -- MQTT protocol extension
//
// Roger B. Dannenberg
// August 2020

/* This extension provides discovery and communication between O2 processes
that are not on the same LAN and are possibly behind NAT. See o2/doc/mqtt.txt
for design details */

#ifndef O2_NO_MQTT

#include "o2internal.h"
#include "mqttcomm.h"
#include "mqtt.h"
#include "discovery.h"
#include "message.h"
#include "services.h"
#include "msgsend.h"

dyn_array o2_mqtt_procs;
o2n_address mqtt_address;
// standard "dot" format IP address for broker:
char mqtt_broker_ip[O2_IP_LEN] = "";
// records that mqtt_enable() was called and should be initialized asap.
bool o2_mqtt_waiting_for_public_ip = false;


o2_err_t o2_mqtt_enable(const char *broker, int port_num)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    if (!broker) broker = "mqtt.eclipse.org";
    if (port_num == 0) port_num = 1883; // default for MQTT
    // look up the server to get the IP address. That way, we can get the
    // blocking call out of the way when the process starts up, and we
    // can return an error synchronously if the server cannot be found.
    // We cannot actually connect until we know our public IP address,
    // which we are getting from a stun server asynchronously since UDP
    // could could result in several retries and should be non-blocking.
    o2_err_t err = o2n_address_init(&mqtt_address, broker, 1883, true);
    if (err) {
        return err;
    }
    if (!inet_ntop(AF_INET, (void *) o2n_address_get_in_addr(&mqtt_address),
                   mqtt_broker_ip, sizeof mqtt_broker_ip)) {
        perror("converting mqtt ip to string");
        return O2_FAIL;
    }
    printf("o2_mqtt_enable %s with IP %s\n", broker, mqtt_broker_ip);
    DA_INIT(o2_mqtt_procs, proc_info_ptr, 0);
    return o2_mqtt_initialize();
}


o2_err_t o2_mqtt_initialize()
{
    if (!o2n_network_enabled) {
        return O2_NO_NETWORK;
    }
    if (!o2n_public_ip[0]) {
        o2_mqtt_waiting_for_public_ip = true;
        return O2_SUCCESS;
    }
    // make MQTT broker connection
    o2m_initialize(mqtt_broker_ip, o2n_address_get_port(&mqtt_address));
    // subscribe to O2-<ensemblename>/disc
    char topic[O2_MAX_NAME_LEN + 16];
    topic[0] = 'O'; topic[1] = '2'; topic[2] = '-';
    assert(strlen(o2_ensemble_name) <= O2_MAX_NAME_LEN); // enforced by o2_initialize
    strcpy(topic + 3, o2_ensemble_name);
    strcat(topic + 3, "/disc");
    o2m_subscribe(topic);  // topic is O2-<ensemblename>/disc
    // send name to O2-<ensemblename>/disc, retain is off.
    assert(o2_ctx->proc->name);
    o2_mqtt_publish(topic, (const uint8_t *) o2_ctx->proc->name,
                    strlen(o2_ctx->proc->name), 0);
    // subscribe to O2-<public ip>:<local ip>:<port>
    strcpy(topic + 3, o2_ctx->proc->name);
    o2m_subscribe(topic);  // topic is O2-<public ip>:<local ip>:<port>
    return O2_SUCCESS;
}


void o2_mqtt_send(proc_info_ptr proc, o2_message_ptr msg)
{
    printf("o2_mqtt_send: ");
    o2_message_print(msg);
    const char *topic = proc->name;
    int payload_len = msg->data.length;
    const uint8_t *payload = (const uint8_t *) &msg->data.flags;
    o2_mqtt_publish(topic, payload, payload_len, 0);
}


void o2_mqtt_free(proc_info_ptr proc)
{
    // proc must actually be o2m_mqtt_client. It is static, so do not free it.
    // But, we are closing connection, so we have MQTT procs to free:
    int length = o2_mqtt_procs.length;
    while (length > 0) {
        // free the last one to avoid N^2 algorithm
        o2_proc_info_free(DA_LAST(o2_mqtt_procs, proc_info_ptr));
        // as a side effect, o2_proc_info_free will remove one entry
        length--;
        // in debug mode, make sure this iteration made progress; if we
        // fail to remove an entry, this loop never terminates
        assert(o2_mqtt_procs.length == length);
    }
    DA_FINISH(o2_mqtt_procs);
}


// Similar to strchr(s, ':'), but this function does not assume a terminating
// end-of-string zero because MQTT strings are not terminated. The end
// parameter is the address of the next character AFTER s
static char *find_colon(char *s, const char *end)
{
    while (s < end) {
        if (*s == ':') {
            return s;
        }
        s++;
    }
    return NULL;
}


void create_mqtt_connection(const char *name)
{
    proc_info_ptr mqtt = O2_CALLOCT(proc_info);
    mqtt->tag = MQTT_NOCLOCK;
    mqtt->name = o2_heapify(name);
    o2_service_provider_new(mqtt->name, NULL,
                            (o2_node_ptr) mqtt, mqtt);
    // add this process name to the list of mqtt processes
    DA_APPEND(o2_mqtt_procs, proc_info_ptr, mqtt);
}


void send_callback_via_mqtt(const char *name)
{
    o2_message_ptr msg = o2_make_dy_msg(o2_ctx->proc, true,
                                        O2_DY_CALLBACK);
    char topic[O2_MAX_NAME_LEN + 16];
    topic[0] = 'O'; topic[1] = '2'; topic[2] = '-';
    strcpy(topic + 3, name);
    o2_mqtt_publish(topic, (const uint8_t *) O2_MSG_PAYLOAD(msg),
                    msg->data.length, 0);
}


// handler for mqtt discovery message
// payload should be of the form xxx.xxx.xxx.xxx:yyy.yyy.yyy.yyy:ddddd
// payload may be altered and restored by this function, hence not const
//
void o2_mqtt_disc_handler(char *payload, int payload_len)
{
    // need 3 strings: public IP, intern IP, port
    char *end = payload + payload_len;
    const char *public_ip = payload;
    char *internal_ip = NULL;
    char *port_num = NULL;
    char *colon_ptr = find_colon(payload, end);
    if (colon_ptr) {
        *colon_ptr = 0;
        internal_ip = colon_ptr + 1;
        colon_ptr = find_colon(internal_ip, end);
        if (colon_ptr) {
            *colon_ptr = 0;
            port_num = colon_ptr + 1;
        }
    }
    char port_string[8];
    if (!port_num || port_num >= end || port_num + 7 < end) {
        // FOR DEBUGGING ONLY: payload IS NOT TERMINATED AND UNSAFE:
        printf("o2_mqtt_disc_handler could not parse payload:\n%s\n",
               payload);
        return;
    }
    // copy port field so it can be zero-terminated:
    int port_string_len = end - port_num;
    memcpy(port_string, port_num, port_string_len);
    port_string[port_string_len] = 0;
    int port = atoi(port_string);
    
    // we need the name for the remote process with zero padding for lookup
    char name[O2_MAX_PROCNAME_LEN];
    snprintf(name, O2_MAX_PROCNAME_LEN, "%s:%s:%s%c%c%c%c",
             public_ip, internal_ip, port_string, 0, 0, 0, 0);
    assert(o2_ctx->proc->name);

    // CASE 1 (See doc/mqtt.txt): remote is not behind NAT
    // (public_ip == internal_ip) OR if remote and local share public IP
    // (public_ip == o2n_public_ip). Either way, remote ID is internal_ip
    int cmp = strcmp(o2_ctx->proc->name, name);
    if (streql(public_ip, internal_ip)) {
        // CASE 1A: we are the client
        if (cmp < 0) {
            o2_discovered_a_remote_process(public_ip, internal_ip,
                                           port, O2_DY_INFO);
        } else if (cmp > 0) {  // CASE 1B: we are the server
            // CASE 1B1: we can receive a connection request
            if (streql(o2n_public_ip, o2n_internal_ip)) {
                o2_discovered_a_remote_process(public_ip, internal_ip,
                                               port, O2_DY_INFO);
            } else {  // CASE 1B2: must create an MQTT connection
                create_mqtt_connection(name);
            }
        } else {  // we got our own name
            return;
        }
        
    } else { // CASE 2: process is behind NAT
        // CASE 2A: we are the client
        if (cmp < 0) {
            o2_discovered_a_remote_process(public_ip, internal_ip,
                                           port, O2_DY_INFO);
        } else if (cmp > 0) {  // CASE 2B: we are the server
            if (streql(o2n_public_ip, o2n_internal_ip)) {
                // CASE 2B2: send O2_DY_CALLBACK via MQTT
                send_callback_via_mqtt(name);
            } else { // CASE 2B2: we are behind NAT
                create_mqtt_connection(name);
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
void o2m_deliver_mqtt_msg(const char *topic, int topic_len,
                          uint8_t *payload, int payload_len)
{
    // see if topic matches public:intern:port string
    if (strncmp("O2-", topic, 3) == 0) {
        if (strncmp(o2_ctx->proc->name, topic + 3, topic_len - 3) == 0) {
            // match, so send the message.
            o2_message_ptr msg = o2_message_new(payload_len);
            memcpy(O2_MSG_PAYLOAD(msg), payload, payload_len);
            o2_prepare_to_deliver(msg);
            o2_message_send_sched(true);
        } else if (strncmp(o2_ensemble_name, topic + 3, topic_len - 8) == 0 &&
                   strncmp(topic + topic_len - 5, "/disc", 5) == 0) {
            // discovered a process through MQTT bridge
            o2_mqtt_disc_handler((char *) payload, payload_len);
        }
    } else {
        // TODO: DEBUGGING ONLY: FIX THIS
        printf("Unexpected MQTT message.");
    }
}

#endif
