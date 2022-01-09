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

#include <ctype.h>
#include "o2internal.h"
#include "discovery.h"
#include "hostip.h"
#include "pathtree.h"
#include "o2zcdisc.h"


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


#ifdef USE_BONJOUR

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
    char name[32];
    char internal_ip[O2N_IP_LEN];
    int udp_port = 0;

    // names are fixed length -- reject if invalid:
    if (!proc_name || proc_name_len != 28) {
        goto no_discovery;
    }
    o2_strcpy(name, proc_name, proc_name_len + 1);  // extra 1 for EOS
    O2_DBz(printf("%s got a TXT field: name=%s\n", o2_debug_prefix, name));
    {   // remove name from resolve_pending
        int i;
        bool found_it = false;
        for (i = 0; i < resolve_pending.size(); i++) {
            if (streql((const char *) context, resolve_pending[i].name)) {
                O2_FREE((void *) resolve_pending[i].name);
                resolve_pending.erase(i);
                found_it = true;
                break;
            }
        }

        if (!found_it) {
            fprintf(stderr, "zc_resolve_callback could not find this name %s\n",
                    (char *) context);
            fprintf(stderr, "    proc name: %s\n", name);
        }
    }
    if (!is_valid_proc_name(name, port, internal_ip, &udp_port)) {
        goto no_discovery;
    }
    {   // check for compatible version number
        int version;
        uint8_t vers_num_len;
        const char *vers_num = (const char *) TXTRecordGetValuePtr(txt_len,
                                         txt_record, "vers", &vers_num_len);
        O2_DBz(if (vers_num) {
                   printf("%s got a TXT field: vers=", o2_debug_prefix);
                   for (int i = 0; i < vers_num_len; i++) {
                       printf("%c", vers_num[i]); }
                   printf("\n"); });
        if (!vers_num ||
            (version = o2_parse_version(vers_num, vers_num_len)) == 0) {
            goto no_discovery;
        }
        o2_discovered_a_remote_process_name(name, version, internal_ip,
                                            port, udp_port, O2_DY_INFO);
    }   // fall through to clean up resolve_info...
  no_discovery:
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
        fprintf(stderr, "Setting up DNSServiceResolve for %s at %g\n", name,
                o2_local_time());
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


void resolve_watchdog(O2msg_data_ptr msg, const char *types,
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
    O2_DBz(printf("%s Registered port %d with text:\n", o2_debug_prefix, port);
           for (int i = 0; i < text_end; ) {
               printf("    ");
               for (int j = 0; j < text[i]; j++) printf("%c", text[i + 1 + j]);
               printf("\n"); i += 1 + text[i]; });
    // make a handler for the socket that was returned
    return new Bonjour_info(sd_ref);
}

#ifdef WIN32
typedef unsigned long in_addr_t;
#endif

