/* osc.h -- open sound control compatibility */

/* Roger B. Dannenberg
 * April 2020
 */

#ifndef O2_NO_OSC

#define ISA_OSC(node) ((node)->tag & (O2TAG_OSC_UDP_SERVER | \
        O2TAG_OSC_TCP_SERVER | O2TAG_OSC_UDP_CLIENT | \
        O2TAG_OSC_TCP_CLIENT | O2TAG_OSC_TCP_CONNECTION))

#ifdef O2_NO_DEBUG
#define TO_OSC_INFO(node) ((Osc_info *) (node))
#else
#define TO_OSC_INFO(node) (assert(ISA_OSC(((Osc_info *) (node)))),\
                           ((Osc_info *) (node)))
#endif

// See o2osc.c for details of osc_info creation, destruction, usage.
class Osc_info : public Proxy_info {
  public:
    // the key is used by Osc_info as the service name
    Net_address udp_address;
    int port; // the port could be either TCP port or UDP port, so we
        // keep a host-order copy here rather than use the port field
        // of udp_address.

    // zero out udp_address by allocating with CALLOC:
    Osc_info(const char *key, int port_, Fds_info *info, int tag) :
        Proxy_info(key, tag) {
            memset(&udp_address, 0, sizeof udp_address);
            port = port_; fds_info = info; }
    virtual ~Osc_info();

    // OSC services are considered synchronized with the Host because
    // they either use Host scheduling or NTP timestamps (which
    // are unlikely to be accurate enough except when sending to
    // localhost).
    bool local_is_synchronized() { return true; }
    bool schedule_before_send();

    // Implement the Net_interface:
    O2err accepted(Fds_info *conn);
    O2err connected();
    O2err deliver(o2n_message_ptr msg);

#ifndef O2_NO_DEBUG
    void show(int indent);
#endif

    O2status status(const char **process) {
        if (process) {
            *process = get_proc_name();
        }
        return o2_clock_is_synchronized ? O2_TO_OSC : O2_TO_OSC_NOTIME;
    }
    O2err msg_data_to_osc_data(o2_msg_data_ptr msg, O2time min_time);

    O2err send(bool block);
};


#endif
