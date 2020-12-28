/* services.h -- mapping from names to lists of services */

/* Roger B. Dannenberg
 * April 2020
 */

// see services.c for discussion on creating/deleting services

class Service_provider : O2obj {
  public:
    O2node *service;
    // if service is a Proc_info or MQTT_info ptr, here are properties
    char *properties;   
};

class Service_tap : O2obj {
  public:
    O2string tapper;  // redirect copy of message to this service
    Proxy_info *proc;  // send the message copy to this process
    O2tap_send_mode send_mode;
};


class Services_entry : public O2node {
  public:
    Vec<Service_provider> services;
            // dynamic array of type Service_provider
            // links to offers of this service. First in list
            // is the service to send to. Here "offers" means a hash_node
            // (local service), handler_entry (local service with just one
            // handler for all messages), proc_info (for remote
            // service), osc_info (for service delegated to OSC), or
            // bridge_inst (for a bridge over alternate non-IP transport).
            // Valid tags for services in this array are:
            //    O2TAG_HASH, O2TAG_HANDLER, O2TAG_BRIDGE,
            //    O2TAG_OSC_TCP_CLIENT, O2TAG_OSC_UDP_CLIENT,
            //    O2TAG_TCP_NOMSGYET, O2TAG_PROC, O2TAG__EMPTY
    Vec<Service_tap> taps; // the "taps" on this service -- these are of type
            // service_tap and indicate services that should get copies
            // of messages sent to the service named by key.
    Services_entry(const char *service_name) :
            O2node(service_name, O2TAG_SERVICES), services(1), taps(0) { }
    virtual ~Services_entry();
#ifndef O2_NO_DEBUG
    void show(int indent);
#endif
    int proc_service_index(Proxy_info *proc);
    Service_provider *proc_service_find(Proxy_info *proc) {
        int index = proc_service_index(proc);
        return index >= 0 ? &services[index] : NULL;
    }
    bool add_service(O2string our_ip_port, O2node *service, char *properties);
    O2err service_remove(const char *srv_name, int index, Proxy_info *proc);
    O2err insert_tap(O2string tapper, Proxy_info *proxy,
                     O2tap_send_mode send_mode);
    O2err tap_remove(Proxy_info *proc, const char *tapper);
    void pick_service_provider();
    void remove_if_empty();
    
    static Services_entry **find(const char *service_name);

    /**
     *  \brief Use initial part of an O2 address to find an o2_service using
     *  a hash table lookup.
     *
     *  @param name points to the service name (do not include the
     *              initial '!' or '/' from the O2 address).
     *
     *  @return The pointer to the service, which is a Hash_node, Handler_node,
     *          or Proxy_info (Proc_info, OSC_info or Bridge_info)
     */
    static O2node *service_find(const char *service_name,
                                Services_entry **services);
    static Services_entry **find_from_msg(O2message_ptr msg);
    static Services_entry *must_get_services(O2string service_name);
    static O2err service_new(O2string padded_name);
    static O2err service_provider_new(O2string name, const char *properties,
                                         O2node *service, Proxy_info *proc);
    static O2err service_provider_replace(const char *service_name,
                               O2node **node_ptr, O2node *new_service);
    static O2err proc_service_remove(const char *service_name,
                  Proxy_info *proc, Services_entry *ss, int index);
    static void list_services(Vec<Services_entry *> &list);
    static O2err remove_services_by(Proxy_info *proc);
    static O2err remove_taps_by(Proxy_info *proc);
};

#ifdef O2_NO_DEBUG
#define TO_SERVICES_ENTRY(node) ((Services_entry *) node)
#else
#define TO_SERVICES_ENTRY(node) (assert(ISA_SERVICES(node)), \
                                 (Services_entry *) node)
#endif

O2err o2_service_free(const char *service_name);