void o2_zc_register_record(int port)
{
    char ipdot[O2N_IP_LEN];
    o2_hex_to_dot(o2n_internal_ip, ipdot);
    in_addr_t addr = inet_addr(ipdot);

    char fullname[64];
    o2_strcpy(fullname, o2_ensemble_name, 64);
    int len = (int) strlen(fullname);
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
    char text[80];
    strcpy(text + 1, "name=");
    int text_end = 6 + (int) strlen(o2_ctx->proc->key);
    strcpy(text + 6, o2_ctx->proc->key);  // proc->key is (currently) 24 bytes
    // for discovery, we need udp port too, so append it after ':'
    text[text_end++] = ':';
    sprintf(text + text_end, "%04x", o2_ctx->proc->udp_address.get_port());
    text_end += 4;
    text[0] = text_end - 1;  // text[0] (length) does not include itself

    // now add vers=2.0.0
    int vers_loc = text_end + 1;
    strcpy(text + vers_loc, "vers=");
    char *vers_num = text + vers_loc + 5;
    o2_version(vers_num);
    text_end = vers_loc + 5 + (int) strlen(vers_num);
    text[vers_loc - 1] = text_end - vers_loc;
    
    fprintf(stderr, "Setting up DNSServiceRegister\n");
    int port = o2_ctx->proc->fds_info->port;
    Bonjour_info *zcreg = 
            zc_register("_o2proc._tcp.", NULL, port, text_end, text);
    if (zcreg == NULL) {
        return O2_FAIL;
    }
    O2_DBz(printf("%s Registered port %d with text:\n", o2_debug_prefix, port);
           for (int i = 0; i < text_end; ) {
               printf("    ");
               for (int j = 0; j < text[i]; j++) printf("%c", text[i + 1 + j]);
               printf("\n"); i += 1 + text[i]; });

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


void o2_zcdisc_finish()
{
    for (int i = 0; i < resolve_pending.size(); i++) {
        O2_FREE((void *) resolve_pending[i].name);
    }
    resolve_pending.finish();
}

#elif USE_AVAHI

/**********************************************/
/* SECTION 2: avahi-client API implementation */
/**********************************************/

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>

// note on naming: Avahi uses "avahi" prefix, so all of our
// avahi-related names in O2 use "zc".

static AvahiServiceBrowser *zc_sb = NULL;
static AvahiClient *zc_client = NULL;  // global access to avahi-client API

// AvahiPoll structure so Avahi can watch sockets:
static AvahiSimplePoll *zc_poll = NULL;


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
static char *zc_http_name = NULL;
static int zc_http_port = 0;
static bool zc_running = false;

static O2err zc_create_services(AvahiClient *c);

// helper to deal with multiple free functions:
#define FREE_WITH(variable, free_function) \
    if (variable) { \
        free_function(variable); \
        variable = NULL; \
    }

static void o2_zcdisc_finish()
{
    O2_DBz(printf("%s o2_zcdisc_finish\n", o2_debug_prefix));
    // (I think) these are freed by avahi_client_free later, so
    // we remove the dangling pointers:
    zc_group = NULL;
    zc_http_group = NULL;
    zc_http_port = 0;
    
    FREE_WITH(zc_sb, avahi_service_browser_free);
    FREE_WITH(zc_client, avahi_client_free);
    FREE_WITH(zc_poll, avahi_simple_poll_free);
    FREE_WITH(zc_name, avahi_free);
    FREE_WITH(zc_http_name, avahi_free);
    zc_running = false;
    resolve_pending.finish();
}


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
            O2_DBz(printf("%s Avahi resolve service '%s' of type '%s' in "
                    "domain '%s':\n", o2_debug_prefix, name, type, domain));
            avahi_address_snprint(a, sizeof(a), address);
            char name[32];
            char internal_ip[O2N_IP_LEN];
            int udp_port = 0;
            int version = 0;
            name[0] = 0;
            for (AvahiStringList *asl = txt; asl; asl = asl->next) {
                printf("resolve callback text: %s\n", asl->text);
                if (strncmp((char *) asl->text, "name=", 5) == 0 &&
                    asl->size == 33) {  // found "name="; proc name len is 28
                    o2_strcpy(name, (char *) asl->text + 5, 29); // includes EOS
                    O2_DBz(printf("%s got a TXT field name=%s\n",
                                  o2_debug_prefix, name));
                }
                if (strncmp((char *) asl->text, "vers=", 5) == 0) {
                    O2_DBz(printf("%s got a TXT field: ", o2_debug_prefix);
                           for (int i = 0; i < asl->size; i++) {
                                printf("%c", asl->text[i]); }
                           printf("\n"));
                    version = o2_parse_version((char *) asl->text + 5,
                                               asl->size - 5);
                }
            }
            if (name[0] && version &&
                is_valid_proc_name(name, port, internal_ip, &udp_port)) {
                o2_discovered_a_remote_process_name(name, version, internal_ip,
                                                    port, udp_port, O2_DY_INFO);
            }
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
            o2_zcdisc_finish();
            return;
        case AVAHI_BROWSER_NEW:
            O2_DBz(printf("%s (Avahi Browser) NEW: service '%s' of type '%s' "
                    "in domain '%s'\n", o2_debug_prefix, name, type, domain));
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
            O2_DBz(printf("%s (Avahi Browser) REMOVE: service '%s' of type '%s'"
                    " in domain '%s'\n", o2_debug_prefix, name, type, domain));
            break;
        case AVAHI_BROWSER_ALL_FOR_NOW:
        case AVAHI_BROWSER_CACHE_EXHAUSTED:
            O2_DBz(printf("%s (Avahi Browser) %s\n", o2_debug_prefix,
                          event == AVAHI_BROWSER_CACHE_EXHAUSTED ?
                          "CACHE_EXHAUSTED" : "ALL_FOR_NOW"));
            break;
    }
}


static void entry_group_callback(AvahiEntryGroup *g,
                                 AvahiEntryGroupState state,
                                 AVAHI_GCC_UNUSED void *userdata)
{
    //TODO remove: zc_group = g;
    /* Called whenever the entry group state changes */
    switch (state) {
        case AVAHI_ENTRY_GROUP_ESTABLISHED :
            /* The entry group has been established successfully */
            O2_DBz(printf("%s (Avahi) Service '%s' successfully established.\n",
                          o2_debug_prefix, zc_name));
            break;
        case AVAHI_ENTRY_GROUP_COLLISION : {
            char *n;
            /* A service name collision with a remote service
             * happened. Let's pick a new name */
            n = avahi_alternative_service_name(zc_name);
            avahi_free(zc_name);
            zc_name = n;
            O2_DBz(printf("%s (Avahi) Service name collision, renaming "
                          "service to '%s'\n", o2_debug_prefix, zc_name));
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
            o2_zcdisc_finish();
            break;
        case AVAHI_ENTRY_GROUP_UNCOMMITED:
        case AVAHI_ENTRY_GROUP_REGISTERING:
            ;
    }
}


O2err zc_commit_group(AvahiClient *c, char **name,
                      AvahiEntryGroup **group, const char *type, int port,
                      const char *text1, const char *text2)
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
    }
    /* If the group is empty (either because it was just created, or
     * because it was reset previously, add our entries.  */
    if (avahi_entry_group_is_empty(*group)) {
        // Publish a service with type, name, and text. Note that text1
        // and/or text2 might be NULL. avahi_entry_group_add_service will
        // ignore anything after NULL:
        O2_DBz(printf("%s (Avahi) Adding service to group, name '%s' "
                      "type '%s'\n",
                      o2_debug_prefix, *name, type));
        if ((ret = avahi_entry_group_add_service(*group, AVAHI_IF_UNSPEC,
                     AVAHI_PROTO_UNSPEC, (AvahiPublishFlags) 0,
                     *name, type, NULL, NULL, port, text1, text2, NULL)) < 0) {
            if (ret == AVAHI_ERR_COLLISION)
                goto collision;
            fprintf(stderr, "Failed to add _o2proc._tcp service %s: %s\n",
                    *name, avahi_strerror(ret));
            goto fail;
        }

        /* Tell the server to register the service */
        if ((ret = avahi_entry_group_commit(*group)) < 0) {
            fprintf(stderr, "Failed to commit entry group: %s\n",
                    avahi_strerror(ret));
            goto fail;
        }
    } else {
        printf("Debug: avahi_entry_group_is_empty() returned false\n");
    }
    return O2_SUCCESS;
