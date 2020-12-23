/* services.c -- mapping from names to lists of services */

/* Roger B. Dannenberg
 * April 2020
 */

#include "o2internal.h"
#include "services.h"
#include "message.h"
#include "msgsend.h"
#include "o2osc.h"
#include "ctype.h"

/*
Creating and deleting services. 
-------------------------------
Services are complicated because of the highly-linked structure
that connects to proc_info structures and osc_info structures
which have back pointers to services and sometimes share service
name strings that also appear as keys in services_entry nodes.

Services entry is created when:
- o2_tap_new creates services for tappee
    - calls o2_must_get_services()
- discovery finds services offered by remote proc, calls
    - o2_service_provider_new(), calls
        - o2_must_get_services()
- o2_service_new creates a service, calls
    - Services_entry::service_new(), calls
        - o2_service_provider_new()
- o2_osc_delegate creates a service to forward messages over OSC, calls
    - o2_service_provider_new()
- o2_mqtt_disc_handler() adds an MQTT process
- note: no service for local PORT:IP - send to _o2 instead

Services entry is destroyed when:
- remove_empty_services_entry() is called from
  - o2_service_remove() which is called from one of:
    - o2_services_handler() (discovery message with service or tap deletion)
    - o2_service_free()
    - o2_remove_services_by() called when process ends
  - o2_tap_remove_from(), which is called from one of:
    - o2_tap_remove() called by o2_untap()
    - o2_remove_taps_by() called when process ends
*/


// internal implementation of o2_service_new, assumes valid
// service name with zero padding. The assumption is there will
// be methods with paths on this service, so the service is
// created as a Hash_node.
O2err Services_entry::service_new(o2string padded_name)
{
    // find services_node if any
    Hash_node *node = new Hash_node(NULL);
    // this will send /_o2/si message to local process:
    O2err rslt = service_provider_new(padded_name, NULL,
                                      node, o2_ctx->proc);
    if (rslt != O2_SUCCESS) {
        delete node;
        return rslt;
    }
    // Note that when the local @public:internal:port service is created,
    // there are no remote connections yet, so o2_notify_others()
    // will not send any messages.
    o2_notify_others(padded_name, true, NULL, NULL);

    return O2_SUCCESS;
}


/* add or update a service provider - a service is added to the list of 
 * services in  a service_entry struct.
 * 1) create the service_entry struct if none exists
 * 2) put the service onto process's list of service names
 * 3) add new service to the list
 *
 * service_name: service to add or update
 * properties: property string for the service
 * service: the service provider (e.g. remote process) or the 
 *          hash table or handler
 * proc: the process offering the service (remember that a service can
 *       be offered by many processes. The lowest IP:Port gets priority)
 *
 * CASE 1: this is a new local service
 *
 * CASE 2: this is the installation of /@public:internal:port for a newly
 *         discovered remote process. service == proc
 *
 * CASE 3: this is creating a service that delegates to OSC. service is
 *         an osc_info_ptr, process is the local process
 *
 * CASE 4: handling /@public:internal:port/sv: service is a Fds_info *equal
 *          to process. Note that /sv can indicate update to properties
 * 
 * CASE 5: this is the installation of /@public:internal:port for a newly
 *         discovered MQTT process. service == proc
 *
 * Algorithm:
 *  - create or lookup service_name
 *  - find the service_provider entry in services for proc
 *  - if we already know about proc, this is a property update: do it
 *  - otherwise, add the proc by creating a new service_provider entry
 *  - if the proc is the active service provider, send an /_o2/si message.
 */
