//
//  o2_send.h
//  O2
//
//  Created by 弛张 on 2/4/16.
//  Copyright © 2016 弛张. All rights reserved.
//

#ifndef o2_send_h
#define o2_send_h

#include "o2_internal.h"
#include "o2_message.h"
#include "o2_discovery.h"

/**
 *  Use the name to find the pointer to the o2_service. This function uses
 *  hash table.
 *
 *  @param name The name of the service.
 *
 *  @return The pointer to the service.
 */
generic_entry_ptr o2_find_service(const char *name);


/**
 *  When we get the va_list, we should pass all the parameters to this function and
 *  use this function to send message.
 *  The function will create a new o2_message and add all the parameters into the
 *  o2_message, then call the o2_send_message to send the o2_message.
 *
 *  @param service_name The service's name you want to send the message to.
 *  @param time         The timestamp of the message.
 *  @param path         The path of the destination handler.
 *  @param typestring   The types of the parameters in string form.
 *  @param ap           The va_list.
 *  @param protocol     O2_UDP or O2_TCP.
 *
 *  @return O2_SUCCESS if succeed, O2_FAIL if not.
 */
int o2_send_internal(const char *service_name, o2_time time, char *path,
                     char *typestring, va_list ap, int protocol);



int send_data(const char *service_name, char *data, const size_t data_len, int protocol);

int o2_add_data(char *ptr, const char *typestring, va_list ap);

int check_buffer_size(int i);
void find_empty_buffer();

void send_osc(generic_entry_ptr service, o2_message_ptr msg);

int send_by_tcp_to_process(fds_info_ptr proc, o2_message_ptr msg);

#endif /* o2_send_h */
