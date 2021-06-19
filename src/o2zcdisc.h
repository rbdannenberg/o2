// o2zcdisc.h -- ZeroConf interface and discovery
//

#ifndef O2_NO_ZEROCONF
#include <dns_sd.h>

class Zc_info : public Proxy_info {
public:
    DNSServiceRef sd_ref;  // use NULL to indicate the connection has ended
    Fds_info *info;
    
    Zc_info(DNSServiceRef sr);
    
    ~Zc_info();
    
    virtual O2err connected() { return O2_FAIL; }  // we never connect
    virtual O2err accepted(Fds_info *conn) { return O2_FAIL; }  // not a server
    virtual O2err deliver(O2netmsg_ptr msg);
    bool local_is_synchronized() { return false; } // not a service
};


Zc_info *o2_zc_register_record(int port);
O2err o2_zcdisc_initialize();
#endif
