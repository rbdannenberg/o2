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


static void o2_mqtt_discovery_handler(O2msg_data_ptr msg, const char *types,
                           O2arg_ptr *argv, int argc, const void *user_data);

// discovered remote processes reachable by MQTT:
Vec<MQTT_info *> o2_mqtt_procs;
// address of the MQTT Broker:
Net_address mqtt_address;
// standard "dot" format IP address for broker:
char mqtt_broker_ip[O2N_IP_LEN] = "";
// records that mqtt_enable() was called and should be initialized asap.
bool o2_mqtt_waiting_for_public_ip = false;
// connection to MQTT broker:
static MQTT_info *mqtt_info = NULL;


class O2_MQTTcomm : public MQTTcomm {
public:
    O2err msg_send(O2netmsg_ptr msg, bool block) {
        if (!o2_ctx->building_message_lock) {
            return O2_FAIL;  // not message was even started
        }
        o2_ctx->building_message_lock = false;
        if (!mqtt_info || !mqtt_info->fds_info) {
            return O2_FAIL;
        }
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
    if (!broker || broker[0] == 0) broker = "mqtt.eclipseprojects.io";
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
    O2_DBq(dbprintf("o2_mqtt_enable %s with IP %s\n", broker, mqtt_broker_ip));
    o2_mqtt_procs.init(0);
    return o2_mqtt_initialize();
}


O2err o2_mqtt_send_disc()
{
    // send name to O2-<ensemblename>/disc, retain is off.
    if (!o2_ctx->proc->key || !mqtt_info) { // no name and no mqtt connection
        return O2_FAIL;
    }
    O2_DBq(dbprintf("publishing to O2-%s/disc with payload %s/%s\n",
                    o2_ensemble_name, o2_ctx->proc->key,
                    o2_clock_is_synchronized ? "cs" : "dy"));
    char suffix[20];
    int udp_port = o2_ctx->proc->udp_address.get_port();
    snprintf(suffix, 19, ":%04x", udp_port);
    strcpy(suffix + 5, o2_clock_is_synchronized ? "/cs/" : "/dy/");
    o2_version(suffix + 9);  // append version number
    mqtt_comm.publish("disc", (const uint8_t *) o2_ctx->proc->key,
                      (int) strlen(o2_ctx->proc->key), suffix, 0, false);
    return O2_SUCCESS;
}


static O2err mqtt_ping_sched()
{
    o2_send_start();
    O2message_ptr msg = o2_message_finish(o2_local_now + MQTT_KEEPALIVE_PERIOD,
                                          "!_o2/mqtt/ps", false);
    assert(msg->next == NULL);
    return o2_schedule_msg(&o2_ltsched, msg);
}


static void mqtt_ping_send(O2msg_data_ptr msg, const char *types,
                           O2arg_ptr *argv, int argc, const void *user_data)
{
    o2_mqtt_send_disc();
    mqtt_ping_sched();
}


static O2err mqtt_check_timeouts_sched()
{
    o2_send_start();
    return o2_schedule_msg(&o2_ltsched,
                o2_message_finish(o2_local_now + MQTT_CHECK_TIMEOUTS_PERIOD,
                                  "!_o2/mqtt/ct", false));
}

static void mqtt_check_timeouts(O2msg_data_ptr msg, const char *types,
                    O2arg_ptr *argv, int argc, const void *user_data)
{
    // check for expired MQTT processes
    for (int i = 0; i < o2_mqtt_procs.size(); i++) {
        MQTT_info *mqtt = o2_mqtt_procs[i];
        if (mqtt->timeout < o2_local_now) {
            o2_mqtt_procs.remove(i);
            i--; // this "hole" is filled with another, so check [i] again.
            mqtt->o2_delete();
        }
    }
    // this is a bit of a hack: if we call mqtt_check_timeouts() because
    // we received a "/bye" message, we do not want to reschedule because
    // this is already scheduled. To indicate this, we set user_data non-zero:
    if (user_data != NULL) {
        mqtt_check_timeouts_sched();
    }
}


O2err o2_mqtt_initialize()
{
    if (!o2n_internet_enabled) {
        return O2_NO_NETWORK;
    }
    if (!o2n_public_ip[0]) {
        o2_mqtt_waiting_for_public_ip = true;
        return O2_SUCCESS;
    }
    RETURN_IF_ERROR(o2_method_new_internal("/_o2/mqtt/dy", "s",
                            &o2_mqtt_discovery_handler, NULL, false, false));
    RETURN_IF_ERROR(o2_method_new_internal("/_o2/mqtt/ps", "",
                            &mqtt_ping_send, NULL, false, false));
    RETURN_IF_ERROR(o2_method_new_internal("/_o2/mqtt/ct", "",
                            &mqtt_check_timeouts, NULL, false, false));
    // make MQTT broker connection
    mqtt_info = new MQTT_info(NULL, O2TAG_MQTT_CON);
    mqtt_info->fds_info = Fds_info::create_tcp_client(&mqtt_address, mqtt_info);
#ifndef O2_NO_DEBUG
    mqtt_info->fds_info->set_description(o2_heapify("MQTTclient"));
#endif
    mqtt_info->fds_info->read_type = READ_RAW;
    O2_DBc(mqtt_info->co_info(mqtt_info->fds_info,
                              "created TCP CLIENT for MQTT broker"));

    RETURN_IF_ERROR(mqtt_comm.initialize(mqtt_broker_ip,
                                         mqtt_address.get_port()));
    // subscribe to O2-<ensemblename>/disc:
    RETURN_IF_ERROR(mqtt_comm.subscribe("disc", false));
    RETURN_IF_ERROR(mqtt_comm.subscribe(o2_ctx->proc->key, false));
    // start sending keep-alive messages every MQTT_KEEPALIVE_PERIOD:
    // first call is as if in a message handler:
    mqtt_ping_send(NULL, NULL, NULL, 0, NULL);
    // start checking for timeouts every MQTT_CHECK_TIMEOUTS_PERIOD:
    return mqtt_check_timeouts_sched();
}

O2err o2_mqtt_disconnect()
{
    if (!o2_ctx->proc->key || !mqtt_info) { // no name and no mqtt connection
        return O2_FAIL;
    }
    O2_DBq(dbprintf("sending /bye to close MQTT connection\n"));
    mqtt_comm.publish("disc", (const uint8_t *) o2_ctx->proc->key,
                      (int) strlen(o2_ctx->proc->key), "/bye", 0, true);
    return O2_SUCCESS;
}


O2err o2_mqtt_finish()
{
    if (mqtt_info) {
        mqtt_info->o2_delete();
        mqtt_info = NULL;
    }
    mqtt_comm.finish();
    return O2_SUCCESS;
}


O2err o2_mqtt_can_send()
{
    return (mqtt_info ?
            (mqtt_info->fds_info->out_message ? O2_BLOCKED : O2_SUCCESS) :
            O2_FAIL);
}


// send an O2 message to proc, which is an MQTT proc
// prerequisite: msg is in localhost byte order
// msg is freed before returning
//
O2err MQTT_info::send(bool block)
{
    O2err rslt;
    bool tcp_flag;
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
        O2_DBQ(printf("MQTT_send payload_len (msg len) %d\n", payload_len));
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
    O2_DBb(dbprintf("deleting MQTT_info@%p\n", this));
    O2_DBo(o2_fds_info_debug_predelete(fds_info));
    if (!key) {  // represents entire MQTT protocol
        while (o2_mqtt_procs.size() > 0) {
            // free the last one to avoid N^2 algorithm
            o2_mqtt_procs.pop_back()->o2_delete();
        }
        o2_mqtt_procs.finish();  // deallocate storage too
        delete_fds_info();
    } else {  // represents a remote MQTT process
        Services_entry::remove_services_by(this);
    }
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
    // need to do it again, but we need to update the timeout
    Services_entry *services = *Services_entry::find(name);
    if (services) {
        O2node *proc = services->services[0].service;
        if (ISA_MQTT(proc)) {  // should we just assert this?
            ((MQTT_info *) proc)->timeout = o2_local_time() +
                                            MQTT_TIMEOUT_PERIOD;
        }
        return O2_SUCCESS;
    }
    // note that O2TAG_OWNED_BY_TREE is not set. We will consider the owner
    // to be o2_mqtt_procs. We will leave it to the mqtt broker connection
    // (mqtt_info) to manage all the other MQTT_info instances that represent
    // remote processes.
    MQTT_info *mqtt = new MQTT_info(name, O2TAG_MQTT);
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
        O2_DBq(o2_dbg_msg("create_mqtt_connection request services from "
                          "remote process", msg, &msg->data, NULL, NULL));
        o2_prepare_to_deliver(msg);
        RETURN_IF_ERROR(mqtt->send(false));
    }
    O2err err = O2_SUCCESS;
    if (!err) err = o2_send_clocksync_proc(mqtt);
    if (!err) err = o2_send_services(mqtt);
    return err;
}