O2err Services_entry::service_provider_new(o2string service_name,
            const char *properties, O2node *service, Proxy_info *proc)
{
    bool active = false;
    O2_DBd(printf("%s %s service_provider_new adding %s to %s\n",
                  o2_debug_prefix,
                  // highlight when proc->key is our IP:Port info:
                  (streql(service_name, "_o2") ? "****" : ""),
                  service_name, proc->key));
    Services_entry *ss = must_get_services(service_name);
    // services exists, is this service already offered by proc?
    Service_provider *spp = ss->proc_service_find(proc);

    // adjust properties to be either NULL or non-empty property string
    if (properties) {
        if (properties[0] == 0 || // convert "" or ";" to NULL (no properties)
            (properties[0] == ';' && properties[1] == 0)) {
            properties = NULL;
        } else {
            properties = o2_heapify(properties);
        }
    }

    if (spp && (ISA_REMOTE_PROC(spp->service))) {
        // now we know this is a remote service and we can set the properties
        O2_DBd(printf("%s service_provider_new service exists %s\n",
                      o2_debug_prefix, service_name));
        active = ss->services[0].service == (O2node *) proc;
        if (spp->properties) O2_FREE(spp->properties);
        spp->properties = (char *) properties;
    } else if (spp) { // it is an error to replace an existing local service
        // you must call o2_service_free() first
        assert(!properties);
        return O2_SERVICE_EXISTS;
    } else {
        // Now we know it's safe to add a service and we have a place to put it
        // Note that the proc name does not need to exist. Proc name is needed
        // to decide which service is active based on proc names. If there is
        // no proc name, o2_node_to_proc_name() will return _o2, which cannot
        // be used in place of an IP address for purposes of picking the active
        // service provider, BUT if there are any other service providers, it
        // means that discovery is running, and that can only happen after we
        // have a full pip:iip:port name.
        active = ss->add_service(proc->key, service, (char *) properties);
        O2_DBG(printf("%s ** new service %s is %p (%s) active %d\n",
                      o2_debug_prefix, ss->key, service,
                      o2_tag_to_string(service->tag), active);
               o2_ctx->path_tree.show(2));
    }
    if (active) {
        // we have an update in the active service, so report it to the local
        // process; /si msg needs: *service_name* *status* *process-name*
        O2status status = service->status(NULL);
        // If this is a new process connection, process_name is NULL
        // and we do not send !_o2/si yet. See o2n_recv() in o2_net.c
        // where protocol completes and !_o2/si is sent.
        // Also, we always send _o2 to name the local process.
        const char *proc_name = (proc == o2_ctx->proc ? "_o2" : proc->key);
        o2_send_cmd("!_o2/si", 0.0, "siss", service_name, status,
                    proc_name, properties ? properties + 1 : "");
    }
    return O2_SUCCESS;
}


/* find existing services_entry node for service_name. If none exists,
 *     return a pointer to NULL.
 * prereq: service_name does not contain '/'
 */
Services_entry **Services_entry::find(const char *service_name)
{
    // all callers are passing in (possibly) unaligned strings, so we
    // need to copy the service_name to aligned storage and pad it
    char key[NAME_BUF_LEN];
    o2_string_pad(key, service_name);
    return (Services_entry **) o2_ctx->path_tree.lookup(key);
}    


// find the service node for this message: This could be a proxy to
//    forward to or the Hash_node or Handler_entry for the local service
//
O2node *o2_msg_service(o2_msg_data_ptr msg, Services_entry **services)
{
    char *service_name = msg->address + 1;
    char *slash = strchr(service_name, '/');
    if (slash) *slash = 0;
    O2node *rslt = NULL; // return NULL if service not found
    // When a message if forwarded to a tap, it is marked with the O2_TAP_FLAG
    // and delivered to a specific tapper process. So if O2_TAP_FLAG is set,
    // we need to find the service offered by this local process even if it
    // is not the active service, so we cannot use Service_entry::find:
    if (msg->flags & O2_TAP_FLAG) {
        *services = *Services_entry::find(service_name);
        Service_provider *spp = (*services)->proc_service_find(o2_ctx->proc);
        if (spp) rslt = spp->service;
    } else {
        rslt = Services_entry::service_find(service_name, services);
    }
    if (slash) *slash = '/';
    return rslt;
}


/* prereq: service_name does not contain '/'
 */
O2node *Services_entry::service_find(const char *service_name,
                                     Services_entry **services)
{
    *services = *Services_entry::find(service_name);
    if (!*services) {
        // map local @public:internal:port string to "_o2": Note that we
        // could save a hash
        // lookup by doing this test first, but I think this is pretty rare:
        // only system messages from remote processes will use @pip:iip:port/
        if (service_name[0] == '@' && o2_ctx->proc->key &&
             streql(service_name, o2_ctx->proc->key)) {
            *services = *Services_entry::find("_o2");
        } else {
            return NULL;
        }
    }
    // service entry could have taps but no service provider yet
    if ((*services)->services.size() == 0) {
        return NULL;
    }
    return (*services)->services[0].service;
}


O2err Services_entry::insert_tap(o2string tapper, Proxy_info *proc)
{
    Service_tap *tap = taps.append_space(1);
    tap->tapper = tapper;
    tap->proc = proc;
    return O2_SUCCESS;
}


Services_entry **Services_entry::find_from_msg(O2message_ptr msg)
{
    char *service_name = msg->data.address + 1; // skip '/' or '!'
    char *slash = strchr(service_name, '/');
    if (slash) *slash = 0;
    Services_entry **s = find(service_name);
    if (slash) *slash = '/';
    return s;
}


