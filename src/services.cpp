/* services.c -- mapping from names to lists of services */

/* Roger B. Dannenberg
 * April 2020
 */

#include "o2internal.h"
#include "services.h"
#include "message.h"
#include "msgsend.h"
#include "o2osc.h"
#include "bridge.h"
#include "ctype.h"
#include "mqtt.h"

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
    - o2_service_new2(), calls
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
// service name with zero padding
o2_err_t o2_service_new2(o2string padded_name)
{
    // find services_node if any
    hash_node_ptr node = o2_hash_node_new(NULL);
    if (!node) return O2_FAIL;
    // this will send /_o2/si message to local process:
    o2_err_t rslt = o2_service_provider_new(padded_name, NULL,
                               (o2_node_ptr) node, o2_ctx->proc);
    if (rslt != O2_SUCCESS) {
        O2_FREE(node);
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
 * CASE 4: handling /@public:internal:port/sv: service is a o2n_info_ptr equal
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
o2_err_t o2_service_provider_new(o2string service_name,
            const char *properties, o2_node_ptr service, proc_info_ptr proc)
{
    bool active = false;
    O2_DBd(printf("%s %s o2_service_provider_new adding %s to %s\n",
                  o2_debug_prefix,
                  // highlight when proc->name is our IP:Port info:
                  (streql(service_name, "_o2") ? "****" : ""),
                  service_name, proc->name));
    services_entry_ptr ss = o2_must_get_services(service_name);
    // services exists, is this service already offered by proc?
    service_provider_ptr spp = o2_proc_service_find(proc, ss);

    // adjust properties to be either NULL or non-empty property string
    if (properties) {
        if (properties[0] == 0 || // convert "" or ";" to NULL (no properties)
            (properties[0] == ';' && properties[1] == 0)) {
            properties = NULL;
        } else {
            properties = o2_heapify(properties);
        }
    }

    if (spp && (IS_REMOTE_PROC(spp->service)
#ifndef O2_NO_MQTT
                || IS_MQTT_PROC(spp->service)
#endif
               )) {
        // now we know this is a remote service and we can set the properties
        O2_DBd(printf("%s o2_service_provider_new service exists %s\n",
                      o2_debug_prefix, service_name));
        active = DA_GET_ADDR(ss->services, service_provider, 0)->service ==
                 (o2_node_ptr) proc;
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
        active = o2_add_to_service_list(ss, proc->name, service,
                                        (char *) properties);
        O2_DBG(printf("%s ** new service %s is %p (%s) active %d\n",
                      o2_debug_prefix, ss->key, service,
                      o2_tag_to_string(service->tag), active);
               o2_node_show((o2_node_ptr) &o2_ctx->path_tree, 2));
    }
    if (active) {
        // we have an update in the active service, so report it to the local
        // process; /si msg needs: *service_name* *status* *process-name*
        int status = o2_status_from_proc(service, NULL);
        // If this is a new process connection, process_name is NULL
        // and we do not send !_o2/si yet. See o2n_recv() in o2_net.c
        // where protocol completes and !_o2/si is sent.
        // Also, we always send _o2 to name the local process.
        const char *proc_name = (proc == o2_ctx->proc ? "_o2" : proc->name);
        o2_send_cmd("!_o2/si", 0.0, "siss", service_name, status,
                    proc_name, properties ? properties + 1 : "");
    }
    return O2_SUCCESS;
}


/* find existing services_entry node for service_name. If none exists,
 *     return NULL.
 * prereq: service_name does not contain '/'
 */
services_entry_ptr *o2_services_find(const char *service_name)
{
    // all callers are passing in (possibly) unaligned strings, so we
    // need to copy the service_name to aligned storage and pad it
    char key[NAME_BUF_LEN];
    o2_string_pad(key, service_name);
    return (services_entry_ptr *) o2_lookup(&o2_ctx->path_tree, key);
}    


// find the service for this message
//
o2_node_ptr o2_msg_service(o2_msg_data_ptr msg, services_entry_ptr *services)
{
    char *service_name = msg->address + 1;
    char *slash = strchr(service_name, '/');
    if (slash) *slash = 0;
    o2_node_ptr rslt = NULL; // return value if not found
    // When a message if forwarded to a tap, it is marked with the O2_TAP_FLAG
    // and delivered to a specific tapper process. So if O2_TAP_FLAG is set,
    // we need to find the service offered by this local process even if it
    // is not the active service, so we cannot use o2_service_find:
    if (msg->flags & O2_TAP_FLAG) {
        *services = *o2_services_find(service_name);
        service_provider_ptr spp = o2_proc_service_find(o2_ctx->proc, *services);
        if (spp) rslt = spp->service;
    } else {
        rslt = o2_service_find(service_name, services);
    }
    if (slash) *slash = '/';
    return rslt;
}

/* prereq: service_name does not contain '/'
 */
o2_node_ptr o2_service_find(const char *service_name,
                            services_entry_ptr *services)
{
    *services = *o2_services_find(service_name);
    if (!*services) {
        // map local @public:internal:port string to "_o2": Note that we
        // could save a hash
        // lookup by doing this test first, but I think this is pretty rare:
        // only system messages from remote processes will use @pip:iip:port/
        if (service_name[0] == '@' && o2_ctx->proc->name &&
             streql(service_name, o2_ctx->proc->name)) {
            *services = *o2_services_find("_o2");
        } else {
            return NULL;
        }
    }
    // service entry could have taps but no service provider yet
    if ((*services)->services.length == 0) {
        return NULL;
    }
    return GET_SERVICE((*services)->services, 0);
}


o2_err_t o2_services_insert_tap(services_entry_ptr ss, o2string tapper,
                                proc_info_ptr proc)
{
    service_tap_ptr tap = DA_EXPAND(ss->taps, service_tap);
    tap->tapper =tapper;
    tap->proc = proc;
    return O2_SUCCESS;
}


services_entry_ptr *o2_services_from_msg(o2_message_ptr msg)
{
    char *service_name = msg->data.address + 1; // skip '/' or '!'
    char *slash = strchr(service_name, '/');
    if (slash) *slash = 0;
    services_entry_ptr *s = o2_services_find(service_name);
    if (slash) *slash = '/';
    return s;
}


/** find address of service in services that is offered by proc, if any */
service_provider_ptr o2_proc_service_find(proc_info_ptr proc,
                                          services_entry_ptr services)
{
    if (!services) return NULL;
    for (int i = 0; i < services->services.length; i++) {
        service_provider_ptr spp =
                DA_GET_ADDR(services->services, service_provider, i);
        o2_node_ptr service = spp->service;
        if (IS_REMOTE_PROC(service) || service->tag == PROC_TCP_SERVER) {
            if (TO_PROC_INFO(service) == proc) {
                return spp;
            }
        } else if (service->tag == NODE_HASH || service->tag == NODE_HANDLER
#ifndef O2_NO_OSC
                   || service->tag == OSC_TCP_CLIENT
#endif
#ifndef O2_NO_BRIDGES
                   || ISA_BRIDGE(service)
#endif
                  ) { // must be local
            if (o2_ctx->proc == proc) {
                return spp; // local service already exists
            }
        }
    }
    return NULL;
}


// in the list of services, find the service with the highest service provider
// name and move it to the top position in the list. This is called when the
// top (active) service is removed and must be replaced
static void pick_service_provider(dyn_array_ptr list)
{
    int top_index = 0;
    int search_start = 1;
    if (top_index >= list->length) return;
    o2_node_ptr top_node = GET_SERVICE(*list, top_index);
    const char *top_name = o2_node_to_proc_name(top_node);
    for (int i = search_start; i < list->length; i++) {
        o2_node_ptr node = GET_SERVICE(*list, i);
        const char *name = o2_node_to_proc_name(node);
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
    DA_SET(*list, o2_node_ptr, top_index, GET_SERVICE(*list, 0));
    DA_SET(*list, o2_node_ptr, 0, top_node);
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
 *         a hash_node where we can install a NODE_HASH based on the
 *         next node in the address.
 * (no more cases because we moved others to o2_service_remove)
 */
o2_err_t o2_service_provider_replace(const char *service_name,
               o2_node_ptr *node_ptr, o2_node_ptr new_service)
{
    assert(new_service);
    // clean up the old service node
    if ((*node_ptr)->tag == NODE_HASH ||
        (*node_ptr)->tag == NODE_HANDLER ||
        (*node_ptr)->tag == NODE_EMPTY) {
        o2_node_free(*node_ptr);
    } else if (
#ifndef O2_NO_OSC
               (*node_ptr)->tag == OSC_TCP_CLIENT ||
               (*node_ptr)->tag == OSC_UDP_CLIENT ||
#endif
#ifndef O2_NO_BRIDGES
               ISA_BRIDGE(*node_ptr) ||
#endif
               false) { // should optimize out if neither OSC nor BRIDGES
        // service is delegated, so you cannot install local handler
        return O2_SERVICE_EXISTS;
    } else {
        O2_DBG(printf("%s o2_service_provider_replace(%s, ...) did not find "
                      "service offered by this process\n",
                      o2_debug_prefix, service_name));
        return O2_FAIL;  // unexpected tag, give up
    }
    *node_ptr = new_service; // install the new service
    // ASSERT: i is now the index of the service we are replacing
    return O2_SUCCESS;
}


// This code is used by o2_service_remove and o2_tap_remove_from:
// it checks if a services_entry no longer has services or taps.
// If not, it removes the services entry completely.
//
static void remove_empty_services_entry(services_entry_ptr ss)
{
    // if no service providers or taps left, remove service entry
    if (ss->services.length == 0 && ss->taps.length == 0) {
        // printf("Removing %s from &o2_ctx->path_tree\n", ss->key);
        o2_remove_hash_entry_by_name(&o2_ctx->path_tree, ss->key);
        // printf(" Here is the result:\n");
        // o2_node_show((o2_node_ptr) &o2_ctx->path_tree, 2);
        // service name (the key in path_tree) is now freed.
    }
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
o2_err_t o2_service_remove(const char *service_name, proc_info_ptr proc,
                           services_entry_ptr ss, int index)
{
    if (!ss) {
        ss = *o2_services_find(service_name);
        index = -1; // indicates we should search ss
    }
    if (!ss || ss->tag != NODE_SERVICES) {
        O2_DBG(printf("%s o2_service_remove(%s, %s) did not find "
                      "service\n",
                      o2_debug_prefix, service_name,
                      IS_REMOTE_PROC(proc) ? proc->name : "local"));
        return O2_FAIL;
    }
    dyn_array_ptr svlist = &ss->services; // list of services
    
    // search for the entry in the list of services that corresponds to proc
    if (index < 0) {
        for (index = 0; index < svlist->length; index++) {
            o2_node_ptr serv = GET_SERVICE(*svlist, index);
            int tag = serv->tag;
            if (IS_REMOTE_PROC(serv) && TO_PROC_INFO(serv) == proc) {
                break;
            } else if ((tag == NODE_HASH || tag == NODE_HANDLER ||
                        tag == NODE_EMPTY
#ifndef O2_NO_BRIDGES
                        || tag == BRIDGE_NOCLOCK
                        || tag == BRIDGE_SYNCED
                       ) && proc == o2_ctx->proc) {
#endif
                o2_node_free(serv);
                break;
#ifndef O2_NO_OSC
            } else if (tag == OSC_TCP_CLIENT) {
                osc_info_ptr osc = TO_OSC_INFO(serv);
                // clearing service_name prevents o2_osc_info_free() from
                // trying to remove the service (again):
                O2_FREE(osc->service_name);
                osc->service_name = NULL;
                // o2n_close_socket does nothing if the socket is already closed
                o2n_close_socket(osc->net_info);
                // later, when socket is removed, o2_osc_info_free()
                // will be called
                break;
            } else if (tag == OSC_UDP_CLIENT) {
                // UDP client is not referenced by an o2n_info->application,
                // so we can delete it now. Calling o2_osc_info_free() would
                // recursively try to remove the service, so simply free the
                // osc_info object -- there's nothing else to clean up.
                O2_FREE(TO_OSC_INFO(serv));
                break;
#endif
            } else {
                assert(false);
            }
        }
    }
    // if we did not find what we wanted to remove, stop here
    if (index >= svlist->length) {
        O2_DBG(printf("%s o2_service_remove(%s, %s, ...) did not find "
                      "service offered by this process\n",
                      o2_debug_prefix, service_name,
                      IS_REMOTE_PROC(proc) ? proc->name : "local"));
        return O2_FAIL;
    }
    // ASSERT: index is now the index of the service we are deleting or
    //         replacing
    //
    // we found the service to replace; finalized the info depending on the
    // type, so now we have a dangling pointer in the services list
    char *properties = GET_SERVICE_PROVIDER(*svlist, index)->properties;
    if (properties) {
        O2_FREE(properties);
    }
    DA_REMOVE(*svlist, service_provider, index);

    o2_do_not_reenter++; // protect data structures
    // send notification message
    o2_send_cmd("!_o2/si", 0.0, "siss", service_name, O2_FAIL,
                o2_node_to_proc_name((o2_node_ptr) proc), "");

    // if we deleted active service, pick a new one
    if (index == 0) { // move top @public:internal:port provider to top spot
        pick_service_provider(svlist);
    }
    // now we probably have a new service, report it:
    if (svlist->length > 0) {
        service_provider_ptr spp = GET_SERVICE_PROVIDER(*svlist, 0);
        const char *process_name;
        int status = o2_status_from_proc(spp->service, &process_name);
        if (status != O2_FAIL) {
            assert(process_name[0]);
            o2_send_cmd("!_o2/si", 0.0, "siss", service_name, status,
                        process_name,
                        spp->properties ? spp->properties + 1 : "");
        }
    }
    // if no more services or taps, remove the whole services_entry:
    // service_name might actually be ss->key, in which case is could
    // be freed, so keep a copy so we can send notification below
    char name[MAX_SERVICE_LEN];
    strncpy(name, service_name, MAX_SERVICE_LEN);
    remove_empty_services_entry(ss);

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


// remove a tap. If tapper is NULL, remove all taps that forward
// to proc.
// returns O2_SUCCESS if at least one tap was removed, o.w. O2_FAIL
//
o2_err_t o2_tap_remove_from(services_entry_ptr ss, proc_info_ptr proc,
                            const char *tapper)
{
    o2_err_t result = O2_FAIL;
    for (int i = 0; i < ss->taps.length; i++) {
        service_tap_ptr tap = GET_TAP_PTR(ss->taps, i);
        if (tap->proc == proc && (!tapper || streql(tap->tapper, tapper))) {
            O2_FREE(tap->tapper);
            DA_REMOVE(ss->taps, service_tap, i);
            result = O2_SUCCESS;
            if (tapper) break; // only removing one tap, so we're done now
        }
    }
    // if we removed something, see if services has become empty and needs to
    // be removed. (It's actually safe -- but useless -- to call this even
    // if nothing was removed):
    if (result == O2_SUCCESS) {
        remove_empty_services_entry(ss);
    }
    return result;
}


// find existing services_entry or create an empty services_entry
// for service_name
//
services_entry_ptr o2_must_get_services(o2string service_name)
{
    services_entry_ptr *services = (services_entry_ptr *)
            o2_lookup(&o2_ctx->path_tree, service_name);
    if (*services) return *services;
    services_entry_ptr s = O2_CALLOCT(services_entry);
    s->tag = NODE_SERVICES;
    s->key = o2_heapify(service_name);
    s->next = NULL;
    DA_INIT(s->services, service_provider, 1);
    // No need to initialize s->taps because it is empty.
    o2_add_entry_at(&o2_ctx->path_tree, (o2_node_ptr *) services,
                    (o2_node_ptr) s);
    return s;
}


// remove a service from o2_ctx->path_tree
//
int o2_service_free(const char *service_name)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    if (!service_name || strchr(service_name, '/'))
        return O2_BAD_NAME;
    return o2_service_remove(service_name, o2_ctx->proc, NULL, -1);
}

// put a list of services_entry's into an *unitialized* dynamic array
//
void o2_list_services(dyn_array_ptr list)
{
    DA_INIT(*list, services_entry_ptr, o2_ctx->path_tree.num_children);
    enumerate enumerator;
    o2_enumerate_begin(&enumerator, &o2_ctx->path_tree.children);
    o2_node_ptr entry;
    while ((entry = o2_enumerate_next(&enumerator))) {
        services_entry_ptr services = TO_SERVICES_ENTRY(entry);
        DA_APPEND(*list, services_entry_ptr, services);
    }
}


// for each services_entry:
//     find the service offered by this process and remove it
//     if a service is the last service in services, remove the 
//         services_entry as well
//
int o2_remove_services_by(proc_info_ptr proc)
{
    // This is pretty messy. We cannot remove a service without
    // possibly rehashing the services hash table, so we have to
    // first make a list of services. Then iterate over that list
    // to remove services.
    dyn_array services_list;
    assert(proc != o2_ctx->proc); // assumes remote proc
    o2_list_services(&services_list);
    int result = O2_SUCCESS;
    for (int i = 0; i < services_list.length; i++) {
        services_entry_ptr services =
                DA_GET(services_list, services_entry_ptr, i);
        for (int j = 0; j < services->services.length; j++) {
            service_provider_ptr spp =
                    GET_SERVICE_PROVIDER(services->services, j);
            if (spp->service == (o2_node_ptr) proc) {
                if (!o2_service_remove(services->key, proc, services, j)) {
                    result = O2_FAIL; // this should never happen
                }
                break; // can only be one of services offered by proc, and maybe
                // even services was removed, so we should move on to the next
                // service in services list
            }
        }
    }
    DA_FINISH(services_list);
    return result;
}


// for each services_entry:
//     remove taps that forward to this process
//     if a service is the last service in services, remove the
//         services_entry as well
//
int o2_remove_taps_by(proc_info_ptr proc)
{
    dyn_array services_list;
    assert(proc != o2_ctx->proc); // assumes remote proc
    o2_list_services(&services_list);
    int result = O2_SUCCESS;
    for (int i = 0; i < services_list.length; i++) {
        services_entry_ptr services =
                DA_GET(services_list, services_entry_ptr, i);
        if (o2_tap_remove_from(services, proc, NULL) == O2_FAIL) {
            result = O2_FAIL; // avoid infinite loop, can't remove tap
        }
    }
    DA_FINISH(services_list);
    return result;
}



void o2_services_entry_finish(services_entry_ptr ss)
{
    for (int i = 0; i < ss->services.length; i++) {
        service_provider_ptr spp = GET_SERVICE_PROVIDER(ss->services, i);
        o2_node_ptr service = spp->service;
        if (service->tag == NODE_HASH || service->tag == NODE_HANDLER
#ifndef O2_NO_BRIDGES
            || ISA_BRIDGE(service)
#endif
            ) {
            o2_node_free(service);
#ifndef O2_NO_OSC
        } else if (ISA_OSC(service)) {
            osc_info_ptr osc = TO_OSC_INFO(service);
            o2_osc_info_free(osc);
#endif
        } else assert(IS_REMOTE_PROC(service));
        // free the properties string if any
        if (spp->properties) {
            O2_FREE(spp->properties);
        }
    }
    DA_FINISH(ss->services);
    // free the taps
    for (int i = 0; i < ss->taps.length; i++) {
        service_tap_ptr info = GET_TAP_PTR(ss->taps, i);
        O2_FREE((void *) info->tapper);
    }
    DA_FINISH(ss->taps);
    O2_FREE(ss->key);
}


bool o2_add_to_service_list(services_entry_ptr ss, o2string our_ip_port,
                           o2_node_ptr service, char *properties)
{
    // find insert location: either at front or at back of services->services
    DA_EXPAND(ss->services, service_provider);
    int index = ss->services.length - 1;
    // new service will go into services at index
    if (index > 0) { // see if we should go first
        // find the top entry
        service_provider_ptr top_entry = GET_SERVICE_PROVIDER(ss->services, 0);
        o2string top_ipport = o2_node_to_proc_name(top_entry->service);
        if (strcmp(our_ip_port, top_ipport) > 0) {
            // move top entry from location 0 to end of array at index
            DA_SET(ss->services, service_provider, index, *top_entry);
            index = 0; // put new service at the top of the list
        }
    }
    // index is now indexing the first or last of services
    service_provider_ptr target = GET_SERVICE_PROVIDER(ss->services, index);
    target->service = service;
    target->properties = properties;
    o2_mem_check(ss->services.array);
    return (index == 0); // new service
}


#ifndef O2_NO_DEBUG
void o2_services_entry_show(services_entry_ptr s, int indent)
{
    for (int j = 0; j < s->services.length; j++) {
        o2_node_show((o2_node_ptr) GET_SERVICE(s->services, j), indent);
    }
}
#endif
