// o2zcdisc.h -- ZeroConf interface and discovery
//

#ifndef O2_NO_ZEROCONF
#ifdef __linux__
#define USE_AVAHI 1
#endif
#ifdef __APPLE__
#define USE_BONJOUR 1
#endif
#ifdef WIN32
#define USE_BONJOUR 1
#endif

class Zc_info : public Proxy_info {
public:
    Fds_info *info;
    Zc_info() : Proxy_info(NULL, O2TAG_ZC) { };
    virtual ~Zc_info() { };
    
    virtual O2err connected() { return O2_FAIL; }  // we never connect
    virtual O2err accepted(Fds_info *conn) { return O2_FAIL; }  // not a server
    virtual O2err deliver(O2netmsg_ptr msg) = 0;
    bool local_is_synchronized() { return false; } // not a service
};

O2err o2_zcdisc_initialize();

void o2_zc_register_record(int port);


#ifdef USE_BONJOUR
#include <dns_sd.h>

class Bonjour_info : public Zc_info {
public:
    DNSServiceRef sd_ref;  // use NULL to indicate the connection has ended
    Bonjour_info(DNSServiceRef sr);
    ~Bonjour_info();
    virtual O2err deliver(O2netmsg_ptr msg);
};


#elif USE_AVAHI

void o2_poll_avahi();
void zc_cleanup();

#endif

#endif
