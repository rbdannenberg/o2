/* properties.h -- interface to properties.c functions */

// the only function without a public interface in o2.h is this:
void  o2_services_list_finish(void);
// needed by o2.c to clean up before exit

// change the property value for spp, the local provider of service
O2err o2_service_provider_set_property(Service_provider *spp,
        const char *service, const char *attr, const char *value);

// set properties of this service to a new value
O2err o2_set_service_properties(Service_provider *spp, const char *service,
                               char *properties);
