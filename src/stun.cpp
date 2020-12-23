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

static Stun_info *stun_info = NULL;
static Net_address stun_server_address;
static int stun_server_port = 0;
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


bool o2_stun_query_running = false;

// initiate stun protocol periodically by calling this with a timed msg
void o2_stun_query(o2_msg_data_ptr msgdata, const char *types,
                   O2arg_ptr *argv, int argc, const void *user_data)
{
    if (o2n_public_ip[0]) {
        o2_stun_query_running = false;
        return;
    }
    if (stun_try_count >= 5) {  // give up
        strcpy(o2n_public_ip, "00000000");
        o2_init_phase2();
        o2_stun_query_running = false;
        return;
    }
    o2n_message_ptr msg = (o2n_message_ptr) O2_MALLOC(80);
    binding_req_ptr brp = (binding_req_ptr) &msg->payload; // after length
    brp->stun_method = htons(0x0001);
    brp->msg_length = 0;
    brp->magic_cookie = htonl(0x2112A442);
    brp->transaction_id[0] = htonl(0x63c7117e);
    brp->transaction_id[1] = htonl(0x0714278f);
    brp->transaction_id[2] = htonl(0x5ded3221);
    msg->length = sizeof(binding_req);
    o2n_send_udp_via_info(stun_info->fds_info, &stun_server_address, msg);
    stun_try_count++;
    o2_send_start();
    o2_schedule_msg(&o2_ltsched, o2_message_finish(o2_local_time() + 2,
                                                   "!_o2/ipq", true));
}


// only called from o2_discovery_initialize() which is called by o2_initialize()
// If the network changes, you MUST restart O2 because the "unique" local
// process name will change if the public port becomes available or changes.
O2err o2_get_public_ip()
{
    if (o2_stun_query_running || o2n_public_ip[0]) {
        return O2_ALREADY_RUNNING; // started already
    }
    stun_try_count = 0;  // we get 5 tries every time we start
    Fds_info *fds_info = Fds_info::create_udp_server(&stun_server_port, true);
    stun_info = new Stun_info(fds_info);
    stun_server_address.init("stun.l.google.com", 19302, false);
    // schedule stun_query until we get a reply
    o2_method_new_internal("/_o2/ipq", "", &o2_stun_query, NULL, false, false);
    o2_stun_query_running = true;  // do not reset until O2 is initialized again
    o2_stun_query(NULL, NULL, NULL, 0, NULL);
    return O2_SUCCESS;
}


O2err Stun_info::deliver(o2n_message_ptr msg)
{
    binding_reply_ptr brp = (binding_reply_ptr) msg->payload;
    // brp is now based just after the message length
    if (brp->response == htons(0x0101)) {
        // int n = htons(brp->n);
        unsigned char *ptr = brp->data;
        while ((char *) ptr < PTR(msg->payload) + msg->length) {
            short attr_type = htons(*(short *) ptr);
            ptr += sizeof(short);
            short attr_len = htons(*(short *) ptr);
            if (attr_type == 0x0020) {
                ptr += 4;
                // short port = ntohs(*(short *) ptr);
                // port ^= 0x2112;
                ptr += 2;
                sprintf(o2n_public_ip, "%02x%02x%02x%02x", ptr[0] ^ 0x21,
                        ptr[1] ^ 0x12, ptr[2] ^ 0xA4, ptr[3] ^ 0x42);
                // if you get the IP address, close the socket
                stun_info->fds_info->close_socket();
                o2_init_phase2();
                break;
            }
            ptr += 4 + attr_len;
        }
    }
    O2_FREE(msg);
    return O2_SUCCESS;
}


Stun_info::~Stun_info()
{
    O2_DBo(o2_fds_info_debug_predelete(fds_info));
    delete_key_and_fds_info();
}

    
// this helps prevent/cancel spurious calls to o2_get_public_ip()
#endif
