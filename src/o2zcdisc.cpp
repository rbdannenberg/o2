// o2zcdisc -- zeroconf/bonjour discovery for O2
//
// Roger B. Dannenberg
// Feb 2021

#ifndef O2_NO_ZEROCONF

#include "o2internal.h"
#include "discovery.h"
#include "hostip.h"
#include "pathtree.h"
#include "o2zcdisc.h"



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

// Set to a new Zc_info instance to listen to a socket from DNSServiceResolve.
// Set to NULL when Zc_info socket is marked for closure. (It may take time
// to actually close the socket by later calling delete on Zc_info.)
static Zc_info *resolve_info = NULL;
static int watchdog_seq = 0;  // sequence number to cancel watchdog callback

typedef struct {
    const char *name;
    bool asap;  // process as soon as possible (first time)
} resolve_type;

Vec<resolve_type> resolve_pending;

static void resolve();

Zc_info::Zc_info(DNSServiceRef sr) : Proxy_info(NULL, O2TAG_ZC) {
    sd_ref = sr;
    // install socket
    info = new Fds_info(DNSServiceRefSockFD(sd_ref),
                        NET_TCP_CLIENT, 0, this);
    info->read_type = READ_CUSTOM;  // we handle everything
}


Zc_info::~Zc_info() {
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

O2err Zc_info::deliver(O2netmsg_ptr msg) {
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
                    context);
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
            resolve_info = new Zc_info(sd_ref);
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



static Zc_info *zc_register(const char *type_domain,  const char *host,
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
    return new Zc_info(sd_ref);
}


Zc_info *o2_zc_register_record(int port)
{
    char ipdot[O2N_IP_LEN];
    o2_hex_to_dot(o2n_internal_ip, ipdot);
    in_addr_t addr = inet_addr(ipdot);

    char fullname[64];
    o2_strcpy(fullname, o2_ensemble_name, 64);
    int len = strlen(fullname);
    if (len > 63 - 6) {
        return NULL;
    }
    strcpy(fullname + len, ".local");

    DNSServiceRef sd_ref;
    DNSServiceErrorType err;
    err = DNSServiceCreateConnection(&sd_ref);
    if (err) {
        return NULL;
    }

    DNSRecordRef record_ref;
    err =  DNSServiceRegisterRecord(sd_ref, &record_ref,
            kDNSServiceFlagsUnique, kDNSServiceFlagsIncludeP2P, fullname,
            kDNSServiceType_A, kDNSServiceClass_IN, sizeof(addr), &addr,
            240, rr_callback, NULL);
    if (err != kDNSServiceErr_NoError) {
        fprintf(stderr, "Error: DNSServiceRegisterRecord failed.\n");
        DNSServiceRefDeallocate(sd_ref);
    }

    Zc_info *zcr_info = zc_register("_http._tcp.", fullname, port, 0, NULL);
    if (zcr_info) {
        return new Zc_info(sd_ref);
    } else {
        DNSServiceRefDeallocate(sd_ref);
        return NULL;
    }
}


static void zc_browse_callback(DNSServiceRef sd_ref, DNSServiceFlags flags,
                uint32_t interfaceIndex, DNSServiceErrorType err,
                const char *name, const char *regtype,
                const char *domain, void *context)
{
    fprintf(stderr, "zc_browse_callback err %d flags %d name %s as %s domain %s\n",
           err, flags, name, regtype, domain);
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
    Zc_info *zcreg = zc_register("_o2proc._tcp.", NULL,
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
    new Zc_info(sd_ref);

    o2_method_new_internal("/_o2/dydog", "i", &resolve_watchdog,
                           NULL, false, true);

    return O2_SUCCESS;
}

#endif
