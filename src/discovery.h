/* discovery.h -- discovery protocol
 *
 * Roger B. Dannenberg
 * April 2020
 */

#ifndef DISCOVERY_H
#define DISCOVERY_H

#define O2_DY_INFO 50
#define O2_DY_HUB 51
#define O2_DY_REPLY 52
#define O2_DY_CALLBACK 53
#define O2_DY_CONNECT 54

// we need to successfully allocate one port from the list. This number is
// how many ports to search.
#define PORT_MAX  16

extern o2_message_ptr o2_discovery_msg;

extern SOCKET o2_discovery_socket;
extern o2n_info_ptr o2_discovery_udp_server;

#ifndef O2_NO_HUB
extern char o2_hub_addr[O2_MAX_PROCNAME_LEN]; // @public:internal:port of hub
        // if any, otherwise empty string. Non-empty turns off broadcasting.
#endif

/**
 *  Initialize for discovery 
 *
 *  @return O2_SUCCESS (0) if succeed, O2_FAIL (-1) if not.
 */
o2_err_t o2_discovery_initialize(void);
void o2_discovery_init_phase2();

o2_err_t o2_discovery_finish(void);

/**
 *  Discover function will send the discover messages and deal with all the discover
 *  messages sent to the discover socket. Record all the information in the
 *  remote_process arrays and periodly check for update. If there exists a new
 *  remote_process, the o2_discover() will automatically set up a new tcp
 *  connection with the remote_process.
 *
 */
void o2_discovery_send_handler(o2_msg_data_ptr msg, const char *types,
                    o2_arg_ptr *argv, int argc, const void *user_data);

void o2_send_discovery_at(o2_time when);

o2_err_t o2_send_services(proc_info_ptr proc);

void o2_discovery_handler(o2_msg_data_ptr msg, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data);

void o2_discovery_init_handler(o2_msg_data_ptr msg, const char *types,
                    o2_arg_ptr *argv, int argc, const void *user_data);


void o2_hub_handler(o2_msg_data_ptr msg, const char *types,
         o2_arg_ptr *argv, int argc, const void *user_data);

void o2_services_handler(o2_msg_data_ptr msg, const char *types,
              o2_arg_ptr *argv, int argc, const void *user_data);

o2_err_t o2_discovered_a_remote_process(const char *public_ip,
                    const char *internal_ip, int port, int dy);

o2_message_ptr o2_make_dy_msg(proc_info_ptr proc, int tcp_flag,
                              int dy_flag);

#endif /* DISCOVERY_H */
