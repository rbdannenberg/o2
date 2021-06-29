// o2zcdisc.h -- ZeroConf interface and discovery
//

#ifndef O2_NO_ZEROCONF

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


void o2_zc_register_record(int port);


#ifdef __APPLE__
#include <dns_sd.h>

class Bonjour_info : public Zc_info {
public:
    DNSServiceRef sd_ref;  // use NULL to indicate the connection has ended
    Bonjour_info(DNSServiceRef sr);
    ~Bonjour_info();
    virtual O2err deliver(O2netmsg_ptr msg);
};


O2err o2_zcdisc_initialize();

#elif __linux__

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>

void zc_cleanup();

class Avahi_info : public Zc_info {
public:
    AvahiWatch *avahi_watch;
    AvahiWatchEvent last_event;
    bool incallback;
    AvahiWatchCallback callback;
    void *userdata;

    Avahi_info(AvahiWatch *watch, int fd, AvahiWatchCallback callback,
               void *userdata);
    ~Avahi_info();
    AvahiWatchEvent get_events() const {
        return incallback ? last_event : (AvahiWatchEvent) 0; }
    void set_watched_events(AvahiWatchEvent event);
    virtual O2err deliver(O2netmsg_ptr msg);
    void remove();
    virtual O2err writeable();
};

O2err o2_zcdisc_initialize();
#endif

#endif
