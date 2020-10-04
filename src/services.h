/* services.h -- mapping from names to lists of services */

/* Roger B. Dannenberg
 * April 2020
 */

// see services.c for discussion on creating/deleting services

typedef struct services_entry { // "subclass" of o2_node
    int tag; // must be NODE_SERVICES
    o2string key; // key (service name) is "owned" by this struct
    o2_node_ptr next;
    dyn_array services;
            // dynamic array of type service_provider
            // links to offers of this service. First in list
            // is the service to send to. Here "offers" means a hash_node
            // (local service), handler_entry (local service with just one
            // handler for all messages), proc_info (for remote
            // service), osc_info (for service delegated to OSC), or
            // bridge_inst (for a bridge over alternate non-IP transport).
            // Valid tags for services in this array are:
            //    NODE_HASH, NODE_HANDLER, BRIDGE_NOCLOCK, BRIDGE_SYNCED,
            //    OSC_TCP_CLIENT, OSC_UDP_CLIENT, INFO_TCP_NOMSGYET,
            //    PROC_NOCLOCK, PROC_SYNCED, NODE_EMPTY
    dyn_array taps; // the "taps" on this service -- these are of type
            // service_tap and indicate services that should get copies
            // of messages sent to the service named by key.
} services_entry, *services_entry_ptr;

#ifdef O2_NO_DEBUG
#define TO_SERVICES_ENTRY(node) ((services_entry_ptr) node)
#else
#define TO_SERVICES_ENTRY(node) (assert(node->tag == NODE_SERVICES), \
                                 (services_entry_ptr) node)
#endif

#define GET_SERVICE(list, i) (DA_GET(list, service_provider, i).service)
#define GET_SERVICE_PROVIDER(list, i) DA_GET_ADDR(list, service_provider, i)
#define GET_TAP_PTR(list, i) DA_GET_ADDR(list, service_tap, i)

typedef struct service_provider {
    o2_node_ptr service;
    char *properties;    // if service is a proc_info_ptr, here are properties
} service_provider, *service_provider_ptr;

typedef struct service_tap {
    o2string tapper;     // redirect copy of message to this service
    proc_info_ptr proc;  // send the message copy to this process
} service_tap, *service_tap_ptr;


o2_err_t o2_service_new2(o2string padded_name);


o2_err_t o2_service_provider_new(o2string key, const char *properties,
                                 o2_node_ptr service, proc_info_ptr proc);


/**
 *  \brief Use initial part of an O2 address to find an o2_service using
 *  a hash table lookup.
 *
 *  @param name points to the service name (do not include the
 *              initial '!' or '/' from the O2 address).
 *
 *  @return The pointer to the service, tag may be INFO_TCP_SOCKET 
 *          (remote process), NODE_HASH (local service), 
 *          NODE_HANDLER (local service with single handler),
 *          or NODE_OSC_REMOTE_SERVICE (redirect to OSC server),
 *          or NULL if name is not found.
 */
o2_node_ptr o2_service_find(const char *name, services_entry_ptr *services);

int o2_services_insert_tap(services_entry_ptr ss, o2string tapper,
                           proc_info_ptr proc);

int o2_service_provider_replace(const char *service_name,
          o2_node_ptr *node_ptr, o2_node_ptr new_service);

int o2_service_remove(const char *service_name, proc_info_ptr proc,
                      services_entry_ptr ss, int index);

int o2_remove_services_by(proc_info_ptr proc);

int o2_tap_remove_from(services_entry_ptr ss, proc_info_ptr proc,
                       const char *tapper);

int o2_remove_taps_by(proc_info_ptr proc);

services_entry_ptr o2_must_get_services(o2string service_name);

int o2_service_free(const char *service_name);

void o2_services_entry_finish(services_entry_ptr entry);

bool o2_add_to_service_list(services_entry_ptr ss, o2string our_ip_port,
                            o2_node_ptr service, char *properties);

services_entry_ptr *o2_services_find(const char *service_name);

services_entry_ptr *o2_services_from_msg(o2_message_ptr msg);

service_provider_ptr o2_proc_service_find(proc_info_ptr proc,
                                  services_entry_ptr services);

void o2_list_services(dyn_array_ptr list);

#ifndef O2_NO_DEBUG
void o2_services_entry_show(services_entry_ptr node, int indent);
#endif
