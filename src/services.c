/* services.c -- mapping from names to lists of services */

/* Roger B. Dannenberg
 * April 2020
 */

#include "o2internal.h"
#include "services.h"
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
    - o2_service_new2(), calls
        - o2_service_provider_new()
- o2_osc_delegate creates a service to forward messages over OSC, calls
    - o2_service_provider_new()
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
    return (services_entry_ptr *) o2_lookup(&o2_context->path_tree, key);
}    


// find the service for this message
//
o2_node_ptr o2_msg_service(o2_msg_data_ptr msg, services_entry_ptr *services)
{
    char *service_name = msg->address + 1;
    char *slash = strchr(service_name, '/');
    if (slash) *slash = 0;
    o2_node_ptr rslt = NULL; // return value if not found
    //  Incoming messages should have IP:PORT mapped to _o2, and IP:PORT
    //  should not be in services at all. See if things work that way
    //  when we can trace the execution. I'm worried somewhere that IP:PORT
    //  might be added to services by some discovery call and I could miss it
    //  so need to check at runtime.
    rslt = o2_service_find(service_name, services);
    if (rslt && rslt->tag == PROC_TCP_SERVER) {
        // service name might be an IP:PORT string. Map that
        // to _o2, so _o2 is an alias for IP:PORT. (However,
        // messages using "!" will do a hash table lookup on
        // the full address (!IP:PORT/x/y), so if the method
        // is entered as !_o2/x/y, the lookup will fail.)
        rslt = o2_service_find("_o2", services);
    }
    /* } */
    if (slash) *slash = '/';
    return rslt;
}

/* prereq: service_name does not contain '/'
 */
o2_node_ptr o2_service_find(const char *service_name, services_entry_ptr *services)
{
    *services = *o2_services_find(service_name);
    if (!*services)
        return NULL;
    assert((*services)->services.length > 0);
    return GET_SERVICE((*services)->services, 0);
}


