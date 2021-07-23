// o2liteavahi.c -- discovery using Avahi for o2lite
//
// Roger B. Dannenberg
// July 2021

/*********** Linux Avahi Implementation *************/
// note on naming: Avahi uses "avahi" prefix, so all of our
// avahi-related names in O2 use "zc".

#include <string.h>
#include "o2lite.h"
#include "hostip.h"

#ifndef O2_NO_ZEROCONF
#ifdef __linux__
// !O2_NO_ZEROCONF and __linux__ -> use Avahi

#ifndef O2_NO_O2DISCOVERY
// O2 ensembles should adopt one of two discovery methods.
#error O2lite supporte either ZeroConf or built-in discovery, but not both.
#error One of O2_NO_ZEROCONF or O2_NO_O2DISCOVERY must be defined
#endif  // end of O2DISCOVERY

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#include <avahi-common/simple-watch.h>

#define BROWSE_TIMEOUT 20  // restart Avahi browser if no activity (in seconds)

static long start_time;
void o2_poll_avahi();

// These globals keep everything we are allocating -- it's not stated
// in Avahi docs what happens to objects passed into it, so we'll
// free them ourselves if we are not explicitly told to free them by
// Avahi. If Avahi takes ownership and frees either _name or _text
// objects, we may end up freeing dangling pointers. Hopefully, this
// will be detected in testing and not released. Yikes... why isn't
// Avahi documented sufficiently for reliable use?

static char *zc_name = NULL;
static bool zc_running = false;
static bool zc_inside_poll = false;
static bool zc_shutdown_request = false;
static o2l_time browse_timeout = BROWSE_TIMEOUT;

static AvahiServiceBrowser *zc_sb = NULL;
static AvahiClient *zc_client = NULL;  // global access to avahi-client API

// AvahiPoll structure so Avahi can watch sockets:
static AvahiSimplePoll *zc_poll = NULL;


// helper to deal with multiple free functions:
#define FREE_WITH(variable, free_function) \
    if (variable) { \
        free_function(variable); \
        variable = NULL; \
    }


static void zc_shutdown()
{
    if (zc_inside_poll) {  // call back after poll returns
        zc_shutdown_request = true;
        return;
    }
    O2LDB printf("o2lite: zc_shutdown\n");
    FREE_WITH(zc_sb, avahi_service_browser_free);
    FREE_WITH(zc_client, avahi_client_free);
    FREE_WITH(zc_poll, avahi_simple_poll_free);
    FREE_WITH(zc_name, avahi_free);
    zc_running = false;
}


void o2ldisc_poll()
{
    // start resolving if timeout
    if (tcp_sock == INVALID_SOCKET) {
        if (o2l_local_now > browse_timeout) {  // no tcp_sock after 20s
            O2LDB printf("o2lite: no activity, restarting Avahi client\n");
            zc_shutdown();
            browse_timeout = o2l_local_now + BROWSE_TIMEOUT;  // try every 20s
            o2ldisc_init(o2l_ensemble);
        }
    }

    if (zc_poll && zc_running) {
        assert(!zc_inside_poll);
        zc_inside_poll = true;
        int ret = avahi_simple_poll_iterate(zc_poll, 0);
        zc_inside_poll = false;
        if (ret == 1) {
            zc_running = false;
            printf("o2_poll_avahi got quit from avahi_simple_poll_iterate\n");
        } else if (ret < 0) {
            zc_running = false;
            fprintf(stderr, "Error: avahi_simple_poll_iterate returned %d\n",
                    ret);
        }
    }

    if (zc_shutdown_request) {
        zc_shutdown_request = false;
        zc_shutdown();
    }



}


