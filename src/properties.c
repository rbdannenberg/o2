/* properties.c - services and their properties for each process */

/* Roger B. Dannenberg
 * April 2020
 */

#include "o2internal.h"
#include "services.h"
#include "message.h"

typedef struct service_info {
    o2string name;
    o2_status_t service_type;
    o2string process; // the port:ip of process offering the service
    o2string properties; // service properties or the tapper of tappee
} service_info, *service_info_ptr;


static dyn_array service_list = {0, 0, NULL};


// add every active service to service_list. Get services from list of
// processes.
int o2_services_list()
{
    if (!o2_ensemble_name) {
        return O2_NOT_INITIALIZED;
    }
    o2_services_list_free();
    enumerate enumerator;
    o2_enumerate_begin(&enumerator, &o2_ctx->path_tree.children);
    o2_node_ptr entry;
    while ((entry = o2_enumerate_next(&enumerator))) {
        services_entry_ptr services = TO_SERVICES_ENTRY(entry);
        if (services->services.length > 0) {
            service_provider_ptr spp =
                    GET_SERVICE_PROVIDER(services->services, 0);
            service_info_ptr sip = DA_EXPAND(service_list, service_info);
            sip->name = o2_heapify(entry->key);
            sip->process = o2_heapify(o2_node_to_ipport(spp->service));
            sip->service_type = (ISA_PROC(spp->service) ? O2_REMOTE : O2_LOCAL);
            sip->properties = spp->properties;
            if (sip->properties) { // need to own string if any
                sip->properties = o2_heapify(sip->properties);
            }
        }
        service_tap_ptr stp;
        for (int i = 0; i < services->taps.length; i++) {
            stp = GET_TAP_PTR(services->taps, i);
            service_info_ptr sip = DA_EXPAND(service_list, service_info);
            sip->name = o2_heapify(entry->key);
            sip->process = o2_heapify(stp->proc->name);
            sip->service_type = O2_TAP;
            sip->properties = o2_heapify(stp->tapper);

        }
    }
    return O2_SUCCESS;
}

#define SERVICE_INFO(sl, i) DA_GET_ADDR(service_list, service_info, i)

int o2_services_list_free()
{
    for (int i = 0; i < service_list.length; i++) {
        service_info_ptr sip = SERVICE_INFO(service_list, i);
        O2_FREE(sip->name);
        O2_FREE(sip->process);
        if (sip->properties) O2_FREE(sip->properties);
    }
    service_list.length = 0;
    return O2_SUCCESS;
}


// internal function to release all services_list memory
void o2_services_list_finish()
{
    o2_services_list_free();
    DA_FINISH(service_list);
}


const char *o2_service_name(int i)
{
    if (i >= 0 && i < service_list.length) {
        service_info_ptr sip = SERVICE_INFO(service_list, i);
        return sip->name;
    }
    return NULL;
}


int o2_service_type(int i)
{
    if (i >= 0 && i < service_list.length) {
        service_info_ptr sip = SERVICE_INFO(service_list, i);
        return sip->service_type;
    }
    return O2_FAIL;
}


const char *o2_service_process(int i)
{
    if (i >= 0 && i < service_list.length) {
        service_info_ptr sip = SERVICE_INFO(service_list, i);
        return sip->process;
    }
    return NULL;
}


const char *o2_service_tapper(int i)
{
    if (i >= 0 && i < service_list.length) {
        service_info_ptr sip = SERVICE_INFO(service_list, i);
        if (sip->service_type != O2_TAP) {
            return NULL; // there is no tapper, it's a service
        }
        return sip->properties;
    }
    return NULL;
}


const char *o2_service_properties(int i)
{
    if (i >= 0 && i < service_list.length) {
        service_info_ptr sip = SERVICE_INFO(service_list, i);
        if (sip->service_type == O2_TAP) {
            return NULL; // it's a tap
        } // otherwise it's a service and properties is good
        return (sip->properties ? sip->properties + 1 : // skip initial ";"
                ";" + 1); // generate an initial ";" for o2_service_search()
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
    while (i >= 0 && i < service_list.length) {
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
static bool service_property_free(service_provider_ptr spp,
                                  const char *attr)
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


// add property at front of old properties; assume old properties does
// not contain attr
// 
static void service_property_add(service_provider_ptr spp, const char *attr,
                                 const char *value)
{
    // allocate space for new properties string
    // need attr, ':', escaped value, ';', existing string, eos
    size_t attr_len = strlen(attr);
    size_t value_len = value_encoded_len(value);
    const char *old_p = spp->properties;
    // we want the old properties to be a real string to avoid checking for NULL
    if (!old_p) old_p = ";";
    int len =  (int) (attr_len + value_len + strlen(old_p) + 3);
    if (len > O2_MAX_MSG_SIZE) {
        return; // property cannot grow too large,
    }           // even O2_MAX_MSG_SIZE is too big
    char *p = O2_MALLOCNT(len, char);
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
    if (spp->properties) O2_FREE(spp->properties);
    spp->properties = p;
}


int o2_service_set_property(const char *service, const char *attr,
                            const char *value)
{
    // find service_provider struct matching service
    services_entry_ptr services = *o2_services_find(service);
    if (!services) {
        return O2_FAIL;
    }
    // need to find locally provided service in service list
    for (int i = 0; i < services->services.length; i++) {
        service_provider_ptr spp = GET_SERVICE_PROVIDER(services->services, i);
        if (!ISA_PROC(spp->service)) {
            service_property_free(spp, attr);
            // this test allows us to free attr by passing in value == NULL:
            if (value) service_property_add(spp, attr, value);
            o2_notify_others(service, true, NULL, spp->properties);
            o2_send_cmd("!_o2/si", 0.0, "siss", service, O2_FAIL,
                        o2_ctx->proc->name,
                        spp->properties ? spp->properties + 1 : "");
            return O2_SUCCESS;
        }
    }
    return O2_FAIL; // failed to find local service
}


int o2_service_property_free(const char *service, const char *attr)
{
    return o2_service_set_property(service, attr, NULL);
}
