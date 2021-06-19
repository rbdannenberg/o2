// websockhost.cpp - o2 process based on o2client.c that talks to web page
//
// Roger B. Dannenberg
// Feb 2021
//
// see o2server.c for details of the client-server protocol
// run this program and open the URL http://wstest.local in a browser.

#include "o2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

bool running = true;

void stop_handler(o2_msg_data_ptr data, const char *types,
                  O2arg_ptr *argv, int argc, const void *user_data)
{
    printf("websockhost received stop message. Shutting down.\n");
    running = false;
}

int main(int argc, const char *argv[])
{
    printf("Usage: websockhost [debugflags]\n"
           "    see o2.h for flags, use a for all, - for none\n");
    if (argc >= 2) {
        o2_debug_flags(argv[1]);
        printf("debug flags are: %s\n", argv[1]);
    }
    if (argc > 2) {
        printf("WARNING: websockhost ignoring extra command line argments\n");
    }
#ifdef O2_NO_WEBSOCKETS
    printf("O2_NO_WEBSOCKETS defined, so this program does no testing.\n");
    printf("CLIENT DONE\n");
#else
    o2_initialize("test");

    // enable websockets
    O2err rslt = http_initialize(8080, "www"); 
    assert(rslt == O2_SUCCESS);
    
    o2_service_new("websockhost");
    o2_method_new("/websockhost/stop", "", &stop_handler, NULL, false, true);

    o2_clock_set(NULL, NULL); // become the master clock
    while (running) {
        o2_poll();
        o2_sleep(2); // 2ms (you could delete this line for benchmarking)
    }
    o2_finish();
#endif
    printf("WEBSOCKETHOST DONE\n");
    return 0;
}
