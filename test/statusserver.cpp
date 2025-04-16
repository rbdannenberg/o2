// statusserver.c - O2 status/discovery test, server side
//
// This program works with statusclient.c. It checks for 
// discovery of statusclient's service, sends it a message
// to exit, then checks that the status of the service
// reverts to "does not exist".

#undef NDEBUG
#include "stdio.h"
#include "string.h"
#include "assert.h"
#include "o2internal.h"

#define POLL_PERIOD 100

bool running = true;

O2message_ptr make_message()
{
    assert(o2_send_start() == O2_SUCCESS);
    return o2_message_finish(0.0, "/This_is_from_make_message.", true);
}


int main(int argc, const char * argv[])
{
    const char *pip = NULL;
    const char *iip = NULL;
    int port = 0;
    printf("Usage: statusserver [debugflags] [pip iip port] "
           "    See o2.h for debugflags, use a for (almost) all.\n"
           "    last args, if set, specify a hub to use as public ip,\n"
           "    internal ip and port number. If only a pip argument\n"
           "    appears (anything), o2_hub(NULL, NULL, 1) is called to\n"
           "    turn off broadcasting\n");
    if (argc >= 2) {
        o2_debug_flags(argv[1]);
        printf("debug flags are: %s\n", argv[1]);
    }
    if (argc == 3) {
        port = 1;
    } else if (argc == 5) {
        pip = argv[2];
        iip = argv[3];
        port = atoi(argv[4]);
        printf("Using %s:%s:%04x as hub.\n", pip, iip, port);
    } else if (argc > 5) {
        printf("WARNING: statusserver ignoring extra command line argments\n");
    }
    if (o2_initialize("test")) {
        printf("FAIL\n");
        return -1;
    }

    // we are the master clock
    o2_clock_set(NULL, NULL);

#ifndef O2_NO_HUB
    if (port > 0)
        o2_hub(pip, iip, port, port);
#endif
    const char *my_pip;
    const char *my_iip;
    int tcp_port;
    O2err err = o2_get_addresses(&my_pip, &my_iip, &tcp_port);
    assert(err == O2_SUCCESS);
    printf("Before stun: address is %s:%s:%04x\n", my_pip, my_iip, tcp_port);

    // wait for client service to be discovered
    while (o2_status("client") < O2_REMOTE_NOTIME) {
        o2_poll();
        o2_sleep(POLL_PERIOD);
    }
    printf("My address is %s:%s:%04x\n", my_pip, my_iip, tcp_port);
    printf("We discovered the client at time %g.\n", o2_time_get());

    // wait for client service to be discovered
    while (o2_status("client") < O2_REMOTE) {
        o2_poll();
        o2_sleep(POLL_PERIOD); // 2ms
    }
    
    printf("We got clock sync at time %g.\n", o2_time_get());
    
    // delay 1 second
    double now = o2_time_get();
    while (o2_time_get() < now + 1) {
        o2_poll();
        o2_sleep(POLL_PERIOD);  // 2ms
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
#ifndef O2_NO_DEBUG
        o2_get_context()->show_tree();
#endif
    }
    o2_finish();
    o2_sleep(1000); // clean up sockets
    return 0;
}