void zc_cleanup()
{
    zc_shutdown();
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
    int udp_send_port;
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
            O2LDB printf("o2lite: Avahi resolve service '%s' of type '%s' in "
                    "domain '%s':\n", name, type, domain);
            avahi_address_snprint(a, sizeof(a), address);
            char name[32];
            char internal_ip[O2N_IP_LEN];
            int udp_port = 0;
            int version = 0;
            name[0] = 0;
            for (AvahiStringList *asl = txt; asl; asl = asl->next) {
                if (strncmp((char *) asl->text, "name=", 5) == 0 &&
                    asl->size == 33) {  // found "name="; proc name len is 28
                    strncpy(name, (char *) asl->text + 5, 28);
                    name[28] = 0;  // make sure name is zero-terminated
                    // O2LDB printf("o2lite: got a TXT field name=%s\n", name);
                } else if (strncmp((char *) asl->text, "vers=", 5) == 0) {
                    /* O2LDB { printf("o2lite: got a TXT field: ");
                            for (int i = 0; i < asl->size; i++) {
                                printf("%c", asl->text[i]); }
                            printf("\n"); }
                    */
                    version = o2l_parse_version((char *) asl->text + 5,
                                                asl->size - 5);
                }
            }
            if (name[0] && version &&
                o2l_is_valid_proc_name(name, port, internal_ip,
                                       &udp_send_port)) {
                char iip_dot[16];
                o2_hex_to_dot(internal_ip, iip_dot);
                o2l_address_init(&udp_server_sa, iip_dot, udp_send_port, false);
                O2LDB printf("o2lite: found a host: %s\n", name);
                o2l_network_connect(iip_dot, port);
            }
        }
    }
    avahi_service_resolver_free(r);
    if (tcp_sock != INVALID_SOCKET) {
        zc_shutdown();
    }
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
            O2LDB printf("o2lite: (Avahi Browser) NEW: service '%s' of type "
                         "'%s' in domain '%s'\n", name, type, domain);
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
            O2LDB printf("o2lite: (Avahi Browser) REMOVE: service '%s' of "
                         "type '%s' in domain '%s'\n", name, type, domain);
            break;
        case AVAHI_BROWSER_ALL_FOR_NOW:
        case AVAHI_BROWSER_CACHE_EXHAUSTED:
            O2LDB printf("o2lite: (Avahi Browser) %s\n",
                          event == AVAHI_BROWSER_CACHE_EXHAUSTED ?
                          "CACHE_EXHAUSTED" : "ALL_FOR_NOW");
            break;
    }
}


static void zc_client_callback(AvahiClient *c, AvahiClientState state,
                               AVAHI_GCC_UNUSED void * userdata)
{
    assert(c);
    /* Called whenever the client or server state changes */
    if (state == AVAHI_CLIENT_FAILURE) {
        fprintf(stderr, "Avahi client failure: %s\n",
                avahi_strerror(avahi_client_errno(c)));
        zc_shutdown();
    }
}

/***************************************************************/
/************* o2ldisc API implementation **********************/
/***************************************************************/

int o2ldisc_init(const char *ensemble)
{
    o2l_ensemble = ensemble;
    int error;
    if (zc_running) {
        return O2L_ALREADY_RUNNING;
    }
    O2LDB printf("o2lite: o2l_avahi_initialize\n");
    zc_running = true;
    // need a copy because if there is a collision, zc_name is freed
    zc_name = avahi_strdup(o2l_ensemble);

    // create poll object
    if (!(zc_poll = avahi_simple_poll_new())) {
        O2LDB printf("Avahi failed to create simple poll object.\n");
        goto fail;
    }
    // create client
    zc_client = avahi_client_new(avahi_simple_poll_get(zc_poll),
                                 (AvahiClientFlags) 0,
                                 &zc_client_callback, NULL, &error);
    if (!zc_client) {
        O2LDB printf("Avahi failed to create client: %s\n",
                     avahi_strerror(error));
        goto fail;
    }
    
    // Create the service browser
    if (!(zc_sb = avahi_service_browser_new(zc_client, AVAHI_IF_UNSPEC,
                      AVAHI_PROTO_UNSPEC, "_o2proc._tcp", NULL,
                      (AvahiLookupFlags) 0, zc_browse_callback, zc_client))) {
        O2LDB printf("Avahi failed to create service browser: %s\n",
                     avahi_strerror(avahi_client_errno(zc_client)));
        goto fail;
    }
    // ZeroConf only requires one (any) udp receive port. The port number
    // is initially zero. After we bind the socket, we never close it, so
    // if o2ldisc_init was called previously, do not try to bind again:
    if (udp_recv_port == 0) {
        if (o2l_bind_recv_socket(udp_recv_sock, &udp_recv_port) != 0) {
            goto fail;
        }
    }
    return O2L_SUCCESS;
 fail:
    zc_shutdown();
    return O2L_FAIL;
}


void o2ldisc_events(fd_set *read_set_ptr)
{
    return;
}

#endif  // end __linux__
#endif  // end ZEROCONF

