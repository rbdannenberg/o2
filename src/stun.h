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

class Stun_info : public Proxy_info {
  public:
    Stun_info(Fds_info *fds_info_) : Proxy_info(NULL, STUN_CLIENT) {
        fds_info = fds_info_;
        fds_info->owner = this;
    }
    ~Stun_info();
    
    // Implement the Net_interface
    O2err accepted(Fds_info *conn) { return O2_FAIL; };  // not TCP
    O2err connected() { return O2_FAIL; }  // not a TCP client
    O2err deliver(o2n_message_ptr msg);
};


// used to detect duplicate calls to o2_get_public_ip():
extern bool o2_stun_query_running;

O2err o2_get_public_ip();

void o2_stun_query(o2_msg_data_ptr msgdata, const char *types,
                   O2arg_ptr *argv, int argc, const void *user_data);

void o2_stun_reply_handler(void *info);