static void o2_mqtt_discovery_handler(O2msg_data_ptr msg, const char *types,
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
    O2message_ptr msg = o2_make_dy_msg(o2_ctx->proc, true, true,
                                       O2_DY_CALLBACK);
    mqtt_comm.publish(name, (const uint8_t *) O2_MSG_PAYLOAD(msg),
                      msg->data.length, "", 0);
}

// handler for mqtt discovery message
// payload should be of the form @xxxxxxxx:yyyyyyyy:ddddd/dy/vers
// or @xxxxxxxx:yyyyyyyy:ddddd/cs/vers
// payload may be altered and restored by this function, hence not const
//
void O2_MQTTcomm::disc_handler(char *payload, int payload_len)
{
    bool this_is_goodbye = false;
    bool proc_discovered = true;  // is the remote process known to us?
    int version;
    O2_DB(O2_DBq_FLAG | O2_DBd_FLAG,
          dbprintf("entered o2_mqtt_disc_handler\n"));
    // need 3 strings: public IP, intern IP, port, clock status
    char *end = payload + payload_len;
    char *public_ip = payload + 1;
    char *internal_ip = NULL;
    char *tcp_port_num = NULL;
    char *udp_port_num = NULL;
    char *action = NULL;
    char *vers_num = NULL;
    char *end_ptr = find_before(public_ip, ':', end);
    if (end_ptr) {
        *end_ptr  = 0;
        internal_ip = end_ptr  + 1;
        end_ptr = find_before(internal_ip, ':', end);
        if (end_ptr) {
            *end_ptr = 0;
            tcp_port_num = end_ptr + 1;
            end_ptr = find_before(tcp_port_num, ':', end);
            if (end_ptr) {
                *end_ptr = 0;
                udp_port_num = end_ptr + 1;
                end_ptr = find_before(udp_port_num, '/', end);
                if (end_ptr) {
                    *end_ptr = 0;
                    action = end_ptr + 1;
                    end_ptr = find_before(action, '/', end);
                    if (end_ptr) {
                        *end_ptr = 0;
                        vers_num = end_ptr + 1;
                    }
                }
            }
        }
    }
    // One message format is just the name followed by /bye. This parses
    // with tcp_port_num like "d923/bye" so we should look for "/bye":
    // Either it is intended or the sender is doing something bogus and
    // we should hang up anyway:
    char *bye = strstr(tcp_port_num, "/bye");
    if (bye) {
        this_is_goodbye = true;
        *bye = 0; // to make tcp_port_num point to a valid hex string
    } else if (!action || !vers_num ||
               !((action[0] == 'd' && action[1] == 'y') ||   // "dy"
                 (action[0] == 'c' && action[1] == 's'))) {  // "cs"
#ifndef NDEBUG
        // PRINT FOR DEBUGGING ONLY: payload IS NOT TERMINATED AND UNSAFE:
        printf("o2_mqtt_disc_handler could not parse payload:\n%s\n",
               payload);
#endif
        return;
    } else {
        if (!(version = o2_parse_version(vers_num, (int) (end - vers_num)))) {
#ifndef NDEBUG
            // PRINT FOR DEBUGGING ONLY: payload IS NOT TERMINATED AND UNSAFE:
            dbprintf("o2_mqtt_disc_handler could not parse payload "
                     "version (%s):\n%s\n", vers_num, payload);
#endif
            return;
        }
    }
    // note that this works even if tcp_port_num ends in /bye:
    int tcp_port = o2_hex_to_int(tcp_port_num);
    // and this works even if udp_port_num is "":
    int udp_port = udp_port_num ? o2_hex_to_int(udp_port_num) : 0;
    O2_DBq(dbprintf("o2_mqtt_disc_handler got %s %s %x %x\n",
                    public_ip, internal_ip, tcp_port, udp_port));
    
    // we need the name for the remote process with zero padding for lookup
    char name[O2_MAX_PROCNAME_LEN];
    snprintf(name, O2_MAX_PROCNAME_LEN, "@%s:%s:%s%c%c%c%c",
             public_ip, internal_ip, tcp_port_num, 0, 0, 0, 0);
    assert(o2_ctx->proc->key);
    // now that we have the name, we can handle "/bye":
    if (this_is_goodbye) {
        Services_entry *services = *Services_entry::find(name);
        if (services) {
            O2node *proc = services->services[0].service;
            if (ISA_MQTT(proc)) {  // should we just assert this?
                ((MQTT_info *) proc)->timeout = 0;
                mqtt_check_timeouts(NULL, NULL, NULL, 0, (void *) 1);
            }
            return;
        }

    }
    // action is "cs" or "dy". The "dy" message can be omitted if the
    // remote process already has clock sync, so we need to act as if
    // "dy" was sent first, then record the synchronization state if 
    // appropriate.

    // WARNING: the following should be cleaner and used to at least be
    // nested if-then-else's, but now we need to block peer-to-peer
    // connections to test MQTT when "F" is set (see O2_DBF in debug.h).
    // Therefore, the if-then-else's are largely broken up into a string
    // of simple if's that contain "goto wrap_up;" which would have been
    // a the end of the outermost if-then-else. By expressing this as
    // just if... if... if..., we can jump around the non-MQTT if...
    // options by preceding if... with O2_DBF(goto force_mqtt); Note that
    // if O2_NO_DEBUG is set, O2_DBF expands to the empty string, so there
    // is no test. We've inserted goto statements, but the compiler would
    // have done that to implement if-then-else.

    // CASE 1 (See doc/mqtt.txt): remote is not behind NAT
    // (public_ip == internal_ip) OR if remote and local share public IP
    // (public_ip == o2n_public_ip). Either way, remote ID is internal_ip
    int cmp = strcmp(o2_ctx->proc->key, name);
    if (cmp == 0) {
        O2_DBq(dbprintf("o2_mqtt_disc_handler \"discovered\" our own name; "
                        "ignored.\n"));
        return;
    }
    O2_DBF(goto force_mqtt);  // "F" flag blocks peer-to-peer connections
    if (streql(public_ip, internal_ip)) {
        // CASE 1A: we are the client
        if (cmp < 0) {
            O2_DBq(dbprintf("o2_mqtt_disc_handler public_ip = internal_ip, "
                            "we are the 'client'\n"));
            o2_discovered_a_remote_process_name(name, version, internal_ip,
                                  tcp_port, udp_port, O2_DY_INFO);
        } else { // (cmp > 0) -- CASE 1B: we are the server
            // CASE 1B1: we can receive a connection request
            if (streql(o2n_public_ip, o2n_internal_ip)) {
                O2_DBq(dbprintf("o2_mqtt_disc_handler public_ip = internal_"
                                "ip, we are the server\n"));
                o2_discovered_a_remote_process_name(name, version, internal_ip,
                                      tcp_port, udp_port, O2_DY_INFO);
                proc_discovered = false;  // waiting for them to connect
            } else {  // CASE 1B2: must create an MQTT connection
                O2_DBq(dbprintf("o2_mqtt_disc_handler public_ip = internal_ip,"
                           " must create MQTT connection\n"));
                create_mqtt_connection(name, true);
            }
        }
        goto wrap_up;
    }
    // CASE 2: process is behind NAT
    // CASE 2A: we have the same public IP
    if (streql(o2n_public_ip, public_ip)) {
        if (cmp > 0) {  // CASE 2A1: we are the server
            O2_DBq(dbprintf("o2_mqtt_disc_handler same public_ip, we are the "
                            "server\n"));
            send_callback_via_mqtt(name);
            proc_discovered = false;  // waiting for them to connect
        } else {  // (cmp < 0) -- CASE 2A2: we are the client
            O2_DBq(dbprintf("o2_mqtt_disc_handler same public_ip, we are the "
                            "client\n"));
            o2_discovered_a_remote_process_name(name, version, internal_ip,
                                  tcp_port, udp_port, O2_DY_INFO);
        }
        goto wrap_up;
    }
  force_mqtt:
    if (cmp < 0) {  // CASE 2B: we are the client
        create_mqtt_connection(name, true);
        goto wrap_up;
    } else {  // (cmp > 0) -- // CASE 2C: we are the server
        O2_DBF(goto force_mqtt_server);
        if (streql(o2n_public_ip, o2n_internal_ip)) {
            // CASE 2C1: send O2_DY_CALLBACK via MQTT
            O2_DBq(dbprintf("o2_mqtt_disc_handler 2C1\n"));
            send_callback_via_mqtt(name);
            proc_discovered = false;
        }
        goto wrap_up;
    }
  force_mqtt_server:
    // CASE 2C2: we are behind NAT
    O2_DBq(dbprintf("o2_mqtt_disc_handler behind NAT\n"));
    create_mqtt_connection(name, true);

  wrap_up:
    // reconstruct payload just to be non-destructive:
    if (internal_ip) internal_ip[-1] = ':';
    if (tcp_port_num) tcp_port_num[-1] = ':';
    if (udp_port_num) udp_port_num[-1] = ':';
    if (action) action[-1] = '/';

    // now, if this discovery message ended with /cs, we need to establish
    // that the remote proc has clock sync. However, if we just sent an
    // O2_DY_CALLBACK to the remote proc, then we are waiting for it to
    // connect and we have no proxy for it and no place to record the
    // clock synchronization status, so we can skip sending this message.
    if (action[0] == 'c' && proc_discovered) {
        o2_send_cmd("/_o2/cs/cs", 0.0, "s", name);
    }
}


