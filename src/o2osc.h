/* osc.h -- open sound control compatibility */

/* Roger B. Dannenberg
 * April 2020
 */

#ifndef O2_NO_OSC

#define OSC_UDP_SERVER     30 // local proc provides an OSC-over-UDP service
#define OSC_TCP_SERVER     31 // local proc provides an OSC-over-TCP service
#define OSC_UDP_CLIENT     32 // local forwards to OSC-over-UDP 
#define OSC_TCP_CLIENT     33 // local forwards to OSC-over-TCP socket
#define OSC_TCP_CONNECTION 34 // accepted connection for OSC-over-TCP service

// is this an osc_info structure?
#define ISA_OSC(o) ((((o)->tag) >= OSC_UDP_SERVER) && \
                    (((o)->tag) <= OSC_TCP_CONNECTION))

#ifdef O2_NO_DEBUG
#define TO_OSC_INFO(node) ((osc_info_ptr) (node))
#else
#define TO_OSC_INFO(node) (assert(ISA_OSC(((osc_info_ptr) (node)))),\
                           ((osc_info_ptr) (node)))
#endif

// See o2osc.c for details of osc_info creation, destruction, usage.
typedef struct osc_info {
    int tag;
    o2n_info_ptr net_info; // this will be NULL for OSC over UDP
    o2string service_name; // this string is owned by this struct
    o2n_address udp_address;
    int port; // the port could be either TCP port or UDP port, so we
        // keep a host-order copy here rather than use the port field
        // of udp_address.
} osc_info, *osc_info_ptr;


o2_err_t o2_deliver_osc(osc_info_ptr info);

o2_err_t o2_send_osc(osc_info_ptr service, services_entry_ptr services);

void o2_osc_info_free(osc_info_ptr osc);

void o2_osc_info_show(osc_info_ptr oi);

o2_err_t o2_osc_accepted(osc_info_ptr server, o2n_info_ptr conn);

o2_err_t o2_osc_connected(osc_info_ptr info);

#endif