collision:
    /* A service name collision with a local service happened. Let's
     * pick a new name */
    n = avahi_alternative_service_name(*name);
    avahi_free(*name);
    *name = n;
    O2_DBz(printf("%s (Avahi) Service name collision, renaming service to "
                  "'%s'\n", o2_debug_prefix, *name));
    avahi_entry_group_reset(*group);
    return zc_commit_group(c, name, group, type, port, text1, text2);
fail:
    return O2_FAIL;
}


static O2err zc_create_services(AvahiClient *c)
{
    int ret;
    assert(c);
    // and text record name=@xxxxxxxx:yyyyyyyy:zzzz
    char name[64];
    strcpy(name, "name=");
    int name_end = 5 + strlen(o2_ctx->proc->key);
    strcpy(name + 5, o2_ctx->proc->key);  // proc->key is 24 bytes
    printf("zc_create_services proc->key %s\n", o2_ctx->proc->key);
    // for discovery, we need udp port too, so append it after ':'
    name[name_end++] = ':';
    sprintf(name + name_end, "%04x", o2_ctx->proc->udp_address.get_port());
    name_end += 4;
    name[name_end] = 0;

    char vers[64];
    strcpy(vers, "vers=");
    char *vers_num = vers + 5;
    o2_version(vers_num);

    return zc_commit_group(c, &zc_name, &zc_group, "_o2proc._tcp", 
                           o2_ctx->proc->fds_info->port, name, vers);
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
            o2_zcdisc_finish();
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


#include <avahi-common/simple-watch.h>


// Start discovery with avahi-client API
// Assumes we have an ensemble name and proc name with public IP
//
O2err o2_zcdisc_initialize()
{
    int error;
    if (zc_running) {
        return O2_ALREADY_RUNNING;
    }
    O2_DBz(printf("%s o2_zcdisc_initialize\n", o2_debug_prefix));
    zc_running = true;
    // need a copy because if there is a collision, zc_name is freed
    zc_name = avahi_strdup(o2_ensemble_name);

    // create poll object
    if (!(zc_poll = avahi_simple_poll_new())) {
        fprintf(stderr, "Avahi failed to create simple poll object.\n");
        goto fail;
    }
    // create client
    zc_client = avahi_client_new(avahi_simple_poll_get(zc_poll),
                                 (AvahiClientFlags) 0,
                                 &zc_client_callback, NULL, &error);
    if (!zc_client) {
        fprintf(stderr, "Avahi failed to create client: %s\n",
                avahi_strerror(error));
        goto fail;
    }
    
    // Create the service browser
    if (!(zc_sb = avahi_service_browser_new(zc_client, AVAHI_IF_UNSPEC,
                      AVAHI_PROTO_UNSPEC, "_o2proc._tcp", NULL,
                      (AvahiLookupFlags) 0, zc_browse_callback, zc_client))) {
        fprintf(stderr, "Avahi failed to create service browser: %s\n",
                avahi_strerror(avahi_client_errno(zc_client)));
        goto fail;
    }

    // register web server port
    if (zc_http_port) {
        o2_zc_register_record(zc_http_port);
    }
    return O2_SUCCESS;
 fail:
    o2_zcdisc_finish();
    return O2_FAIL;
}


void o2_poll_avahi()
{
    if (zc_poll && zc_running) {
        int ret = avahi_simple_poll_iterate(zc_poll, 0);
        if (ret == 1) {
            zc_running = false;
            printf("o2_poll_avahi got quit from avahi_simple_poll_iterate\n");
        } else if (ret < 0) {
            zc_running = false;
            fprintf(stderr, "Error: avahi_simple_poll_iterate returned %d\n",
                    ret);
        }
    }
}


void o2_zc_register_record(int port)
{
    zc_http_port = port;
    if (zc_running) {
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

        assert(zc_client);
        zc_commit_group(zc_client, &zc_http_name, &zc_http_group,
                        "_http._tcp", port, NULL, NULL);
    }
}



#endif

/*******************************/
/* SECTION 3: Common functions */
/*******************************/

#endif
