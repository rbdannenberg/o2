// o2litemulticlient.c -- test for multiple clients and ensemble name check
//
// Roger B. Dannenberg
// July 2020

/* 
This o2lite process is meant to connect to o2litemultihost.cpp. The
expected protocol is:
- initialize o2lite
- wait for discovery
- wait for clock sync
- send a message to /checkin/hello with ID and a reply address.
- receive message at /client<N>/checkin_reply (<N> is our ID)
- send a message to /checkin/bye with ID
- exit

Alternatively, we can join the wrong ensemble and fail to connect
(controlled by a command line argument). Then we time out and exit,
but if we *do* connect, we continue with the protocol above.

The o2litemultihost will exit after the 2nd /checkin/bye message,
so if the bad ensemble name is not rejected, a 3rd o2litemulticlient
using the correct ensemble name should fail to find o2litemulticlient
and will therefore fail.
*/

/* does not define usleep when compiled with:

/usr/bin/cc -D_FORTIFY_SOURCE=0 -D_POSIX_C_SOURCE=201112L -I/home/rbd/o2/src  -std=c11 -mcx16 -g   -o CMakeFiles/o2liteserv.dir/test/o2liteserv.c.o   -c /home/rbd/o2/test/o2liteserv.c

*/

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "o2lite.h"
#include <string.h>

bool running = true;
bool use_tcp = true;
bool wrong_name = false;

// handler for incoming /client<N>/checkin_reply message:
//
void checkin_reply(o2l_msg_ptr msg, const char *types, void *data, void *info)
{
    int id = o2l_get_int32();
    if (id != o2l_bridge_id) {
        printf("FAILURE: o2litemultclient checkin_reply got id %d, "
               "expected %d\n", id, o2l_bridge_id);
        exit(1);
    }
    printf("checkin_reply called\n");
    o2l_send_start("/checkin/goodbye", 0, "i", use_tcp);
    o2l_add_int32(o2l_bridge_id);
    o2l_send();
    running = false;
}


void time_check()
{
    if (o2l_local_time() > 15) {
        if (wrong_name) {
            printf("o2litemulticlient with wrong ensemble name timed out\n");
            printf("CLIENT DONE\n");
            exit(0);
        } else {
            printf("o2litemulticlient timeout FAILURE exiting now\n");
            exit(1);
        }
    }
}


int main(int argc, const char * argv[])
{
    char service_name[32];
    char reply_address[128];
    
    printf("Usage: o2litemulticlient [-v] [-w] [udp]\n"
           "    -v enable verbose debug output "
           "(in o2lite debug versions only)\n"
           "    -w initializes with the wrong ensemble name\n"
           "    specify udp to use udp. Anything else or nothing gives tcp\n");
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "-w") == 0) {
            wrong_name = true;
        } else if (strcmp(argv[i], "udp") == 0) {
            use_tcp = false;
        } else {
            printf("Unknown option: %s\n", argv[i]);
        }
    }

    if (o2l_initialize(wrong_name ? "badname" : "test") != O2L_SUCCESS) {
        printf("o2liteserv\nFAILURE\n");
        exit(1);
    }

    while (o2l_bridge_id < 0 || o2l_time_get() < 0) { // not connected, sync'd
        time_check();
        o2l_poll();
        o2_sleep(2); // 2ms
    }
    printf("main detected o2lite connected\n");

    snprintf(service_name, 32, "client%d", o2l_bridge_id);
    snprintf(reply_address, 128, "/%s/checkin_reply", service_name);
    o2l_set_services(service_name);

    o2l_method_new(reply_address, "i", true, &checkin_reply, NULL);

    // The next send, if UDP, could arrive before the service is
    // known, which would cause the host to drop the reply message to
    // reply_address. We could send our own message to reply_address
    // via TCP. This would make a round-trip to the host and when
    // received, we would know that our service is known to the host.
    // Instead, we just delay for a minimum of 100 msec:
    for (int i = 0; i < 50; i++) {
        o2l_poll();
        o2_sleep(2);
    }

    o2l_send_start("/checkin/hello", 0, "is", use_tcp);
    o2l_add_int32(o2l_bridge_id);
    o2l_add_string(service_name);
    o2l_send();

    printf("main detected o2lite clock sync\n");

    o2l_time start_wait = o2l_time_get();
    while (running) {
        time_check();
        o2l_poll();
        o2_sleep(2);
    }
    // run for another 1/2 second to make sure all messages delivered
    for (int i = 0; i < 250; i++) {
        o2l_poll();
        o2_sleep(2);
    }

    printf("o2litemulticlient\nCLIENT DONE\n");
}
