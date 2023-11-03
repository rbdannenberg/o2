/* properties.c - services and their properties for each process */

/* Roger B. Dannenberg
 * April 2020
 */

#include "o2internal.h"
#include "services.h"
#include "message.h"
#include "msgsend.h"
#include "o2osc.h"

typedef struct service_info {
    O2string name;
    O2status service_type;
    O2string process; // the port:ip of process offering the service
    O2string properties; // service properties or the tapper of tappee
} service_info, *service_info_ptr;


static Vec<service_info> service_list;


// add every active service to service_list. Get services from list of
// processes.
O2err o2_services_list()
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    o2_services_list_free();
    Enumerate enumerator(&o2_ctx->path_tree);
    O2node *entry;
    while ((entry = enumerator.next())) {
        Services_entry *services = TO_SERVICES_ENTRY(entry);
        if (services->services.size() > 0) {
            Service_provider *spp = &services->services[0];
            service_info_ptr sip = service_list.append_space(1);
            sip->name = o2_heapify(entry->key);
            sip->process = o2_heapify(spp->service->get_proc_name());
            if (ISA_PROC(spp->service)) sip->service_type = O2_REMOTE;
#ifndef O2_NO_BRIDGE
            else if (ISA_BRIDGE(spp->service)) sip->service_type = O2_BRIDGE;
#endif
#ifndef O2_NO_OSC
            else if (ISA_OSC(spp->service)) sip->service_type = O2_TO_OSC;
#endif
            else sip->service_type = O2_LOCAL;
            sip->properties = spp->properties;
            if (sip->properties) { // need to own string if any
                sip->properties = o2_heapify(sip->properties);
            }
        }
        for (int i = 0; i < services->taps.size(); i++) {
            Service_tap *stp = &services->taps[i];
            service_info_ptr sip = service_list.append_space(1);
            sip->name = o2_heapify(entry->key);
            sip->process = o2_heapify(stp->proc->key ? stp->proc->key :
                                                       "local");
            sip->service_type = O2_TAP;
            sip->properties = o2_heapify(stp->tapper);

        }
    }
    return O2_SUCCESS;
}

#define SERVICE_INFO(sl, i) DA_GET_ADDR(service_list, service_info, i)

O2err o2_services_list_free()
{
    for (int i = 0; i < service_list.size(); i++) {
        service_info_ptr sip = &service_list[i];
        O2_FREE((char *) sip->name);
        O2_FREE((char *) sip->process);
        if (sip->properties) O2_FREE((char *) sip->properties);
    }
    service_list.clear();
    return O2_SUCCESS;
}


// internal function to release all services_list memory
void o2_services_list_finish()
{
    o2_services_list_free();
    service_list.finish();
}


const char *o2_service_name(int i)
{
    if (i >= 0 && i < service_list.size()) {
        return service_list[i].name;
    }
    return NULL;
}


int o2_service_type(int i)
{
    if (i >= 0 && i < service_list.size()) {
        return service_list[i].service_type;
    }
    return O2_FAIL;
}


const char *o2_service_process(int i)
{
    if (i >= 0 && i < service_list.size()) {
        return service_list[i].process;
    }
    return NULL;
}


const char *o2_service_tapper(int i)
{
    if (i >= 0 && i < service_list.size()) {
        service_info_ptr sip = &service_list[i];
        if (sip->service_type != O2_TAP) {
            return NULL; // there is no tapper, it's a service
        }
        return sip->properties;
    }
    return NULL;
}


const char *o2_service_properties(int i)
{
    if (i >= 0 && i < service_list.size()) {
        service_info_ptr sip = &service_list[i];
        if (sip->service_type == O2_TAP) {
            return NULL; // it's a tap
        } // otherwise it's a service and properties is good
        return (sip->properties ? sip->properties + 1 : // skip initial ";"
                &(";"[1])); // generate an initial ";" for o2_service_search()
    }
    return NULL;
}


// find end of attribute (a pointer to ':' in properties string)
// attr is the attribute with no ';' or ':'. We construct ";attr:"
// and search for an exact match to find the attribute in the properties
// string.
static const char *find_attribute_end(const char *attr, const char *properties)
{
    if (properties) {
        ssize_t len = strlen(attr);
        char exact[MAX_SERVICE_LEN];
        // construct the string ";attr:" to get exact match
        exact[0] = ';';
        exact[1] = 0;
        if (len + 3 > MAX_SERVICE_LEN) {
            return NULL; // attr is too big!
        }
        strcpy(exact + 1, attr);
        exact[len + 1] = ':';
        exact[len + 2] = 0;
        const char *loc = strstr(properties, exact);
        if (loc) {
            return loc + len + 2;
        }
    }
    return NULL;
}


// how long is the value string starting at loc
static int value_len(const char *loc)
{
    const char *end = loc;
    while (*end && *end != ';') {
        if (*end && *end == '\\') {
            end++;
        }
        end++;
    }
    return (int) (end - loc); // length not including terminating character
}


// how long will value string be after inserting escape chars?
static size_t value_encoded_len(const char *loc)
{
    size_t len = 0;
    while (*loc) {
        if (*loc == '\\' || *loc == ':' || *loc == ';') len++;
        loc++;
        len++;
    }
    return len;
}


