// stun.c -- use stun server to get public IP address
//
// Roger B. Dannenberg
// August 2020
// based on github.com/node/turn-client/blob/master/c-stun-client-demo.c

#ifndef O2_NO_MQTT

#include "o2internal.h"
#include "services.h"
#include "msgsend.h"
#include "pathtree.h"
#include "o2sched.h"
#include "discovery.h"
#include "mqtt.h"

static o2n_info_ptr public_ip_info = NULL;
static o2n_address stun_server_address;
static int port = 0;
static int stun_try_count = 0;

typedef struct binding_req {
    short stun_method;
    short msg_length;
    int32_t magic_cookie;
    int32_t transaction_id[3];
} binding_req, *binding_req_ptr;

typedef struct binding_reply {
    short response;        // 0
    short n;               // 2
    int32_t fill[4];       // 4
    unsigned char data[8]; // 20
} binding_reply, *binding_reply_ptr;

    
// this helps prevent/cancel spurious calls to o2_get_public_ip()
bool o2_stun_query_running = false;

void o2_stun_query(o2_msg_data_ptr msgdata, const char *types,
                   o2_arg_ptr *argv, int argc, const void *user_data)
{
    if (o2n_public_ip[0]) {
        o2_stun_query_running = false;
        return;
    }
    if (stun_try_count >= 5) {  // give up
        strcpy(o2n_public_ip, "0.0.0.0");
        o2_init_phase2();
        o2_stun_query_running = false;
        return;
    }
    o2_message_ptr msg = (o2_message_ptr) O2_MALLOC(80);
    binding_req_ptr brp = (binding_req_ptr) &msg->data.flags; // after length
    brp->stun_method = htons(0x0001);
    brp->msg_length = 0;
    brp->magic_cookie = htonl(0x2112A442);
    brp->transaction_id[0] = htonl(0x63c7117e);
    brp->transaction_id[1] = htonl(0x0714278f);
    brp->transaction_id[2] = htonl(0x5ded3221);
    msg->data.length = sizeof(binding_req);
    o2n_send_udp_via_info(public_ip_info, &stun_server_address,
                          (o2n_message_ptr) msg);
    stun_try_count++;
    o2_send_start();
    msg = o2_message_finish(o2_local_time() + 2, "!_o2/ipq", true);
    o2_prepare_to_deliver(msg);
    o2_schedule(&o2_ltsched);
}


// only called from o2_discovery_initialize() which is called by o2_initialize()
// If the network changes, you MUST restart O2 because the "unique" local process
// name will change if the public port becomes available or changes.
o2_err_t o2_get_public_ip()
{
    if (o2_stun_query_running || o2n_public_ip[0]) {
        printf("---- o2_get_public_ip - o2_stun_query_running %d "
               "o2n_public_ip %s\n", o2_stun_query_running, o2n_public_ip);
        return O2_ALREADY_RUNNING; // started already
    }
    stun_client_ptr client = O2_MALLOCT(stun_client_info);
    client->tag = STUN_CLIENT;
    public_ip_info = o2n_udp_server_new(&port, true, client);
    o2n_address_init(&stun_server_address, "stun.l.google.com", 19302, false);
    // schedule stun_query until we get a reply
    o2_method_new_internal("/_o2/ipq", "", &o2_stun_query, NULL, false, false);
    o2_stun_query_running = true;  // do not reset until O2 is initialized again
    o2_stun_query(NULL, NULL, NULL, 0, NULL);
    return O2_SUCCESS;
}


void o2_stun_reply_handler(void *info)
{
    o2_message_ptr msg = o2_postpone_delivery();
    binding_reply_ptr brp = (binding_reply_ptr) &msg->data.flags;
    // brp is now based just after the message length
    if (brp->response == htons(0x0101)) {
        // int n = htons(brp->n);
        unsigned char *ptr = brp->data;
        while ((char *) ptr < PTR(&msg->data.flags) + msg->data.length) {
            short attr_type = htons(*(short *) ptr);
            ptr += sizeof(short);
            short attr_len = htons(*(short *) ptr);
            if (attr_type == 0x0020) {
                ptr += 4;
                // short port = ntohs(*(short *) ptr);
                // port ^= 0x2112;
                ptr += 2;
                sprintf(o2n_public_ip, "%u.%u.%u.%u", ptr[0] ^ 0x21,
                        ptr[1] ^ 0x12, ptr[2] ^ 0xA4, ptr[3] ^ 0x42);
                // if you get the IP address, close the socket
                o2n_close_socket(public_ip_info);
                o2_init_phase2();
                break;
            }
            ptr += 4 + attr_len;
        }
    }
    O2_FREE(msg);
}

#endif
