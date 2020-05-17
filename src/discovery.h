/* discovery.h -- discovery protocol
 *
 * Roger B. Dannenberg
 * April 2020
 */

#ifndef DISCOVERY_H
#define DISCOVERY_H

// we need to successfully allocate one port from the list. This number is
// how many ports to search.
#define PORT_MAX  16

extern o2_message_ptr o2_discovery_msg;

extern SOCKET o2_discovery_socket;
extern int o2_port_map[16];

extern char o2_hub_addr[32]; // ip:port of hub if any, otherwise empty string
                        // non-empty turns off broadcasting


/**
 *  Initialize for discovery 
 *
 *  @return O2_SUCCESS (0) if succeed, O2_FAIL (-1) if not.
 */
int o2_discovery_initialize(void);

int o2_discovery_finish(void);

/**
 *  Discover function will send the discover messages and deal with all the discover
 *  messages sent to the discover socket. Record all the information in the
 *  remote_process arrays and periodly check for update. If there exists a new
 *  remote_process, the o2_discover() will automatically set up a new tcp
 *  connection with the remote_process.
 *
 */
void o2_discovery_send_handler(o2_msg_data_ptr msg, const char *types,
                               o2_arg_ptr *argv, int argc, void *user_data);

void o2_send_discovery_at(o2_time when);

// int o2_send_initialize(o2n_info_ptr process, int32_t hub_flag);

int o2_send_services(proc_info_ptr proc);

void o2_discovery_handler(o2_msg_data_ptr msg, const char *types,
                          o2_arg_ptr *argv, int argc, void *user_data);

void o2_discovery_init_handler(o2_msg_data_ptr msg, const char *types,
                               o2_arg_ptr *argv, int argc, void *user_data);


void o2_hub_handler(o2_msg_data_ptr msg, const char *types,
                    o2_arg_ptr *argv, int argc, void *user_data);

void o2_services_handler(o2_msg_data_ptr msg, const char *types,
                         o2_arg_ptr *argv, int argc, void *user_data);

int o2_make_tcp_connection(const char *ip, int tcp_port, o2n_info_ptr *info, int hub_flag);

int o2_discovery_by_tcp(const char *ipaddress, int port, char *name,
                        int be_server, int32_t hub_flag);

int o2_discovered_a_remote_process(const char *ip, int tcp, int udp, int dy);


#endif /* DISCOVERY_H */
