//
//  o2_interoperation.c
//  o2
//
//  Created by ĺźĺź  on 3/31/16.
//
/* Design notes:
 *    We handle incoming OSC ports using 
 * o2_create_osc_port(service_name, port_num), which puts an entry in
 * the fds_info table that says incoming OSC messages are handled by
 * the service_name (which may or may not be local). Thus, when an OSC
 * message arrives, we use the incoming data to construct a full O2
 * message. We can use the OSC message length plus the service name
 * length plus a timestamp length (plus some padding) to determine
 * how much message space to allocate. Then, we can receive the data
 * with some offset allowing us to prepend the timestamp and 
 * service name. Finally, we just send the message, resulting in
 * either a local dispatch or forwarding to an O2 service.
 *
 *   We handle outgoing OSC messages using
 * o2_delegate_to_osc(service_name, ip, port_num), which puts an
 * entry in the top-level hash table with the OSC socket.
 */


#include "o2.h"
#include "o2_dynamic.h"
#include "o2_socket.h"
#include "o2_search.h"
#include "o2_internal.h"
#include "o2_message.h"
#include "o2_sched.h"
#include "o2_send.h"

/* create a port to receive OSC messages. 
 * Messages are directed to service_name. 
 *
 * Algorithm: Create 
 */
int o2_create_osc_port(char *service_name, int port_num, int udp_flag)
{
    osc_entry_ptr osc_entry = O2_MALLOC(sizeof(osc_entry));
    osc_entry->tag = OSC_LOCAL_SERVICE;
    osc_entry->key = o2_heapify(service_name);
    osc_entry->port = port_num;
    if (udp_flag) {
        // TODO: set osc_entry->udp_sa for sending messages
    } else {
        assert(FALSE); // need to implement TCP
        DA_EXPAND(o2_fds, struct pollfd);
        struct pollfd *fdptr = DA_LAST(o2_fds, struct pollfd);
        /* TODO: 
        fdptr->fd = sock;
        fdptr->events = POLLIN;
        if (o2_bind(sock, port_num) == -1) {
            perror("Bind udp socket");
            return O2_FAIL;
        }
        */
        DA_EXPAND(o2_fds_info, struct fds_info);
        struct fds_info *info = DA_LAST(o2_fds_info, struct fds_info);
        info->tag = OSC_SOCKET;
        // info->port = port_num;
        info->length = 0;
        info->length_got = 0;
        info->u.osc_service_name = o2_heapify(service_name);
    }
    // TODO: what happens to o2_osc_service that we created above?
    // TODO: What does this do?
    //     add_local_osc_to_hash(path, port_num, o2_osc_service_num);
    // TODO: this implies there's an o2_socket table; probably wrong:
    //     o2_socket_num++;
    
    return O2_SUCCESS;
}


size_t o2_arg_size(o2_type type, void *data);

/** send an OSC message directly. The service_name is the O2 equivalent
 * of an address. path is a normal OSC address string and is not prefixed
 * with an O2 service name.
 */
int o2_send_osc_message_marker(char *service_name, const char *path,
                               const char *typestring, ...)
{
    va_list ap;
    va_start(ap, typestring);
    
    o2_message_ptr msg = o2_build_message(0.0, service_name, path, typestring, ap);
    // TODO: send the message

    return O2_SUCCESS;
}

int o2_delegate_to_osc(char *service_name, char *ip, int port_num, int tcp_flag)
{
    // make a description for fds_info
    osc_entry_ptr entry = O2_MALLOC(sizeof(osc_entry));
    
    entry->tag = OSC_REMOTE_SERVICE;
    char *key = o2_heapify(service_name);
    entry->key = key;
    // TODO: set up upb_sa
    strncpy(entry->ip, 20, ip);
    entry->port = port_num;
    
    // put the entry in the master table
    // add_entry(&path_tree_table, entry);
    return O2_SUCCESS;
}