const char *o2_service_getprop(int i, const char *attr)
{
    const char *p = o2_service_properties(i);
    if (p) {
        p--; // back up to initial ";"
        const char *loc = find_attribute_end(attr, p);
        if (loc) {
            int len = value_len(loc);
            const char *end = loc + len;
            // len may be too big given that we will remove escape characters
            char *rslt = O2_MALLOCNT(len + 1, char); // include space for EOS
            // copy string value, removing escape characters
            char *dest = rslt;
            while (loc < end) {
                if (*loc == '\\') { // skip escape characters
                    loc++;
                }
                *dest++ = *loc++;
            }
            *dest = 0; // end-of-string
            return rslt;
        }
    }
    return NULL;
}


int o2_service_search(int i, const char *attr, const char *value)
{
    while (i >= 0 && i < service_list.size()) {
        const char *p = o2_service_properties(i);
        if (p) {
            p--; // back up to initial ";"
            const char *v = find_attribute_end(attr, p);
            if (v) {
                int len = value_len(v);
                // start searching at the ":" preceding v
                const char *loc = strstr(v - 1, value);
                // search must find value before v + len
                if (loc != NULL && loc <= v + len) {
                    return i;
                } // otherwise, value not found, continue search
            } // otherwise, attr not found, continue search
        } // otherwise it's a tap, continue search
        i++;
    }    
    return -1;
}


static void encode_value_to(char *p, const char *v)
{
    while (*v) {
        if (*v == '\\' || *v == ':' || *v == ';') *p++ = '\\';
        *p++ = *v++;
    }
}


// returns true if properties string has changed
//
static bool service_property_free(Service_provider *spp, const char *attr)
{
    // see if attr already exists. If so, just remove it in place.
    const char *attr_end = find_attribute_end(attr, spp->properties);
    if (attr_end) {
        // find beginning of attr as destination for copy
        char *dst = (char *) attr_end - strlen(attr) - 1;
        // find end of value
        const char *src = attr_end + value_len(attr_end) + 1;
        // splice out attr:value; <- the "+ 1" above is to splice out the ';'
        while (*src) {
            *dst++ = *src++;
        }
        *dst = 0;
        return true;
    }
    return false;
}


O2err o2_set_service_properties(Service_provider *spp, const char *service,
                               char *properties)
{
    if (spp->properties) O2_FREE(spp->properties);
    spp->properties = properties;
    assert(!properties || properties[0] == ';');
    o2_notify_others(service, true, NULL, properties, 0);
    if (o2_ctx->proc->key) {  // no notice until we have a name
        o2_send_cmd("!_o2/si", 0.0, "siss", service, o2_status(service),
                    o2_ctx->proc->key, properties ? properties + 1 : "");
    }
    return O2_SUCCESS;
}


// remove current attr:value from properties and if value != NULL,
// add new attr:value to the front of properties.
// 
static void service_property_add(Service_provider *spp, const char *service,
                                 const char *attr, const char *value)
{
    // instead of replacing, which requires more work to break the string
    // into components, we remove old attr, then insert new attr:value
    bool changed = service_property_free(spp, attr);
    char *p = spp->properties;
    if (value) { // we have a new attr:value
        // allocate space for new properties string
        // need attr, ':', escaped value, ';', existing string, eos
        size_t attr_len = strlen(attr);
        size_t value_len = value_encoded_len(value);
        const char *old_p = p;
        // we want the old properties to be a real string to avoid checking for NULL
        if (!old_p) old_p = ";";
        int len =  (int) (attr_len + value_len + strlen(old_p) + 3);
        if (len > O2_MAX_MSG_SIZE) {
            return; // property cannot grow too large,
        }           // even O2_MAX_MSG_SIZE is too big
        p = O2_MALLOCNT(len, char);
        p[0] = ';';
        strcpy(p + 1, attr);
        p[1 + attr_len] = ':'; // "1 +" is for leading ';'
        encode_value_to(p + 2 + attr_len, value);
        p[attr_len + value_len + 2] = ';'; // "+ 2" is for ';' and ':'
        // when copying spp->properties, skip the leading ';' because we
        // already inserted that. We could have appended the new property
        // instead or prepending and things would work out a little cleaner,
        // but since attr has just changed, maybe lookups of attr are more
        // likely and putting it first will make lookups faster
        strcpy(p + attr_len + value_len + 3, old_p + 1);
        changed = true;
        o2_set_service_properties(spp, service, p);  // replaces old_p
    } else if (changed) {
        p = spp->properties;      // tricky: set_service_properties will
        spp->properties = NULL;   // free old properties if they exist.
        o2_set_service_properties(spp, service, p);
    } // else asked to delete non-existent attribute, so nothing to do
}

O2err o2_service_provider_set_property(Service_provider *spp,
        const char *service, const char *attr, const char *value)
{
    service_property_add(spp, service, attr, value);
    return O2_SUCCESS;
}


O2err o2_service_set_property(const char *service, const char *attr,
                              const char *value)
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    // find service_provider struct matching service
    Service_provider *spp = Services_entry::find_local_entry(service);
    if (spp) {
        return o2_service_provider_set_property(spp, service, attr, value);
    }
    return O2_FAIL;
}


O2err o2_service_property_free(const char *service, const char *attr)
{
    return o2_service_set_property(service, attr, NULL);
}
