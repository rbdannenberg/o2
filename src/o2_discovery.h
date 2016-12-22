//
//  o2_discovery.h
//  O2
//
//  Created by 弛张 on 1/26/16.
//  Copyright © 2016 弛张. All rights reserved.
//

#ifndef o2_discovery_h
#define o2_discovery_h

#define PORT_MAX  5

extern SOCKET o2_discovery_socket;
extern int o2_port_map[16];

/**
 *  Initialize for discovery 
 *
 *  @return O2_SUCCESS (0) if succeed, O2_FAIL (-1) if not.
 */
int o2_discovery_init();
int o2_discovery_msg_init();


/**
 *  Listen for  messages comning from all IPs
 *
 *  @param addr   socket address
 *  @param port_s the port number to use
 *  @param sock   pointer to the socket
 *
 *  @return 0 if succeed, -1 if not
 */
int o2_bind(struct o2_socket *s, int port_s);

/**
 *  Send broadcast messages to discover other devices.
 *
 *  @param port_s  the port number
 *  @param sock    the socket in use
 *  @param message data of the message
 */
void o2_broadcast_message(int port_s, SOCKET sock, o2_message_ptr message);

/**
 *  Add a remote service in the remote services list.
 *
 *  @param m The discover message
 *
 *  @return 0 if succeed, -1 if not
 */
int o2_add_remote_services(char *m);

/**
 *  Discover function will send the discover messages and deal with all the discover
 *  messages sent to the discover socket. Record all the information in the
 *  remote_process arrays and periodly check for update. If there exists a new
 *  remote_process, the o2_discover() will automatically set up a new tcp
 *  connection with the remote_process.
 *
 *  @return 0 if succeed, 1 if there is some error.
 */
int o2_discovery_send_handler(o2_message_ptr msg, const char *types,
                o2_arg_ptr *argv, int argc, void *user_data);


// callback for discovery messages
int o2_discovery_handler(o2_message_ptr msg, const char *types,
                         o2_arg_ptr *argv, int argc, void *user_data);

int o2_discovery_init_handler(o2_message_ptr msg, const char *types,
                              o2_arg_ptr *argv, int argc, void *user_data);

int o2_services_handler(o2_message_ptr msg, const char *types,
                        o2_arg_ptr *argv, int argc, void *user_data);

int make_tcp_connection(char *ip, int tcp_port,
                        o2_socket_handler handler, fds_info_ptr *info);

int o2_send_init(fds_info_ptr process);
int o2_send_services(fds_info_ptr process);


#endif /* O2_discovery_h */
