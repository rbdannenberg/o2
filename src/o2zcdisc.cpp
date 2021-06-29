// o2zcdisc -- zeroconf/bonjour discovery for O2
//
// Roger B. Dannenberg
// Feb 2021

// This file is divided into sections:
//    0. Heading: #include's
//    1. Bonjour API implementation
//    2. avahi-client API implementation
//    3. Common functions

#ifndef O2_NO_ZEROCONF

/****************************/
/* SECTION 0: include files */
/****************************/

#include "o2internal.h"
#include "discovery.h"
#include "hostip.h"
#include "pathtree.h"
#include "o2zcdisc.h"

#ifdef __APPLE__

/*****************************************/
/* SECTION 1: Bonjour API implementation */
/*****************************************/

// To "harden" this discovery process, we need to worry about error codes
// and failure to receive callbacks.
// - DNSServiceRegister and DNSServiceBrowse errors, since they happen
//   early, will simply shut down discovery. No real recovery here.
// - zc_register_callback and zc_browse_callback errors are reported, but
//   since this is a callback that stays active, there is nothing to do
//   but wait for the next callback.
// - DNSServiceResolve errors mean we will not be able to look up a name.
//   We could try again every 1s.
// - zc_resolve_callback may never be called or may be called with an error,
//   in which case it is ignored. If it fails, we should consider the
//   DNSServiceResolve to have failed, shut down resolve_info, and call
//   resolve() for the next pending name. If it succeeds, we should remove
//   the name from resolve_pending, so let's just do a linear search to find
//   the name.

// The logic is tricky: We keep all un-resolved names in resolve_pending.
// We want to resolve the list quickly, but sequentially to avoid opening
// a pile of TCP connections to the Bonjour/ZeroConf server. But if there
// is a failure, we want to retry every 1s. So we keep a marker with each
// name to indicate we have not yet tried to resolve.
// In addition, we keep a watchdog timer running that is scheduled for 1s
// after each DNSServiceResolve call (whether it fails or not). We use a
// sequence number so only the last scheduled watchdog event will do
// anything.
// If DNSServiceResolve fails, we move the failing name to the beginning
// of resolve_pending (so it will not be revisited until every other name
// has been revisited). Then we return after setting timer for 1s.
// If the watchdog with the current serial number wakes up, it means
// DNSServiceResolve was called 1s ago, and either it failed and we need to
// retry, or it succeeded but we never got a callback. If we did not get
// a callback, resolve_info is non-null and we should call close_socket().
// Then, we are ready for another attept at DNSServiceResolve.
// When the callback is called, we need to remove the name from
// resolve_pending, close the connection, and set resolve_info to NULL.

// Set to a new Bonjour_info instance to listen to a socket from
// DNSServiceResolve.
// Set to NULL when Bonjour_info socket is marked for closure. (It may take time
// to actually close the socket by later calling delete on Bonjour_info.)
static Bonjour_info *resolve_info = NULL;
static int watchdog_seq = 0;  // sequence number to cancel watchdog callback

typedef struct {
    const char *name;
    bool asap;  // process as soon as possible (first time)
} resolve_type;

Vec<resolve_type> resolve_pending;

static void resolve();

Bonjour_info::Bonjour_info(DNSServiceRef sr) {
    sd_ref = sr;
    // install socket
    info = new Fds_info(DNSServiceRefSockFD(sd_ref),
                        NET_TCP_CLIENT, 0, this);
    info->read_type = READ_CUSTOM;  // we handle everything
}


Bonjour_info::~Bonjour_info() {
   // do some garbage collection: we use resolve_info to keep
   // track of Zc_info created by DNSServiceResolve so that when
   // we get the data we need from a callback, we can close the
   // connection. But if the connection closes without the
   // callback, we have a dangling pointer blocking further calls
   // to discover processes indicated by resolve_pending.
   // (Probably, Zc_info should be in the context pointer passed
   // to DNSServiceResolve, but it's easier to call
   // DNSServiceResolve first, then create Zc_info, which uses
   // the socket returned by DNSServiceResolve.)
   if (this == resolve_info) {
       resolve_info = NULL;
   }
   DNSServiceRefDeallocate(sd_ref);
   sd_ref = NULL;
}

O2err Bonjour_info::deliver(O2netmsg_ptr msg) {
    // this is called when there is data from the Bonjour/Zeroconf server
    // to be processed by the library: pass it on...
    DNSServiceErrorType err;
    err = DNSServiceProcessResult(sd_ref);
    if (err) {
        fprintf(stderr, "DNSServiceProcessResult returns %d, "
                "ending O2 discovery\n", err);
        return O2_FAIL;
    }
    return O2_SUCCESS;
}

