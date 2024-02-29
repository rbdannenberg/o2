// o2litebonjour.c -- discovery using Avahi for o2lite
//
// Roger B. Dannenberg
// July 2021

#include <stdlib.h>
#include <string.h>
#include "o2lite.h"
#include "hostip.h"

// Overview:
//     Initialize by creating a DNSServiceBrowse object.
// It calls zc_browse_callback, which gets _o2proc._tcp. data.
// For each browse_callback, a record is pushed onto pending_services,
// which we resolve one-at-a-time to pace the CPU load and avoid
// dealing with many open sockets. Resolving is initiated in
// o2ldisc_poll(), and resolve_timeout initiates a resolution every
// 1s, or immediately if resolve_callback fails to find a host, but
// o2ldisc_poll() waits for something on pending_services.
//     If a resolve is in progress, both resolve_ref and active_service
// are non-null. Two things can happen: a zc_resolve_callback() can
// deliver the text record. If it is good and we find an o2 host, then
// we free the browse_ref and free_pending_services() so there will
// be no more resolve operations. Whether or not we find an o2 host,
// we call stop_resolving() to free the current active_service and
// resolve_ref.
//     Alternatively, maybe zc_resolve_callback is not called before
// resolve_timeout and there are other pending_services. In this case,
// we stop_resolving() to cancel active_service and resolve_ref, then
// start_resolving() to initiate a new active_service and resolve_ref.
//     If there is no zc_resolve_callback before resolve_timeout, but
// there is nothing else on pending_services, we can wait forever.
//     For greater robustness, e.g. Bonjour restarts, if we do not
// find an o2 host in 20s, we shut down browsing and resolving and
// reinitialize discovery.
//    Note that o2lite does not have a way to shut down and disconnect,
// so there is no other cleanup (currently).


#ifndef O2_NO_ZEROCONF
#ifndef __linux__
// !O2_NO_ZEROCONF and !defined(__linux__) -> use Bonjour

#ifndef O2_NO_O2DISCOVERY
// O2 ensembles should adopt one of two discovery methods.
#error O2lite supporte either ZeroConf or built-in discovery, but not both.
#error One of O2_NO_ZEROCONF or O2_NO_O2DISCOVERY must be defined
#endif  // end of O2DISCOVERY

#include <dns_sd.h>

#define BROWSE_TIMEOUT 20  // restart ServiceBrowse if no activity

static DNSServiceRef browse_ref = NULL;
static SOCKET browse_sock = INVALID_SOCKET;
static DNSServiceRef resolve_ref = NULL;
static SOCKET resolve_sock = INVALID_SOCKET;
static o2l_time resolve_timeout = 0;
static o2l_time browse_timeout = BROWSE_TIMEOUT;

static void zc_handle_event(SOCKET *sock, DNSServiceRef *sd_ref,
                            const char *msg)
{
    DNSServiceErrorType err = DNSServiceProcessResult(*sd_ref);
    if (err) {
        printf("Error %d from DNSServiceProcessResult for %s\n", err, msg);
        DNSServiceRefDeallocate(*sd_ref);
        *sd_ref = NULL;
        *sock = INVALID_SOCKET;
    }
}


typedef struct pending_service_struct {
    char *name;
    struct pending_service_struct *next;
} pending_service_type;

static pending_service_type *pending_services = NULL;
static pending_service_type *active_service = NULL;
#define LIST_PUSH(list, node) (node)->next = (list); (list) = (node);
#define LIST_POP(list, node) (node) = (list); (list) = (list)->next;

static void stop_resolving()
{
    // clean up previous resolve attempt:
    if (resolve_ref) {
        DNSServiceRefDeallocate(resolve_ref);
        resolve_sock = INVALID_SOCKET;
        resolve_ref = NULL;
    }
    if (active_service) {
        O2_FREE(active_service->name);
        O2_FREE(active_service);
        active_service = NULL;
    }
}


static void free_pending_services()
{
    while (pending_services) {
        pending_service_type *service;
        LIST_POP(pending_services, service);
        O2_FREE(service->name);
        O2_FREE(service);
    }
}    


static void DNSSD_API zc_resolve_callback(DNSServiceRef sd_ref,
        DNSServiceFlags flags, uint32_t interface_index,
        DNSServiceErrorType err, const char *fullname,
        const char *hosttarget, uint16_t tcp_port, uint16_t txt_len,
        const unsigned char *txt_record, void *context)
{
    char name[32];
    char internal_ip[O2N_IP_LEN];
    int udp_port;
    tcp_port = ntohs(tcp_port);
    uint8_t proc_name_len;
    const char *proc_name;
    if (tcp_sock != INVALID_SOCKET) {  // already connected to a host
        goto no_discovery;
    }
    proc_name = (const char *) TXTRecordGetValuePtr(txt_len, txt_record,
                                                    "name", &proc_name_len);
    // names are fixed length -- reject if invalid
    if (!proc_name || proc_name_len != 28) {
        goto no_discovery;
    }
    memcpy(name, proc_name, 28);
    name[28] = 0;
    O2LDB printf("o2lite: got a TXT field: name=%s\n", name);
    if (!o2l_is_valid_proc_name(name, tcp_port, internal_ip, &udp_port)) {
        goto no_discovery;
    }
    {   // check for compatible version number
        int version;
        uint8_t vers_num_len;
        const char *vers_num = (const char *) TXTRecordGetValuePtr(txt_len,
                                         txt_record, "vers", &vers_num_len);
        O2LDB if (vers_num) {
                  printf("o2lite: got a TXT field: vers=%.*s\n", 
                         vers_num_len, vers_num); }

        if (!vers_num ||
            (version = o2l_parse_version(vers_num, vers_num_len) == 0)) {
            goto no_discovery;
        }

        char iip_dot[16];
        o2_hex_to_dot(internal_ip, iip_dot);
        o2l_address_init(&udp_server_sa, iip_dot, udp_port, false);
        o2l_network_connect(iip_dot, tcp_port);
        if (tcp_sock) {  // we are connected; stop browsing ZeroConf
            if (browse_ref) {
                DNSServiceRefDeallocate(browse_ref);
            }
            browse_ref = NULL;
            browse_sock = INVALID_SOCKET;
            free_pending_services();
            // fall through to no_discovery to free the active_service and
            // resolve_ref that generated this callback...
        }
    }
  no_discovery:
    stop_resolving();
    resolve_timeout = o2l_local_now;  // so start_resolving() will be called
}