// Two kinds of incoming MQTT messages: From O2-<ensemble>/disc, we get
// the full O2 name. From O2-<full_O2_name>, we get whole O2 messages.
//
void O2_MQTTcomm::deliver_mqtt_msg(const char *topic, int topic_len,
                                   uint8_t *payload, int payload_len)
{
    O2_DBQ(dbprintf("deliver_mqtt_msg topic %s payload_len %d\n",
                    topic, payload_len));
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
            O2_DB(O2_DBq_FLAG | O2_DBR_FLAG | O2_DBr_FLAG,
                  o2_dbg_msg("deliver_mqtt_msg", msg, &msg->data,
                              NULL, NULL));
            o2_message_send(msg);
        } else if (strncmp(o2_ensemble_name, topic + 3, topic_len - 8) == 0 &&
                   strncmp(topic + topic_len - 5, "/disc", 5) == 0) {
            O2_DBq(dbprintf("    deliver_mqtt_msg (disc)\n"));
            // discovered a process through MQTT bridge
            disc_handler((char *) payload, payload_len);
        }
    } else {
        // TODO: DEBUGGING ONLY: FIX THIS
        printf("Unexpected MQTT message.");
    }
}


O2err MQTT_info::deliver(O2netmsg_ptr o2n_msg)
{
    O2message_ptr msg = (O2message_ptr) o2n_msg;
    char *data = (char *) &msg->data.misc;
    int len = msg->data.length;
    mqtt_comm.deliver(data, len);
    O2_FREE(msg);
    return O2_SUCCESS;
}    


#endif
