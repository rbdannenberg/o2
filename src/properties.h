/* properties.h -- interface to properties.c functions */

// the only function without a public interface in o2.h is this:
void  o2_services_list_finish(void);
// needed by o2.c to clean up before exit