/** find address of service in services that is offered by proc, if any. 
 *      Note that if proc is the local process (o2_ctx->proc), the
 *  result can be an OSC or BRIDGE node, since these are proxies for
 *  the local process.
 */
Service_provider *Services_entry::proc_service_find(Proxy_info *proc)
{
    if (!this) return NULL;
    for (int i = 0; i < services.size(); i++) {
        Service_provider *spp = &services[i];
        O2node *a_prvdr = spp->service;
        if (a_prvdr == proc) {
            return spp;
        } else if ((a_prvdr->tag & (O2TAG_HASH | O2TAG_HANDLER | O2TAG_EMPTY |
                                    O2TAG_BRIDGE | O2TAG_OSC_UDP_CLIENT |
                                    O2TAG_OSC_TCP_CLIENT)) &&
                   proc == o2_ctx->proc) {
            return spp; // local service already exists
        }
    }
    return NULL;
}


// in the list of services, find the service with the highest service provider
// name and move it to the top position in the list. This is called when the
// top (active) service is removed and must be replaced
void Services_entry::pick_service_provider()
{
    int top_index = 0;
    int search_start = 1;
    if (top_index >= services.size()) return;
    O2node *top_node = services[top_index].service;
    const char *top_name = top_node->get_proc_name();
    for (int i = search_start; i < services.size(); i++) {
        O2node *node = services[i].service;
        const char *name = node->get_proc_name();
        // if location 0 was not a tap, we did not update search_start,
        // so we have to skip over taps to find real services.
        if (strcmp(name, top_name) > 0) {
            // we found a service and it has a greater name, so remember
            // the top name so far and where we found it.
            top_node = node;
            top_name = name;
            top_index = i;
        }
    }
    // swap top_index and 0. It is possible there's only one service at
    // location 0 and we swap it with itself - a no-op. Or maybe service
    // 0 is the top ipport, so again swapping with itself is OK.
    Service_provider temp = services[top_index];
    services[top_index] = services[0];
    services[0] = temp;
}


/** replace the service named service_name offered by proc with new_service.
 * This happens when we change from all-service handler to per-node handlers
 * or vice versa. Also happens when we delete a service, and when we remove a
 * remote service when it disconnects.
 *
 * precondition: service_name does not have '/' && new_service != NULL
 *
 * CASE 1: called from o2_method_new(), installing a global handler for
 *         service, maybe replacing an existing one, maybe not.
 * CASE 2: called from o2_method_new(), replacing a global handler with
 *         a hash_node where we can install a Hash_node based on the
 *         next node in the address.
 * (no more cases because we moved others to o2_service_remove)
 */
O2err Services_entry::service_provider_replace(const char *service_name,
                                 O2node **node_ptr, O2node *new_service)
{
    assert(new_service);
    // clean up the old service node
    if (HANDLER_IS_LOCAL(*node_ptr)) {
        delete *node_ptr;
    } else if (ISA_LOCAL_SERVICE(*node_ptr)) {
        return O2_SERVICE_EXISTS;  // can't replace pre-existing OSC or BRIDGE
    } else {
        O2_DBG(printf("%s service_provider_replace(%s, ...) did not find "
                      "service offered by this process\n",
                      o2_debug_prefix, service_name));
        return O2_FAIL;  // unexpected tag, give up
    }
    *node_ptr = new_service; // install the new service
    return O2_SUCCESS;
}


// This code is used by o2_service_remove and o2_tap_remove_from:
// it checks if a services_entry no longer has services or taps.
// If not, it removes the services entry completely.
//
void Services_entry::remove_if_empty()
{
    // if no service providers or taps left, remove service entry
    if (services.size() == 0 && taps.size() == 0) {
        // printf("Removing %s from &o2_ctx->path_tree\n", ss->key);
        o2_ctx->path_tree.entry_remove_by_name(key);
        // printf(" Here is the result:\n");
        // o2_node_show((o2_node_ptr) &o2_ctx->path_tree, 2);
        // service name (the key in path_tree) is now freed.
    }
}


