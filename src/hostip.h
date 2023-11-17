// room for IP address in dot notation and terminating EOS
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#define O2N_IP_LEN 16

extern bool o2n_internet_enabled;
extern bool o2n_network_enabled;
extern bool o2n_network_found;
extern char o2n_internal_ip[O2N_IP_LEN];

#ifndef O2_EXPORT
// must be in o2lite or some other static library client
#define O2_EXPORT extern
#endif

O2_EXPORT void o2n_get_internal_ip(char *internal_ip);
O2_EXPORT void o2_hex_to_dot(const char *hex, char *dot);
O2_EXPORT int o2_hex_to_byte(const char *hex);


#ifdef __cplusplus
}
#endif
