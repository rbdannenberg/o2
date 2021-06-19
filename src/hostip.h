// room for IP address in dot notation and terminating EOS
#ifdef __cplusplus
extern "C" {
#endif

#define O2N_IP_LEN 16

extern bool o2n_network_enabled;
extern bool o2n_network_found;
extern char o2n_internal_ip[O2N_IP_LEN];

void o2n_get_internal_ip(void);
void o2_hex_to_dot(const char *hex, char *dot);
int o2_hex_to_byte(const char *hex);


#ifdef __cplusplus
}
#endif