O2err Services_entry::service_remove(const char *srv_name,
                                        int index, Proxy_info *proc)
{
    Service_provider *spp;
    if (index < 0) {
        spp = proc_service_find(proc);
        if (spp) {
            // proc_service_find returned an index instead of a pointer
            delete spp->service;
        }
    } else {
        spp = &services[index];
    }
    // if we did not find what we wanted to remove, stop here
    if (!spp) {
        O2_DBG(printf("%s o2_service_remove(%s, %s, ...) did not find "
                      "service offered by this process\n",
                      o2_debug_prefix, key,
                      ISA_REMOTE_PROC(proc) ? proc->key : "local"));
        return O2_FAIL;
    }
    // ASSERT: index is now the index of the service we are deleting or
    //         replacing, spp == &services[index]
    //
    // we found the service to replace; finalized the info depending on the
    // type, so now we have a dangling pointer in the services list
    char *properties = spp->properties;
    if (properties) {
        O2_FREE(properties);
    }
    
    services.remove(index);

    o2_do_not_reenter++; // protect data structures
    // send notification message
    o2_send_cmd("!_o2/si", 0.0, "siss", srv_name, O2_FAIL,
                proc->get_proc_name(), "");

    // if we deleted active service, pick a new one
    if (index == 0) { // move top @public:internal:port provider to top spot
        pick_service_provider();
    }
    // now we probably have a new service, report it:
    if (services.size() > 0) {
        Service_provider *spp = &services[0];
        const char *process_name;
        int status = spp->service->status(&process_name);
        if (status != O2_FAIL) {
            assert(process_name[0]);
            o2_send_cmd("!_o2/si", 0.0, "siss", srv_name, status,
                        process_name,
                        spp->properties ? spp->properties + 1 : "");
        }
    }
    // if no more services or taps, remove the whole services_entry:
    // service_name might actually be ss->key, in which case is could
    // be freed, so keep a copy so we can send notification below
    char name[MAX_SERVICE_LEN];
    strncpy(name, srv_name, MAX_SERVICE_LEN);
    remove_if_empty();

    // if the service was local, tell other processes that it is gone
    if (proc == o2_ctx->proc
#ifndef O2_NO_BRIDGES
        || ISA_BRIDGE(proc)
#endif
       ) {
        o2_notify_others(name, false, NULL, NULL);
    }
    o2_do_not_reenter--;
    return O2_SUCCESS;
}



/*
 * Remove a service offering from proc. If this is the last use of the
 * service, remove the service entirely. If the service has already
 * been looked up, you can pass in the services_entry and index of the
 * service matching proc. Otherwise, pass NULL for ss. If the index
 * is unknown, pass -1 to do a search.
 *
 * CASE 1: OSC_TCP_CLIENT gets hangup, tag==OSC_TCP_CLIENT,
 *         proc is o2_ctx->proc
 *     find osc_info_entry that is offering the service
 *     remove the osc_info_entry pointer from services_entry
 *     if services_entry is empty, remove service from path_tree
 *     notify that service is gone from proc
 *     the osc_info_entry's service_name is owned by the services_entry
 *     free the osc_info_entry
 *  
 * CASE 2: /@public:internal:port/sv gets a service removed message.
 *         info is remote
 *
 * CASE 3: NET_TCP_CLIENT or _CONNECTION gets hangup. remove_remote_services
 *         calls this with each service to do the work. We remove the
 *         back-pointer from info to services.
 *
 * CASE 4: service is removed from local process by o2_service_free()
 *
 * CASE 5: service is a bridge service, proc is the o2_ctx-proc
 */
O2err Services_entry::proc_service_remove(const char *service_name,
                       Proxy_info *proc, Services_entry *ss, int index)
{
    if (!ss) {
        ss = *find(service_name);
        index = -1; // indicates we should search ss
    }
    if (!ss || !ISA_SERVICES(ss)) {
        O2_DBG(printf("%s o2_service_remove(%s, %s) did not find "
                      "service\n",
                      o2_debug_prefix, service_name,
                      ISA_REMOTE_PROC(proc) ? proc->key : "local"));
        return O2_FAIL;
    }
    return ss->service_remove(service_name, index, proc);
}


// remove a tap. If tapper is NULL, remove all taps that forward
// to proc.
// returns O2_SUCCESS if at least one tap was removed, o.w. O2_FAIL
//
O2err Services_entry::tap_remove(Proxy_info *proc, const char *tapper)
{
    O2err result = O2_FAIL;
    for (int i = 0; i < taps.size(); i++) {
        Service_tap *tap = &taps[i];
        if (tap->proc == proc && (!tapper || streql(tap->tapper, tapper))) {
            O2_FREE(tap->tapper);
            taps.remove(i);
            result = O2_SUCCESS;
            if (tapper) break; // only removing one tap, so we're done now
        }
    }
    // if we removed something, see if services has become empty and needs to
    // be removed. (It's actually safe -- but useless -- to call this even
    // if nothing was removed):
    if (result == O2_SUCCESS) {
        remove_if_empty();
    }
    return result;
}


