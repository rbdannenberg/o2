// bridgeapi.c -- simple test of bridge
//
// Roger B. Dannenberg
// July 2020

/* 
This test:
- install a new bridge with protocol "Demo"
- create a service named "bridge"
- view the service status
- send to the service
- receive and print messages arriving over the bridge
- send a timed message to the service
- receive message over the bridge and check timing
- change the bridge status to BRIDGE_SYNCED
- send an untimed message
- check receipt of the untimed message to BRIDGE_SYNCED bridge
- send a timed message to BRIDGE_SYNCED
- check receipt of the timed message to BRIDGE_SYNCED bridge
- close the bridge
*/

#ifdef __GNUC__
// define usleep:
#define _XOPEN_SOURCE 500
#define _POSIX_C_SOURCE 200112L
#endif

#include <stdio.h>
#include <assert.h>
#include "o2internal.h"
#include "services.h"
#include "bridge.h"

#ifdef WIN32
#include "usleep.h" // special windows implementation of sleep/usleep
#else
#include <unistd.h>
#endif

void *br_info = (void *) 12345;
bridge_inst_ptr br_inst;


o2_err_t br_poll(bridge_inst_ptr node)
{
    assert(node == NULL);
    return O2_SUCCESS;
}

int32_t message_int = -1;


o2_err_t br_send(bridge_inst_ptr node)
{
    assert(node->info == br_info);
    o2_message_ptr msg = o2_current_message();
    printf("br_send got a message: ");
    o2_message_print(msg);
    printf("\n");
    // extract the parameter
    assert(streql(O2_MSGDATA_TYPES(&msg->data), "i"));
    o2_extract_start(&msg->data);
    message_int = o2_get_next(O2_INT32)->i32;
    o2_complete_delivery();
    return O2_SUCCESS;
}

o2_err_t br_recv(bridge_inst_ptr node)
{
    assert(node->info == br_info);
    o2_message_ptr msg = o2_current_message();
    printf("br_recv got a message: ");
    o2_message_print(msg);
    printf("\n");
    o2_complete_delivery();
    return O2_SUCCESS;
}

o2_err_t br_inst_finish(bridge_inst_ptr node)
{
    printf("br_inst_finish called\n");
    assert(node->info == br_info);
    return O2_SUCCESS;
}


o2_err_t br_finish(bridge_inst_ptr node)
{
    printf("br_finish called\n");
    return O2_SUCCESS;
}
    

int main(int argc, const char * argv[])
{
    o2_err_t err = O2_SUCCESS;

    if (argc == 2) {
        o2_debug_flags(argv[1]);
    }
    if (argc > 2) {
        printf("WARNING: bridgeapi ignoring extra command line argments\n");
    }
        
    if (o2_initialize("test") != O2_SUCCESS) {
        printf("o2_initialize failed\n");
        exit(1);
    }
    // install a new bridge with protocol "Demo"
    bridge_protocol_ptr bridge = o2_bridge_new("Demo", &br_poll, &br_send,
                                    &br_recv, &br_inst_finish, &br_finish);
    br_inst = o2_bridge_inst_new(bridge, br_info);
    assert(bridge);

    // create a service named "bridge"
    char name[16];
    memset(name, 0, 16);
    strncpy(name, "bridge", 16);
    err = o2_service_provider_new(name, NULL, (o2_node_ptr) br_inst,
                                  o2_ctx->proc);
    assert(err == O2_SUCCESS);

    // view the service status
    int stat = o2_status("bridge");
    printf("Status of bridge is %s\n", o2_status_to_string((o2_status_t) stat));
    assert(stat == O2_BRIDGE_NOTIME);
    // send to the service
    o2_send("/bridge/test", 0, "i", 23); // no clock, no sync
    assert(message_int == 23);
    message_int = -1;

    // send a timed message to the service
    o2_clock_set(NULL, NULL);
    stat = o2_status("bridge");
    printf("Status of bridge is %s\n", o2_status_to_string((o2_status_t) stat));
    assert(stat == O2_BRIDGE_NOTIME);
    o2_send("/bridge/test", 0, "i", 34); // clock, no sync
    assert(message_int == 34);
    message_int = -1;
    
    // receive message over the bridge and check timing
    double now = o2_time_get();
    o2_send("/bridge/test", now + 0.2, "i", 45); // clock, future, no sync
    while (o2_time_get() < now + 0.4 && message_int != 45) {
        o2_poll();
        usleep(2000); // 2 ms
    }
    assert(message_int == 45);
    double delay = o2_time_get() - now;
    printf("expected delay = 0.2, actual delay = %g\n", delay);
    assert(delay > 0.19 && delay < 0.21);
    message_int = -1;

    // change the bridge status to BRIDGE_SYNCED
    br_inst->tag = BRIDGE_SYNCED;
    stat = o2_status("bridge");
    printf("Status of bridge is %s\n", o2_status_to_string((o2_status_t) stat));
    assert(stat == O2_BRIDGE);

    // send an untimed message
    o2_send("/bridge/test", 0, "i", 56); // clock, sync
    // check receipt of the untimed message to BRIDGE_SYNCED bridge
    assert(message_int == 56);
    message_int = -1;

    // send a timed message to BRIDGE_SYNCED
    now = o2_time_get();
    o2_send("/bridge/test", now + 0.2, "i", 67); // clock, future, sync

    // check receipt of the timed message to BRIDGE_SYNCED bridge
    while (o2_time_get() < now + 0.4 && message_int != 67) {
        o2_poll();
        usleep(2000); // 2 ms
    }
    assert(message_int == 67);
    delay = o2_time_get() - now;
    printf("expected delay for timed message = 0.0, actual delay = %g\n",
           delay);
    assert(delay >= 0.0 && delay < 0.01);
    message_int = -1;

    // close the bridge
    err = o2_bridge_remove("Demo");
    assert(err == O2_SUCCESS);
    
    printf("calling o2_finish()\n");
    o2_finish();
    printf("BRIDGEAPI\nDONE\n");
}


