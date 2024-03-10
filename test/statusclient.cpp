//  statusclient.c - O2 status/discovery test, client side
//
//  see statusserver.c for details


#include "o2.h"
#include <stdio.h>
#include <stdlib.h>  // atoi
#include <string.h>
#include <assert.h>

#define POLL_PERIOD 100

bool running = true;


void stop_handler(O2msg_data_ptr data, const char *types,
                  O2arg_ptr *argv, int argc, const void *user_data)
{
    printf("client received stop message. Bye.\n");
    running = false;
}


int main(int argc, const char * argv[])
{
    const char *pip = NULL;
    const char *iip = NULL;
    int port = 0;
    printf("Usage: statusclient [debugflags] [pip iip port] "
           "(see o2.h for flags, use a for (almost) all)\n"
           "    last args, if set, specify a hub to use; if only pip\n"
           "    is given, o2_hub(NULL, NULL, 1) is called to turn off\n"
           "    broadcasting. If port is 0, you will be prompted\n"
           "    (allowing you to start statusserver first)\n");
    if (argc >= 2) {
        o2_debug_flags(argv[1]);
        printf("debug flags are: %s\n", argv[1]);
    }
    if (o2_initialize("test")) {
        printf("FAIL\n");
        return -1;
    }
    if (argc == 3) {
        port = 1;
    } else if (argc == 5) {
        pip = argv[2];
        iip = argv[3];
        port = atoi(argv[4]);
        while (port == 0) {
            char input[100];
            printf("Port specified as 0, enter new value: ");
            port = atoi((const char *) fgets(input, 100, stdin));
        }
        printf("Using %s:%s:%04x as hub.\n", pip, iip, port);
    } else if (argc > 5) {
        printf("WARNING: statusclient too many command line argments\n");
    }
    o2_service_new("client");
    o2_method_new("/client/stop", "", &stop_handler, NULL, false, true);

#ifndef O2_NO_HUB
    if (port > 0)
        o2_hub(pip, iip, port, port);
#endif
    const char *pipaddr;
    const char *iipaddr;
    int tcp_port;
    O2err err = o2_get_addresses(&pipaddr, &iipaddr, &tcp_port);
    assert(err == O2_SUCCESS);
    printf("My address is %s:%s:%04x\n", pipaddr, iipaddr, tcp_port);
    
    while (running) {
        o2_poll();
        o2_sleep(POLL_PERIOD);
    }
    // exit without calling o2_finish() -- this is a test for behavior when
    // the client crashes. Will the server still remove the service?
    // o2_finish();
    printf("CLIENT DONE\n");
    return 0;
}