int o2_services_insert_tap(services_entry_ptr ss, o2string tapper,
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
o2_node_ptr *o2_proc_service_find(proc_info_ptr proc,
                                  services_entry_ptr services)
{
    if (!services) return NULL;
    for (int i = 0; i < services->services.length; i++) {
        o2_node_ptr *service_ptr = DA_GET_ADDR(services->services, o2_node_ptr, i);
        o2_node_ptr service = *service_ptr;
        if (PROC_IS_REMOTE(service) || service->tag == PROC_TCP_SERVER) {
            if (TO_PROC_INFO(service) == proc) {
                return service_ptr;
            }
        } else if (service->tag == OSC_TCP_CLIENT) {
            if (o2_context->proc == proc) {
                return service_ptr;
            }
        } else if (service->tag == NODE_HASH ||
                   service->tag == NODE_HANDLER) { // must be local
            if (o2_context->proc == proc) {
                return service_ptr; // local service already exists
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
    const char *top_name = o2_node_to_ipport(top_node);
    for (int i = search_start; i < list->length; i++) {
        o2_node_ptr node = GET_SERVICE(*list, i);
        const char *name = o2_node_to_ipport(node);
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
int o2_service_provider_replace(const char *service_name,
          o2_node_ptr *node_ptr, o2_node_ptr new_service)
{
    assert(new_service);
    // clean up the old service node
    if ((*node_ptr)->tag == NODE_HASH ||
        (*node_ptr)->tag == NODE_HANDLER) {
        o2_node_free(*node_ptr);
    } else if ((*node_ptr)->tag == OSC_TCP_CLIENT ||
               (*node_ptr)->tag == OSC_TCP_CLIENT ||
               (*node_ptr)->tag == NODE_BRIDGE_SERVICE) {
        // service is delegated to OSC, so you cannot install local handler
        return O2_SERVICE_CONFLICT;
    } else {
        O2_DBg(printf("%s o2_service_provider_replace(%s, ...) did not find "
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
        printf("Removing %s from &o2_context->path_tree\n", ss->key);
        o2_remove_hash_entry_by_name(&o2_context->path_tree, ss->key);
        printf(" Here is the result:\n");
        o2_node_show((o2_node_ptr) &o2_context->path_tree, 2);
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
 * CASE 1: OSC_TCP_CLIENT gets hangup. Get service_name is
 *         from with tag==OSC_TCP_CLIENT,
 *         proc is o2_context->proc
 *     finds and frees osc_info_entry pointed to by services_entry
 *     removes the osc_info_entry pointer from services_entry
 *     if services_entry is empty, remove service from path_tree
 *     if proc is o2_context->proc, notify that service is gone from proc
 *  
 * CASE 2: /ip:port/sv gets a service removed message. info is remote
 *
 * CASE 3: NET_TCP_CLIENT or _CONNECTION gets hangup. remove_remote_services
 *         calls this with each service to do the work. We remove the
 *         back-pointer from info to services.
 *
 * CASE 4: service is removed from local process by o2_service_free()
 */
int o2_service_remove(const char *service_name, proc_info_ptr proc,
                      services_entry_ptr ss, int index)
{
    if (!ss) {
        ss = *o2_services_find(service_name);
        index = -1; // indicates we should search ss
    }
    if (!ss || ss->tag != NODE_SERVICES) {
        O2_DBg(printf("%s o2_service_remove(%s, %s) did not find "
                      "service\n",
                      o2_debug_prefix, service_name, proc->name));
        return O2_FAIL;
    }
    dyn_array_ptr svlist = &(ss->services); // list of services
    
    // search for the entry in the list of services that corresponds to info
    if (index < 0) {
        for (index = 0; index < svlist->length; index++) {
            o2_node_ptr serv = GET_SERVICE(*svlist, index);
            int tag = serv->tag;
            if (PROC_IS_REMOTE(serv) && TO_PROC_INFO(serv) == proc) {
                break;
            } else if ((tag == NODE_HASH || tag == NODE_HANDLER) &&
                       proc == o2_context->proc) {
                o2_node_free(serv);
                break;
            } else if (tag == OSC_TCP_CLIENT) {
                osc_info_ptr osc = TO_OSC_INFO(serv);
                // clearing service_name prevents o2_osc_info_free() from
                // trying to remove the service (again):
                osc->service_name = NULL;
                    // setting to N
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
            } else {
                assert(tag != NODE_BRIDGE_SERVICE);
            }
        }
    }
    // if we did not find what we wanted to replace, stop here
    if (index >= svlist->length) {
        O2_DBg(printf("%s o2_service_remove(%s, %s, ...) did not find "
                      "service offered by this process\n",
                      o2_debug_prefix, service_name, proc->name));
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
    assert(proc->name[0]);
    // exclude reports of our own IP:PORT service
    if (!isdigit(service_name[0]) || proc->tag != PROC_TCP_SERVER) {
        o2_send_cmd("!_o2/si", 0.0, "sis", service_name, O2_FAIL,
                    proc->name);
    }

    // if we deleted active service, pick a new one
    if (index == 0) { // move top ip:port provider to top spot
        pick_service_provider(svlist);
    }
    // now we probably have a new service, report it:
    if (svlist->length > 0) {
        o2_node_ptr new_service = GET_SERVICE(*svlist, 0);
        const char *process_name;
        int status = o2_status_from_proc(new_service, &process_name);
        if (status != O2_FAIL) {
            assert(process_name[0]);
            // exclude reports of our own IP:PORT service
            if (!isdigit(service_name[0] || new_service->tag != PROC_TCP_SERVER)) {
                o2_send_cmd("!_o2/si", 0.0, "sis", service_name, status,
                            process_name);
            }
        }
    }
    // if no more services or taps, remove the whole services_entry:
    remove_empty_services_entry(ss);

    // if the service was local, tell other processes that it is gone
    if (proc == o2_context->proc) {
        o2_notify_others(service_name, FALSE, NULL, NULL);
    }
    o2_do_not_reenter--;
    return O2_SUCCESS;
}


// remove a tap. If tapper is NULL, remove all taps that forward
// to proc.
// returns O2_SUCCESS if at least one tap was removed, o.w. O2_FAIL
//
int o2_tap_remove_from(services_entry_ptr ss, proc_info_ptr proc,
                       o2string tapper)
{
    int result = O2_FAIL;
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
            o2_lookup(&o2_context->path_tree, service_name);
    if (*services) return *services;
    services_entry_ptr s = O2_CALLOC(1, sizeof(services_entry));
    s->tag = NODE_SERVICES;
    s->key = o2_heapify(service_name);
    s->next = NULL;
    DA_INIT(s->services, o2n_info_ptr, 1);
    // No need to initialize s->taps because it is empty.
    o2_add_entry_at(&o2_context->path_tree, (o2_node_ptr *) services,
                    (o2_node_ptr) s);
    return s;
}


// remove a service from o2_context->path_tree
//
int o2_service_free(const char *service_name)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    if (!service_name || strchr(service_name, '/'))
        return O2_BAD_SERVICE_NAME;
    return o2_service_remove(service_name, o2_context->proc, NULL, -1);
}

// put a list of services_entry's into an *unitialized* dynamic array
//
static void list_services(dyn_array_ptr list)
{
    DA_INIT(*list, services_entry_ptr, o2_context->path_tree.num_children);
    enumerate enumerator;
    o2_enumerate_begin(&enumerator, &o2_context->path_tree.children);
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
    assert(proc != o2_context->proc); // assumes remote proc
    list_services(&services_list);
    int result = O2_SUCCESS;
    for (int i = 0; i < services_list.length; i++) {
        services_entry_ptr services = DA_GET(services_list, services_entry_ptr, i);
        for (int j = 0; j < services->services.length; j++) {
            service_provider_ptr spp = GET_SERVICE_PROVIDER(services->services, i);
            if (spp->service == (o2_node_ptr) proc) {
                if (!o2_service_remove(services->key, proc, services, i)) {
                    result = O2_FAIL; // this should never happen
                }
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
    assert(proc != o2_context->proc); // assumes remote proc
    list_services(&services_list);
    int result = O2_SUCCESS;
    for (int i = 0; i < services_list.length; i++) {
        services_entry_ptr services = DA_GET(services_list, services_entry_ptr, i);
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
        o2_node_ptr service = GET_SERVICE(ss->services, i);
        if (service->tag == NODE_HASH || service->tag == NODE_HANDLER) {
            o2_node_free(service);
        } else if (ISA_OSC(service)) {
            osc_info_ptr osc = TO_OSC_INFO(service);
            o2_osc_info_free(osc);
        } else assert(PROC_IS_REMOTE(service));
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


int o2_add_to_service_list(services_entry_ptr ss, o2string our_ip_port,
                           o2_node_ptr service)
{
    // find insert location: either at front or at back of services->services
    DA_EXPAND(ss->services, o2_node_ptr);
    int index = ss->services.length - 1;
    // new service will go into services at index
    if (index > 0) { // see if we should go first
        // find the top entry
        o2_node_ptr top_entry = GET_SERVICE(ss->services, 0);
        o2string top_ipport = o2_node_to_ipport(top_entry);
        if (strcmp(our_ip_port, top_ipport) > 0) {
            // move top entry from location 0 to end of array at index
            DA_SET(ss->services, o2_node_ptr, index, top_entry);
            index = 0; // put new service at the top of the list
        }
    }
    // index is now indexing the first or last of services
    DA_SET(ss->services, o2_node_ptr, index, service);
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