// check for len-char hex string
static bool check_hex(const char *addr, int len)
{
    for (int i = 0; i < len; i++) {
        char c = addr[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

// check for valid name since we will try to use name as IP addresses
// returns true if format is valid. Also returns the udp_port number
// extracted from the end of name, and modifies name by padding with
// zeros after the tcp_port (erasing most of the field with udp_port)
// so that name, on return, is an O2string suitable for service lookup.
static bool is_valid_proc_name(char *name, int port,
                               char *internal_ip, int *udp_port)
{
    if (!name) return false;
    if (strlen(name) != 28) return false;
    if (name[0] != '@') return false;
    // must have 8 lower case hex chars starting at name[1] followed by ':'
    if (!check_hex(name + 1, 8)) return false;
    if (name[9] != ':') return false;
    if (!check_hex(name + 10, 8)) return false;
    if (name[18] != ':') return false;
    // internal IP is copied to internal_ip
    o2_strcpy(internal_ip, name + 10, 9);
    // must have 4-digit hex tcp port number matching port
    if (!check_hex(name + 19, 4)) return false;
    int top = o2_hex_to_byte(name + 19);
    int bot = o2_hex_to_byte(name + 21);
    int tcp_port = (top << 8) + bot;
    if (tcp_port != port) return false;  // name must be consistent
    if (name[23] != ':') return false;
    // must find 4-digit hex udp port number
    if (!check_hex(name + 24, 4)) return false;
    top = o2_hex_to_byte(name + 24);
    bot = o2_hex_to_byte(name + 26);
    *udp_port = (top << 8) + bot;
    // pad O2 name with zeros to a word boundary (only one zero needed)
    name[23] = 0;
    return true;
}


void zc_resolve_callback(DNSServiceRef sd_ref, DNSServiceFlags flags,
                         uint32_t interface_index, DNSServiceErrorType err,
                         const char *fullname, const char *hosttarget,
                         uint16_t port, uint16_t txt_len,
                         const unsigned char *txt_record, void *context)
{
    port = ntohs(port);
    fprintf(stderr, "zc_resolve_callback err %d name %s hosttarget %s port %d"
            " len %d\n", err, fullname, hosttarget, port, txt_len);
    uint8_t proc_name_len;
    const char *proc_name = (const char *) TXTRecordGetValuePtr(txt_len,
                                    txt_record, "name", &proc_name_len);
    if (proc_name_len == 28) {  // names are fixed length -- reject if invalid
        char name[32];
        o2_strcpy(name, proc_name, proc_name_len + 1);  // extra 1 for EOS
        // remove name from resolve_pending
        int i;
        bool found_it = false;
        for (i = 0; i < resolve_pending.size(); i++) {
            if (streql((const char *) context, resolve_pending[i].name)) {
                resolve_pending.erase(i);
                found_it = true;
                break;
            }
        }
        if (!found_it) {
            fprintf(stderr, "zc_resolve_callback could not find this name %s\n",
                    (char *) context);
        }
        fprintf(stderr, "    proc name: %s\n", name);
        char internal_ip[O2N_IP_LEN];
        int udp_port = 0;
        if (is_valid_proc_name(name, port, internal_ip, &udp_port)) {
            o2_discovered_a_remote_process_name(name, internal_ip,
                                                port, udp_port, O2_DY_INFO);
        }
    }
    if (resolve_info) {
        resolve_info->info->close_socket(true);
        resolve_info = NULL;
    } else {
        fprintf(stderr, "zc_resolve_callback with null resolve_info\n");
    }
    resolve();  // no-op if empty
}


void set_watchdog_timer()
{
    o2_send_start();
    o2_add_int32(++watchdog_seq);
    O2message_ptr m = o2_message_finish(o2_local_time() + 1, "!_o2/dydog", true);
    o2_schedule_msg(&o2_ltsched, m);
}


void resolve()
{
    DNSServiceRef sd_ref;
    DNSServiceErrorType err;
    while (!resolve_info && resolve_pending.size() > 0) {
        resolve_type rt = resolve_pending.last();
        const char *name = rt.name;
        fprintf(stderr, "Setting up DNSServiceResolve for %s\n", name);
        err = DNSServiceResolve(&sd_ref, 0, kDNSServiceInterfaceIndexAny, name,
                                "_o2proc._tcp.", "local", zc_resolve_callback,
                                (void *) name);
        if (err) {
            fprintf(stderr, "DNSServiceResolve returned %d for %s\n",
                    err, name);
        } else {  // create resolve_info
            resolve_info = new Bonjour_info(sd_ref);
        }
        // move name to the beginning of the list
        // (to be revisited after all others)
        resolve_pending.pop_back();  // we've already copied to rt
        resolve_pending.insert(0, rt);
        set_watchdog_timer();
    }
}


void zc_register_callback(DNSServiceRef sd_ref, DNSServiceFlags flags,
                          DNSServiceErrorType err, const char *name,
                          const char *regtype, const char *domain,
                          void *context)
{
    fprintf(stderr, "zc_register_callback err %d registered %s as %s domain %s\n",
           err, name, regtype, domain);
}


void resolve_watchdog(o2_msg_data_ptr msg, const char *types,
                      O2arg_ptr *argv, int argc, const void *user_data)
{
    if (argv[0]->i != watchdog_seq) {
        return;  // a newer timer is set; ignore this one
    }
    if (resolve_info) {  // connection is still open, no response for 1s
        resolve_info->info->close_socket(true); // this will clean up
        resolve_info = NULL;
    }
    resolve();  // try again
}


static void rr_callback(DNSServiceRef sd_ref, DNSRecordRef record_ref,
                        DNSServiceFlags flags, DNSServiceErrorType err,
                        void *context)
{
    printf("rr_callback sd_ref %p record_ref %p flags %d err %d\n",
           sd_ref, record_ref, flags, err);
}



static Bonjour_info *zc_register(const char *type_domain,  const char *host,
                            int port, int text_end, const char *text)
{
    DNSServiceRef sd_ref;
    DNSServiceErrorType err =
            DNSServiceRegister(&sd_ref, kDNSServiceInterfaceIndexAny, 0, 
                               o2_ensemble_name, type_domain, NULL, host,
                               htons(port), text_end, text,
                               zc_register_callback, NULL);
    if (err) {
        fprintf(stderr, "DNSServiceRegister returned %d, "
                "O2 discovery is not possible.\n", err);
        return NULL;
    }
    // make a handler for the socket that was returned
    return new Bonjour_info(sd_ref);
}


void o2_zc_register_record(int port)
{
    char ipdot[O2N_IP_LEN];
    o2_hex_to_dot(o2n_internal_ip, ipdot);
    in_addr_t addr = inet_addr(ipdot);

    char fullname[64];
    o2_strcpy(fullname, o2_ensemble_name, 64);
    int len = strlen(fullname);
    if (len > 63 - 6) {
        return;
    }
    strcpy(fullname + len, ".local");

    DNSServiceRef sd_ref;
    DNSServiceErrorType err;
    err = DNSServiceCreateConnection(&sd_ref);
    if (err) {
        return;
    }

    DNSRecordRef record_ref;
    err =  DNSServiceRegisterRecord(sd_ref, &record_ref,
            kDNSServiceFlagsUnique, 0 /*kDNSServiceFlagsIncludeP2P*/, fullname,
            kDNSServiceType_A, kDNSServiceClass_IN, sizeof(addr), &addr,
            240, rr_callback, NULL);
    if (err != kDNSServiceErr_NoError) {
        fprintf(stderr, "Error: DNSServiceRegisterRecord failed.\n");
        DNSServiceRefDeallocate(sd_ref);
    }

    Bonjour_info *zcr_info = zc_register("_http._tcp.", fullname,
                                         port, 0, NULL);
    if (zcr_info) {
        new Bonjour_info(sd_ref);
    } else {
        DNSServiceRefDeallocate(sd_ref);
    }
}


static void zc_browse_callback(DNSServiceRef sd_ref, DNSServiceFlags flags,
                uint32_t interfaceIndex, DNSServiceErrorType err,
                const char *name, const char *regtype,
                const char *domain, void *context)
{
    fprintf(stderr, "zc_browse_callback err %d flags %d name %s as %s "
            "domain %s\n", err, flags, name, regtype, domain);
    // match if ensemble name is a prefix of name, e.g. "ensname (2)"
    if ((flags & kDNSServiceFlagsAdd) &&
        (strncmp(o2_ensemble_name, name, strlen(o2_ensemble_name)) == 0)) {
        resolve_type *rt = resolve_pending.append_space(1);
        rt->name = o2_heapify(name);
        rt->asap = true;
        resolve();
    }
}


// Start discovery with zeroconf/bonjour ...
// Assumes we have an ensemble name and proc name with public IP
//
O2err o2_zcdisc_initialize()
{
    // Publish a service with type _o2proc._tcp, name ensemblename
    // and text record name=@xxxxxxxx:yyyyyyyy:zzzz
    char text[64];
    strcpy(text + 1, "name=");
    int text_end = 6 + strlen(o2_ctx->proc->key);
    strcpy(text + 6, o2_ctx->proc->key);  // proc->key is (currently) 24 bytes
    // for discovery, we need udp port too, so append it after ':'
    text[text_end++] = ':';
    sprintf(text + text_end, "%04x", o2_ctx->proc->udp_address.get_port());
    text_end += 4; 
    text[0] = text_end - 1;  // text[0] (length) does not include itself
    fprintf(stderr, "Setting up DNSServiceRegister\n");
    Bonjour_info *zcreg = zc_register("_o2proc._tcp.", NULL,
                                 o2_ctx->proc->fds_info->port, text_end, text);
    if (zcreg == NULL) {
        return O2_FAIL;
    }

    // create a browser
    DNSServiceRef sd_ref;
    fprintf(stderr, "Setting up DNSServiceBrowse\n");
    DNSServiceErrorType err = DNSServiceBrowse(&sd_ref, 0, 
                                kDNSServiceInterfaceIndexAny, "_o2proc._tcp.",
                                NULL, zc_browse_callback, NULL);
    if (err) {
        fprintf(stderr, "DNSServiceBrowse returned %d, "
                "O2 discovery is not possible.\n", err);
        delete zcreg;
        return O2_FAIL;
    }

    // make a handler for the socket that was returned
    new Bonjour_info(sd_ref);

    o2_method_new_internal("/_o2/dydog", "i", &resolve_watchdog,
                           NULL, false, true);

    return O2_SUCCESS;
}

#elif __linux__

/**********************************************/
/* SECTION 2: avahi-client API implementation */
/**********************************************/

// note on naming: Avahi uses "avahi" prefix, so all of our
// avahi-related names in O2 use "zc".

// API for Avahi to use sockets

// Avahi delegates socket management and timer functions to
// the application (this code), using callbacks and object pointers
// as a flexible common ground.  Avahi knows about file descriptors
// (sockets) as well.
//
// We define:
//     AvahiTimeout -- the timer object given to Avahi
//     AvahiWatch -- the socket event object given to Avahi
// Each of these is paired with an O2 object:
//     O2Timeout -- a timer object implemented on O2 scheduling
//     Avahi_info -- a socket object based on Zc_info, which is
//                   common to Avahi and Bonjour implementations
// Note that these object reference each other:
//     AvahiTimeout.o2_timeout points to an O2Timeout
//     O2Timeout.avahi_timeout points back to the AvahiTimeout
//
//     AvahiWatch.info points to an Avahi_info
//     Avahi_info.avahi_watch points back to the AvahiWatch


struct AvahiWatch {
    Avahi_info *info;
};


// Timeout design: Avahi can create a timeout and cancel it or
// reschedule it.  It appears that Avahi can also free it, so it must
// be a heap object created with avahi_new(). O2 can schedule local
// message delivery.  When the O2 message is delivered, we need a way
// to check if the Avahi timeout still exists and has not been
// rescheduled.  I think the most direct solution is 2 objects:
// AvahiTimeout and O2Timeout, which point to each other.
//     For O2Timeout, the message will contain a blob which is the
// address of the O2Timeout. The O2Timeout is unlinked and freed when
// the message is received. When O2 shuts down, after we free the
// scheduler and all pending messages, we can use a doubly linked
// list to free all the O2Timeout objects remaining.
//     Each O2Timeout is created for an AvahiTimeout update. The
// objects point to each other until the event or until the timeout
// is cancelled at which time the links are set to NULL.  Note that
// the links are either both present or both NULL, so there are
// never dangling pointers or confusing over whether an event is
// pending.

struct O2Timeout;

struct AvahiTimeout {
    O2Timeout *o2_timeout;
    AvahiTimeoutCallback callback;
    void *userdata;
};

typedef struct O2Timeout {
    struct O2Timeout *prev;
    struct O2Timeout *next;
    AvahiTimeout *avahi_timeout;
} O2Timeout;

// to make initialization easy, linked list is not circular! Last node
// points to NULL; Head's prev node is NULL:
O2Timeout o2_timeouts = {NULL, NULL, NULL};  // list of all pending timeouts


static short map_events_to_poll(AvahiWatchEvent events)
{
    return (events & AVAHI_WATCH_IN ? POLLIN : 0) |
           (events & AVAHI_WATCH_OUT ? POLLOUT : 0) |
           (events & AVAHI_WATCH_ERR ? POLLERR : 0) |
           (events & AVAHI_WATCH_HUP ? POLLHUP : 0);
}


static AvahiWatchEvent map_events_from_poll(int events)
{
    const AvahiWatchEvent NONE = (AvahiWatchEvent) 0;
    return (AvahiWatchEvent)
           ((events & POLLIN ? AVAHI_WATCH_IN : NONE) |
            (events & POLLOUT ? AVAHI_WATCH_OUT : NONE) |
            (events & POLLERR ? AVAHI_WATCH_ERR : NONE) |
            (events & POLLHUP ? AVAHI_WATCH_HUP : NONE));
}


static AvahiWatch *zc_watch_new(const AvahiPoll *api, int fd,
                                AvahiWatchEvent events,
                                AvahiWatchCallback callback, void *userdata)
{
    AvahiWatch *w = avahi_new(AvahiWatch, 1);
    if (!w) {
        return NULL;
    }
    w->info = new Avahi_info(w, fd, callback, userdata);
    w->info->set_watched_events(events);
    O2_DBz(printf("%s: zc_watch_new %p socket %d events %x\n",
                  o2_debug_prefix, w, fd, events));
    return w;
}


static void zc_watch_update(AvahiWatch *w, AvahiWatchEvent events)
{
    O2_DBz(printf("%s: zc_watch_update %p events %x\n",
                  o2_debug_prefix, w, events));
    w->info->set_watched_events(events);
}


static AvahiWatchEvent zc_watch_get_events(AvahiWatch *w)
{
    return map_events_from_poll(w->info->get_events());
}


static void zc_watch_free(AvahiWatch *w)
{
    if (w->info->fds_info) {
        w->info->fds_info->close_socket(true);
        // note: closing socket does not delete w->info
        // since this is a READ_CUSTOM type Fds_info
    }
    O2_DBz(printf("%s: zc_watch_free %p\n", o2_debug_prefix, w));
    w->info->avahi_watch = NULL;
    delete w->info;
    w->info = NULL;
    // it appears that Avahi uses avahi_free to release w
    // TODO: try to confirm that w is deleted by Avahi
}    


// handler for /_o2/at (timeout event)
static void o2_zcdisc_timeout(const o2_msg_data_ptr msg, const char *types,
                              O2arg_ptr *argv, int argc,
                              const void *user_data)
{
    // first, get the O2timeout pointer from the blob:
    o2_extract_start(msg);
    O2arg_ptr arg = o2_get_next(O2_BLOB);
    O2Timeout *o2_timeout = *(O2Timeout **) arg->b.data;
    assert(o2_timeout);
    AvahiTimeout *t = o2_timeout->avahi_timeout;

    // dispose of o2_timeout after unlinking:
    o2_timeout->prev->next = o2_timeout->next;
    if (o2_timeout->next) {
        o2_timeout->next->prev = o2_timeout->prev;
    }
    O2_DBz(printf("%s: timeout callback, o2_timeout %p->%p\n",
                  o2_debug_prefix, o2_timeout, t));
    O2_FREE(o2_timeout);
    // remove dangling pointer to O2Timeout and notify Avahi of timeout:
    if (t) {
        t->o2_timeout = NULL;
        t->callback(t, t->userdata);
    }
}


static void zc_timeout_free(AvahiTimeout *t)
{
    O2_DBz(printf("%s: zc_timeout_free %p\n", o2_debug_prefix, t));
    // cancel pending O2timeout if any
    if (t && t->o2_timeout) {
        t->o2_timeout->avahi_timeout = NULL;
    }
    t->o2_timeout = NULL;
}


static void zc_timeout_update(AvahiTimeout *t, const struct timeval *tv)
{
    O2_DBz(printf("%s: zc_timeout_update %p calling timeout_free...\n",
                  o2_debug_prefix, t));
    zc_timeout_free(t);  // does not free t; just cancels timeout event
    if (!tv) {  // NULL tv means simply cancel the timeout event
        return;
    }
    // create new O2timeout and schedule wakeup message
    struct timeval nowtv;
    gettimeofday(&nowtv, NULL);
    O2time o2time = o2_local_time() + (tv->tv_sec - nowtv.tv_sec) +
                    (tv->tv_usec - nowtv.tv_usec) * 0.000001;

    t->o2_timeout = O2_MALLOCT(O2Timeout);
    O2Timeout *o2_timeout = t->o2_timeout;
    O2_DBz(printf("%s: zc_timeout_update t=%p->%p to local time %g\n",
                  o2_debug_prefix, t, o2_timeout, o2time));
    o2_timeout->avahi_timeout = t;
    // link to front of doubly-linked list
    o2_timeout->next = o2_timeouts.next;
    o2_timeout->prev = &o2_timeouts;
    o2_timeouts.next = o2_timeout;
    if (o2_timeout->next) o2_timeout->next->prev = o2_timeout;

    // schedule a message
    o2_send_start();
    // note that we are adding the pointer, not copying the object:
    o2_add_blob_data((uint32_t) sizeof(o2_timeout), &o2_timeout);
    O2message_ptr timeout_msg = o2_message_finish(o2time, "!_o2/at", true);
    o2_schedule_msg(&o2_ltsched, timeout_msg);
    return;
}



static AvahiTimeout *zc_timeout_new(const AvahiPoll *api,
                                    const struct timeval *tv,
                                    AvahiTimeoutCallback callback,
                                    void *userdata)
{
    AvahiTimeout *t;

    assert(api);
    assert(callback);

    if (!(t = avahi_new(AvahiTimeout, 1))) {
        return NULL;
    }

    t->o2_timeout = NULL; // tells update there is no previous timeout
    t->callback = callback;
    t->userdata = userdata;
    O2_DBz(printf("%s: zc_timeout_new created %p, calling update...\n",
                  o2_debug_prefix, t));
    zc_timeout_update(t, tv);
    return t;

}


Avahi_info::Avahi_info(AvahiWatch *watch, int fd,
                       AvahiWatchCallback callback_, void *userdata_)
{
    avahi_watch = watch;
    info = new Fds_info(fd, NET_TCP_CLIENT, 0, this);
    info->read_type = READ_CUSTOM;  // we handle everthing
    info->write_type = WRITE_CUSTOM;
    callback = callback_;
    userdata = userdata_;
}


// I think this should only be called in response to Avahi closing
// the socket, either because a socket error is reported or because
// Avahi connection is shut down.  If we close the socket while 
// Avahi is using it, I expect bad things will happen.
//
// Note that this deletion normally caused by zc_watch_free().
//
Avahi_info::~Avahi_info()
{
    if (avahi_watch) {
        printf("WARNING: Avahi_info deleted but its "
               "avahi_watch %p still exists", avahi_watch);
        avahi_watch = NULL;  // be extra safe
        callback = NULL;
    }
}

void Avahi_info::set_watched_events(AvahiWatchEvent evts)
{
    info->set_events(map_events_to_poll(evts));
}
        

// remove is called when POLLHUP is raised
void Avahi_info::remove()
{
    fds_info = NULL; // fds_info is removing itself
}


O2err Avahi_info::writeable()
{
    // the socket has become writeable - same handling as any event
    return deliver(NULL);
}


O2err Avahi_info::deliver(O2netmsg_ptr msg)
{
    // msg is NULL because read_type is READ_CUSTOM
    (*callback)(avahi_watch, info->get_socket(),
                map_events_from_poll(info->get_events()), userdata);
    info->set_events(0);
    return O2_SUCCESS;
}


static AvahiServiceBrowser *zc_sb = NULL;
static AvahiClient *zc_client = NULL;  // global access to avahi-client API

// AvahiPoll structure so Avahi can watch sockets:
static AvahiPoll zc_poll = {NULL, zc_watch_new, zc_watch_update, 
                            zc_watch_get_events, zc_watch_free, 
                            zc_timeout_new, zc_timeout_update,
                            zc_timeout_free};

// These globals keep everything we are allocating -- it's not stated
// in Avahi docs what happens to objects passed into it, so we'll
// free them ourselves if we are not explicitly told to free them by
// Avahi. If Avahi takes ownership and frees either _name or _text
// objects, we may end up freeing dangling pointers. Hopefully, this
// will be detected in testing and not released. Yikes... why isn't
// Avahi documented sufficiently for reliable use?

static AvahiEntryGroup *zc_group = NULL;
static AvahiEntryGroup *zc_http_group = NULL;
static char *zc_name = NULL;
static char *zc_text = NULL;
static char *zc_http_name = NULL;
static char *zc_http_text = NULL;

static O2err zc_create_services(AvahiClient *c);


static void zc_shutdown()
{
    if (zc_sb) {
        avahi_service_browser_free(zc_sb);
        zc_sb = NULL;
    }
    if (zc_client) {
        avahi_client_free(zc_client);
        zc_client = NULL;
    }
}


void zc_cleanup()
{
    zc_shutdown();
    /* TODO: we need this code to clean up
    O2Timeout *oto;
    while ((oto = o2_timeouts.next)) {
        o2_timeouts.next = oto->next;
        O2_FREE(oto);
    }
    */
}



/*
static Avahi_info *zc_register(const char *type_domain,  const char *host,
                               int port, int text_end, const char *text)
{
    
}
*/


static void zc_resolve_callback(AvahiServiceResolver *r,
                                AVAHI_GCC_UNUSED AvahiIfIndex interface,
                                AVAHI_GCC_UNUSED AvahiProtocol protocol,
                                AvahiResolverEvent event,
                                const char *name, const char *type,
                                const char *domain, const char *host_name,
                                const AvahiAddress *address,
                                uint16_t port, AvahiStringList *txt,
                                AvahiLookupResultFlags flags,
                                AVAHI_GCC_UNUSED void* userdata)
{
    assert(r);
    /* Called whenever a service has been resolved successfully or timed out */
    switch (event) {
        case AVAHI_RESOLVER_FAILURE:
            fprintf(stderr, "(Resolver) Failed to resolve service '%s' of "
                    "type '%s' in domain '%s': %s\n", name, type, domain,
                    avahi_strerror(avahi_client_errno(
                                       avahi_service_resolver_get_client(r))));
            break;
        case AVAHI_RESOLVER_FOUND: {
            char a[AVAHI_ADDRESS_STR_MAX], *t;
            fprintf(stderr, "Service '%s' of type '%s' in domain '%s':\n",
                    name, type, domain);
            avahi_address_snprint(a, sizeof(a), address);
            t = avahi_string_list_to_string(txt);
            fprintf(stderr,
                    "\t%s:%u (%s)\n"
                    "\tTXT=%s\n"
                    "\tcookie is %u\n"
                    "\tis_local: %i\n"
                    "\tour_own: %i\n"
                    "\twide_area: %i\n"
                    "\tmulticast: %i\n"
                    "\tcached: %i\n",
                    host_name, port, a,
                    t,
                    avahi_string_list_get_service_cookie(txt),
                    !!(flags & AVAHI_LOOKUP_RESULT_LOCAL),
                    !!(flags & AVAHI_LOOKUP_RESULT_OUR_OWN),
                    !!(flags & AVAHI_LOOKUP_RESULT_WIDE_AREA),
                    !!(flags & AVAHI_LOOKUP_RESULT_MULTICAST),
                    !!(flags & AVAHI_LOOKUP_RESULT_CACHED));
            avahi_free(t);
        }
    }
    avahi_service_resolver_free(r);
}


static void zc_browse_callback(AvahiServiceBrowser *b,
                               AvahiIfIndex interface,
                               AvahiProtocol protocol,
                               AvahiBrowserEvent event,
                               const char *name,
                               const char *type,
                               const char *domain,
                               AVAHI_GCC_UNUSED AvahiLookupResultFlags flags,
                               void* userdata)
{
    AvahiClient *c = (AvahiClient *) userdata;
    assert(b);
    /* Called whenever a new services becomes available on the LAN or 
       is removed from the LAN */
    switch (event) {
        case AVAHI_BROWSER_FAILURE:
            fprintf(stderr, "(Browser) %s\n", avahi_strerror(
                     avahi_client_errno(avahi_service_browser_get_client(b))));
            zc_shutdown();
            return;
        case AVAHI_BROWSER_NEW:
            fprintf(stderr, "(Browser) NEW: service '%s' of type '%s' in "
                    "domain '%s'\n", name, type, domain);
            /* We ignore the returned resolver object. In the callback
               function we free it. If the server is terminated before
               the callback function is called the server will free
               the resolver for us. */
            if (!(avahi_service_resolver_new(c, interface, protocol, name,
                        type, domain, AVAHI_PROTO_UNSPEC, (AvahiLookupFlags) 0,
                        zc_resolve_callback, c)))
                fprintf(stderr, "Failed to resolve service '%s': %s\n",
                        name, avahi_strerror(avahi_client_errno(c)));
            break;
        case AVAHI_BROWSER_REMOVE:
            fprintf(stderr, "(Browser) REMOVE: service '%s' of type '%s' in "
                    "domain '%s'\n", name, type, domain);
            break;
        case AVAHI_BROWSER_ALL_FOR_NOW:
        case AVAHI_BROWSER_CACHE_EXHAUSTED:
            fprintf(stderr, "(Browser) %s\n",
                    event == AVAHI_BROWSER_CACHE_EXHAUSTED ?
                    "CACHE_EXHAUSTED" : "ALL_FOR_NOW");
            break;
    }
}


static void entry_group_callback(AvahiEntryGroup *g,
                                 AvahiEntryGroupState state,
                                 AVAHI_GCC_UNUSED void *userdata)
{
    assert(g == zc_group || zc_group == NULL);
    zc_group = g;
    /* Called whenever the entry group state changes */
    switch (state) {
        case AVAHI_ENTRY_GROUP_ESTABLISHED :
            /* The entry group has been established successfully */
            fprintf(stderr, "Service '%s' successfully established.\n",
                    zc_name);
            break;
        case AVAHI_ENTRY_GROUP_COLLISION : {
            char *n;
            /* A service name collision with a remote service
             * happened. Let's pick a new name */
            n = avahi_alternative_service_name(zc_name);
            avahi_free(zc_name);
            zc_name = n;
            fprintf(stderr, "Service name collision, renaming "
                    "service to '%s'\n", zc_name);
            /* And recreate the services */
            zc_create_services(avahi_entry_group_get_client(g));
            break;
        }
        case AVAHI_ENTRY_GROUP_FAILURE :
            fprintf(stderr, "Entry group failure: %s\n",
                    avahi_strerror(avahi_client_errno(
                                     avahi_entry_group_get_client(g))));
            /* Some kind of failure happened while we were registering 
               our services */
            zc_shutdown();
            break;
        case AVAHI_ENTRY_GROUP_UNCOMMITED:
        case AVAHI_ENTRY_GROUP_REGISTERING:
            ;
    }
}


O2err zc_commit_group(AvahiClient *c, char **name,
                      AvahiEntryGroup **group, const char *type, int port,
                      const char *text)
{
    char *n;
    int ret;
    if (!*group) {
        if (!(*group =
              avahi_entry_group_new(c, entry_group_callback, NULL))) {
            fprintf(stderr, "avahi_entry_group_new() failed: %s\n",
                    avahi_strerror(avahi_client_errno(c)));
            goto fail;
        }
        /* If the group is empty (either because it was just created, or
         * because it was reset previously, add our entries.  */
        if (avahi_entry_group_is_empty(*group)) {
            // Publish a service with type, name, and text
            fprintf(stderr, "Adding service group '%s'\n", *name);
            if ((ret = avahi_entry_group_add_service(*group, AVAHI_IF_UNSPEC,
                           AVAHI_PROTO_UNSPEC, (AvahiPublishFlags) 0,
                           *name, type, NULL, NULL, port, text, NULL)) < 0) {
                if (ret == AVAHI_ERR_COLLISION)
                    goto collision;
                fprintf(stderr, "Failed to add _o2proc._tcp service: %s\n",
                        avahi_strerror(ret));
                goto fail;
            }
            /* Tell the server to register the service */
            if ((ret = avahi_entry_group_commit(zc_group)) < 0) {
                fprintf(stderr, "Failed to commit entry group: %s\n",
                        avahi_strerror(ret));
                goto fail;
            }
        }
    }    
    return O2_SUCCESS;
collision:
    /* A service name collision with a local service happened. Let's
     * pick a new name */
    n = avahi_alternative_service_name(*name);
    avahi_free(*name);
    *name = n;
    fprintf(stderr, "Service name collision, renaming service to '%s'\n",
            *name);
    avahi_entry_group_reset(*group);
    return zc_commit_group(c, name, group, type, port, text);
fail:
    return O2_FAIL;
}


static O2err zc_create_services(AvahiClient *c)
{
    int ret;
    assert(c);
    // and text record name=@xxxxxxxx:yyyyyyyy:zzzz
    char text[64];
    strcpy(text, "name=");
    int text_end = 5 + strlen(o2_ctx->proc->key);
    strcpy(text + 5, o2_ctx->proc->key);  // proc->key is 24 bytes
    // for discovery, we need udp port too, so append it after ':'
    text[text_end++] = ':';
    sprintf(text + text_end, "%04x", o2_ctx->proc->udp_address.get_port());
    text_end += 4;
    text[text_end] = 0;
                    
    return zc_commit_group(c, &zc_name, &zc_group, "_o2proc._tcp", 
                           o2_ctx->proc->fds_info->port, text);
}


static void zc_client_callback(AvahiClient *c, AvahiClientState state,
                               AVAHI_GCC_UNUSED void * userdata)
{
    assert(c);
    /* Called whenever the client or server state changes */
    switch (state) {
        case AVAHI_CLIENT_S_RUNNING:
            /* The server has startup successfully and registered its host
             * name on the network, so it's time to create our services */
            zc_create_services(c);
            break;
        case AVAHI_CLIENT_FAILURE:
            fprintf(stderr, "Avahi client failure: %s\n",
                    avahi_strerror(avahi_client_errno(c)));
            zc_shutdown();
            break;
        case AVAHI_CLIENT_S_COLLISION:
            /* Let's drop our registered services. When the server is back
             * in AVAHI_SERVER_RUNNING state we will register them
             * again with the new host name. */
        case AVAHI_CLIENT_S_REGISTERING:
            /* The server records are now being established. This
             * might be caused by a host name change. We need to wait
             * for our own records to register until the host name is
             * properly established. */
            if (zc_group)
                avahi_entry_group_reset(zc_group);
            break;
        case AVAHI_CLIENT_CONNECTING:
            ;
    }
}


// DEBUG:
#include <avahi-common/simple-watch.h>


// Start discovery with avahi-client API
// Assumes we have an ensemble name and proc name with public IP
//
O2err o2_zcdisc_initialize()
{
    o2_method_new_internal("/_o2/at", "b", &o2_zcdisc_timeout, NULL,
                           false, false);
    int error;
    // need a copy because if there is a collision, zc_name is freed
    zc_name = avahi_strdup(o2_ensemble_name);

    zc_client = avahi_client_new(&zc_poll, (AvahiClientFlags) 0,
                                 &zc_client_callback, NULL, &error);
    if (!zc_client) {
        fprintf(stderr, "Failed to create Avahi client: %s\n",
                avahi_strerror(error));
        return O2_FAIL;
    }
    
    /* Create the service browser */
    if (!(zc_sb = avahi_service_browser_new(zc_client, AVAHI_IF_UNSPEC,
                      AVAHI_PROTO_UNSPEC, "_o2proc._tcp", NULL,
                      (AvahiLookupFlags) 0, zc_browse_callback, zc_client))) {
        fprintf(stderr, "Failed to create service browser: %s\n",
                avahi_strerror(avahi_client_errno(zc_client)));
        goto fail;
    }
    return O2_SUCCESS;
 fail:
    zc_shutdown();
    return O2_FAIL;
}


void o2_zc_register_record(int port)
{
    char ipdot[O2N_IP_LEN];
    o2_hex_to_dot(o2n_internal_ip, ipdot);
    in_addr_t addr = inet_addr(ipdot);

    char fullname[64];
    o2_strcpy(fullname, o2_ensemble_name, 64);
    int len = strlen(fullname);
    if (len > 63 - 6) {
        return;
    }
    strcpy(fullname + len, ".local");
    if (zc_http_name) avahi_free(zc_http_name);
    zc_http_name = avahi_strdup(o2_ensemble_name);

    zc_commit_group(zc_client, &zc_http_name, &zc_http_group,
                    "_http._tcp", port, "");
}



#endif

/*******************************/
/* SECTION 3: Common functions */
/*******************************/

#endif