static void start_resolving()
{
    DNSServiceErrorType err;
    stop_resolving();
    LIST_POP(pending_services, active_service);
    
    err = DNSServiceResolve(&resolve_ref, 0, kDNSServiceInterfaceIndexAny,
                       active_service->name, "_o2proc._tcp.", "local",
                       zc_resolve_callback, (void *) active_service->name);
    browse_timeout = o2l_local_now + BROWSE_TIMEOUT;
    if (err) {
        fprintf(stderr, "DNSServiceResolve returned %d\n", err);
        DNSServiceRefDeallocate(resolve_ref);
        resolve_ref = NULL;
    } else {
        resolve_sock = DNSServiceRefSockFD(resolve_ref);
        resolve_timeout = o2l_local_now + 1; //  try for 1s
    }
}

static void DNSSD_API zc_browse_callback(DNSServiceRef sd_ref, 
        DNSServiceFlags flags, uint32_t interfaceIndex,
        DNSServiceErrorType err, const char *name, const char *regtype,
        const char *domain, void *context)
{
    // match if ensemble name is a prefix of name, e.g. "ensname (2)"
    if ((flags & kDNSServiceFlagsAdd) &&
        (strncmp(o2l_ensemble, name, strlen(o2l_ensemble)) == 0)) {
        pending_service_type *ps = O2_MALLOCT(pending_service_type);
        ps->name = O2_MALLOCNT(strlen(name) + 1, char);
        strcpy(ps->name, name);
        LIST_PUSH(pending_services, ps);
    }
}


/***************************************************************/
/************* o2ldisc API implementation **********************/
/***************************************************************/

int o2ldisc_init(const char *ensemble)
{
    o2l_ensemble = ensemble;
    // set up ZeroConf discovery -- our goal is to find any O2 host
    // in the ensemble, so service type is "_o2proc._tcp". Then, we
    // have to resolve a service to get the proc name, IP, and ports.
    // We'll start DNSServiceBrowse and make a list of incoming services.
    // We'll start DNSServiceResolve one service at a time until we find
    // a host. Allow 1s for each service to return a resolution. Host
    // connection will just time out on its own. Unlike the more elaborate
    // scheme in the O2 implementation, if a service times out, we just
    // go on to the next one without any retries later.
    DNSServiceErrorType err = DNSServiceBrowse(&browse_ref, 0,
                 kDNSServiceInterfaceIndexAny,
                 "_o2proc._tcp.", NULL, zc_browse_callback, NULL);
    if (!err) {
        browse_sock = DNSServiceRefSockFD(browse_ref);
        return O2L_SUCCESS;
    }
    O2LDB printf("o2lite: DNSServiceBrowse returned %d\n", err);
    DNSServiceRefDeallocate(browse_ref);
    browse_ref = NULL;
    browse_sock = INVALID_SOCKET;
    return O2L_FAIL;
}


void o2ldisc_poll()
{
    if (tcp_sock == INVALID_SOCKET) {
        if (pending_services && o2l_local_now > resolve_timeout) {
            start_resolving();
        // in principle, if we just leave the browser open, we'll see
        // anything new that appears. But we have nothing else to do.
        // And a full restart seems more robust when all else fails.
        // So if there's nothing to resolve, and no activity for 20s,
        // restart the ServiceBrowse operation.
        } else if (!pending_services && o2l_local_now > browse_timeout) {
            O2LDB printf("No activity, restarting ServiceBrowse\n");
            free_pending_services();
            stop_resolving();
            if (browse_ref) {
                DNSServiceRefDeallocate(browse_ref);
                browse_ref = NULL;
                browse_sock = INVALID_SOCKET;
            }
            // try every 20s:
            browse_timeout = o2l_local_now + BROWSE_TIMEOUT;
            o2ldisc_init(o2l_ensemble);
        }
    }
    o2l_add_socket(browse_sock);
    o2l_add_socket(resolve_sock);
}


void o2ldisc_events(fd_set *read_set_ptr)
{
    if (browse_sock != INVALID_SOCKET) {
        if (FD_ISSET(browse_sock, read_set_ptr)) {
            zc_handle_event(&browse_sock, &browse_ref, "ServiceBrowse");
        }
    }
    if (resolve_sock != INVALID_SOCKET) {
        if (FD_ISSET(resolve_sock, read_set_ptr)) {
            zc_handle_event(&resolve_sock, &resolve_ref, "ServiceResolve");
        }
    }
}

#endif // __linux__
#endif // zeroconf (!defined(O2_NO_ZEROCONF))
