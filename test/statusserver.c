// statusserver.c - O2 status/discovery test, server side
//
// This program works with statusclient.c. It checks for 
// discovery of statusclient's service, sends it a message
// to exit, then checks that the status of the service
// reverts to "does not exist".

#include "o2usleep.h"
#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"
#include "o2internal.h"

bool running = true;

o2_message_ptr make_message()
{
    assert(o2_send_start() == O2_SUCCESS);
    return o2_message_finish(0.0, "/This_is_from_make_message.", true);
}


int main(int argc, const char * argv[])
{
    const char *ip = NULL;
    int port = 0;
    printf("Usage: statusserver [debugflags] [ip port] "
           "(see o2.h for flags, use a for all)\n"
           "    last args, if set, specify a hub to use; if only ip is given,\n"
           "    o2_hub(NULL, 0) is called to turn off broadcasting\n");
    if (argc >= 2) {
        o2_debug_flags(argv[1]);
        printf("debug flags are: %s\n", argv[1]);
    }
    if (argc == 3) {
        port = 1;
    } else if (argc == 4) {
        ip = argv[2];
        port = atoi(argv[3]);
        printf("Using %s:%d as hub.\n", ip, port);
    } else if (argc > 4) {
        printf("WARNING: statusserver ignoring extra command line argments\n");
    }
    if (o2_initialize("test")) {
        printf("FAIL\n");
        return -1;
    }

    // we are the master clock
    o2_clock_set(NULL, NULL);
    
    if (port > 0)
        o2_hub(ip, port);

    const char *address;
    int tcp_port;
    o2_get_address(&address, &tcp_port);
    printf("My address is %s:%d\n", address, tcp_port);

    // wait for client service to be discovered
    while (o2_status("client") < O2_REMOTE_NOTIME) {
        o2_poll();
        usleep(2000); // 2ms
    }
    
    printf("We discovered the client at time %g.\n", o2_time_get());

    // wait for client service to be discovered
    while (o2_status("client") < O2_REMOTE) {
        o2_poll();
        usleep(2000); // 2ms
    }
    
    printf("We got clock sync at time %g.\n", o2_time_get());
    
    // delay 1 second
    double now = o2_time_get();
    while (o2_time_get() < now + 1) {
        o2_poll();
        usleep(2000);
    }
    
    double start_time = o2_time_get();
    printf("Here we go! ...\ntime is %g.\n", start_time);
    o2_send_cmd("!client/stop", 0.0, "");
    // allow 3s for client to shut down and detect it
    while (running && o2_time_get() < start_time + 3 &&
           o2_status("client") >= 0) {
        o2_poll();
    }
    if (o2_status("client") < 0) {
        printf("SERVER DONE\n");
    } else {
        printf("FAIL: client service status is %d\n", o2_status("client"));
        o2_node_show((o2_node_ptr) &o2_ctx->path_tree, 2);
    }
    o2_finish();
    sleep(1); // clean up sockets
    return 0;
}
