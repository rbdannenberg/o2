/* osc.h -- open sound control compatibility */

/* Roger B. Dannenberg
 * April 2020
 */

#define OSC_UDP_SERVER     30 // local proc provides an OSC-over-UDP service
#define OSC_TCP_SERVER     31 // local proc provides an OSC-over-TCP service
#define OSC_UDP_CLIENT     32 // local forwards to OSC-over-UDP 
#define OSC_TCP_CLIENT     33 // local forwards to OSC-over-TCP socket
// is this an osc_info structure?
#define ISA_OSC(o) ((((o)->tag) >= OSC_UDP_SERVER) && \
                    (((o)->tag) <= OSC_TCP_CLIENT))

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
    // in the case of TCP, this name is created for the OSC_TCP_SERVER
    // and is shared by every accepted  OSC_TCP_SOCKET. It is also
    // shared with the services_entry, which stores name as the key.
    o2string service_name;
    o2n_address udp_address;
    int port; // the port in host byte order
} osc_info, *osc_info_ptr;


int o2_deliver_osc(osc_info_ptr info, o2_message_ptr msg);

int o2_send_osc(osc_info_ptr service, o2_message_ptr msg, services_entry_ptr services);

void o2_osc_info_free(osc_info_ptr osc);

void o2_osc_info_show(osc_info_ptr oi);

int o2_osc_accepted(osc_info_ptr server, o2n_info_ptr conn);

int o2_osc_connected(osc_info_ptr info);