// find existing services_entry or create an empty services_entry
// for service_name
//
Services_entry *Services_entry::must_get_services(o2string service_name)
{
    Services_entry **services = (Services_entry **)
            o2_ctx->path_tree.lookup(service_name);
    if (*services) return *services;
    Services_entry *s = new Services_entry(service_name);
    o2_ctx->path_tree.entry_insert_at((O2node **) services, s);
    return s;
}


// remove a service from o2_ctx->path_tree
//
O2err o2_service_free(const char *service_name)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    if (!service_name || strchr(service_name, '/'))
        return O2_BAD_NAME;
    return Services_entry::proc_service_remove(service_name,
                                               o2_ctx->proc, NULL, -1);
}


// put a list of services_entry's into an *unitialized* dynamic array
//
void Services_entry::list_services(Vec<Services_entry *> &list)
{
    Enumerate enumerator(&o2_ctx->path_tree);
    O2node * entry;
    while ((entry = enumerator.next())) {
        Services_entry *services = TO_SERVICES_ENTRY(entry);
        list.push_back(services);
    }
}


// for each services_entry:
//     find the service offered by this process and remove it
//     if a service is the last service in services, remove the 
//         services_entry as well
//
O2err Services_entry::remove_services_by(Proxy_info *proc)
{
    // This is pretty messy. We cannot remove a service without
    // possibly rehashing the services hash table, so we have to
    // first make a list of services. Then iterate over that list
    // to remove services.
    Vec<Services_entry *> services_list;
    assert(proc != o2_ctx->proc); // assumes remote proc
    list_services(services_list);
    O2err result = O2_SUCCESS;
    for (int i = 0; i < services_list.size(); i++) {
        Services_entry *services = services_list[i];
        for (int j = 0; j < services->services.size(); j++) {
            Service_provider *spp = &services->services[j];
            if (spp->service == proc) {
                if (!services->proc_service_remove(services->key, proc,
                                                   services, j)) {
                    result = O2_FAIL; // this should never happen
                }
                break; // can only be one of services offered by proc, and maybe
                // even services was removed, so we should move on to the next
                // service in services list
            }
        }
    }
    return result;
}


// for each services_entry:
//     remove taps that forward to this process
//     if a service is the last service in services, remove the
//         services_entry as well
//
O2err Services_entry::remove_taps_by(Proxy_info *proc)
{
    Vec<Services_entry *> services_list;
    assert(proc != o2_ctx->proc); // assumes remote proc
    list_services(services_list);
    O2err result = O2_SUCCESS;
    for (int i = 0; i < services_list.size(); i++) {
        Services_entry *services = services_list[i];
        if (services->tap_remove(proc, NULL) == O2_FAIL) {
            result = O2_FAIL; // avoid infinite loop, can't remove tap
        }
    }
    return result;
}


Services_entry::~Services_entry()
{
    for (int i = 0; i < services.size(); i++) {
        Service_provider *spp = &services[i];
        O2node *prvdr = spp->service;
        if (ISA_LOCAL_SERVICE(prvdr)) {
            delete prvdr;
        } else assert(ISA_REMOTE_PROC(prvdr));
        // free the properties string if any
        if (spp->properties) {
            O2_FREE(spp->properties);
        }
    }
    // free the taps
    for (int i = 0; i < taps.size(); i++) {
        Service_tap *info = &taps[i];
        O2_FREE(info->tapper);
    }
}


#ifndef O2_NO_DEBUG
void Services_entry::show(int indent)
{
    O2node::show(indent);
    printf("\n");
    indent++;
    for (int j = 0; j < services.size(); j++) {
        services[j].service->show(indent);
    }
}
#endif

bool Services_entry::add_service(o2string our_ip_port,
                                 O2node *service, char *properties)
{
    // find insert location: either at front or at back of services->services
    int index = services.size();
    services.append_space(1);
    // new service will go into services at index
    if (index > 0) { // see if we should go first
        // find the top entry
        Service_provider *top_entry = &services[0];
        o2string top_ipport = top_entry->service->get_proc_name();
        if (strcmp(our_ip_port, top_ipport) > 0) {
            // move top entry from location 0 to end of array at index
            services[index] = *top_entry;
            index = 0; // put new service at the top of the list
        }
    }
    // index is now indexing the first or last of services
    Service_provider *target = &services[index];
    target->service = service;
    target->properties = properties;
    return (index == 0); // new service
}
