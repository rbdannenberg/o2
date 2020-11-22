// stun.h -- get public IP address using stun server
//
// Roger B. Dannenberg
// August 2020

#define STUN_CLIENT        70 // used for MQTT protocol, getting public IP
#define ISA_STUN_CLIENT(p) ((p) && (p)->tag == STUN_CLIENT)

#ifdef O2_NO_DEBUG
#define TO_STUN_CLIENT(node) ((stun_client_ptr) (node))
#else
#define TO_STUN_CLIENT(node) (assert(ISA_STUN_CLIENT(node)), \
                              ((stun_client_ptr) (node)))
#endif 

typedef struct stun_client_info {
    int tag;
} stun_client_info, *stun_client_ptr;

// used to detect duplicate calls to o2_get_public_ip():
extern bool o2_stun_query_running;

o2_err_t o2_get_public_ip();

void o2_stun_query(o2_msg_data_ptr msgdata, const char *types,
                   o2_arg_ptr *argv, int argc, const void *user_data);

void o2_stun_reply_handler(void *info);
