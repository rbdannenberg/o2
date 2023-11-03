// websockhost.cpp - o2 process based on o2client.c that talks to web page
//
// Roger B. Dannenberg
// Feb 2021
//
// see o2server.c for details of the client-server protocol
// run this program and open the URL http://wstest.local in a browser.

#include "o2.h"
#if WIN32
#include "Direct.h"
#define getcwd _getcwd
#define MAXPATHLEN 4096
// note: this number is arbitrary and program may fail if it is too small
// Linux apparently has the same problem: it defines MAXPATHLEN, but it
// allows paths to be even longer. This code should check return values.
#else
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

bool running = true;
bool one_minute_max = false;

void stop_handler(O2msg_data_ptr data, const char *types,
                  O2arg_ptr *argv, int argc, const void *user_data)
{
    printf("websockhost received stop message. Shutting down.\n");
    running = false;
}

int main(int argc, const char *argv[])
{
    const char *ens_name = "test";
    int port = 8080;
    const char *path = "www";
    printf("Usage: websockhost [debugflags] ensemble_name port path\n"
           "    see o2.h for flags, use a for all, - for none\n"
           "    Extra flag '@' means exit after 60 seconds\n"
           "    ensemble_name defaults to test\n"
           "    port defaults to 8080\n"
           "    path (for static web pages and files) defaults to ./www\n");
    if (argc >= 2) {
        o2_debug_flags(argv[1]);
        printf("debug flags are: %s\n", argv[1]);
        if (strchr(argv[1], '@')) {
            one_minute_max = true;
        }
    }
    if (argc >= 3) {
        ens_name = argv[2];
        printf("O2 ensemble name: %s\n", ens_name);
    }
    if (argc >= 4) {
        port = atoi(argv[3]);
        printf("websockhost HTTP port: %d\n", port);
    }
    if (argc >= 5) {
        path = argv[4];
        char* cwd = getcwd(NULL, MAXPATHLEN);
        printf("HTTP service root: %s/%s\n",  cwd, path);
        free(cwd);
    }
    if (argc > 5) {
        printf("WARNING: websockhost ignoring extra command line argments\n");
    }
#ifdef O2_NO_WEBSOCKETS
    printf("O2_NO_WEBSOCKETS defined, so this program does nothing.\n");
    printf("WEBSOCKETHOST DONE\n");
#else
    char* cwd = getcwd(NULL, MAXPATHLEN);
    printf("Server port %d, ensemble \"%s\", path \"%s/%s\"\n",
           port, ens_name, cwd, path);
    free(cwd);
    o2_initialize(ens_name);

    // enable websockets
    O2err rslt = o2_http_initialize(port, path); 
    assert(rslt == O2_SUCCESS);
    
    o2_service_new("websockhost");
    o2_method_new("/websockhost/stop", "", &stop_handler, NULL, false, true);

    o2_clock_set(NULL, NULL); // become the master clock
    while (running) {
        if (one_minute_max && o2_local_time() > 60) {
            printf("timed out after 1 minute");
            break;
        }
        o2_poll();
        o2_sleep(2); // 2ms (you could delete this line for benchmarking)
    }
    // run a bit more to close websockets
    for (int i = 0; i < 100; i++) {
        o2_poll();
        o2_sleep(1);
    }
    printf("Calling o2_finish()\n");
    o2_finish();
#endif
    if (one_minute_max && o2_local_time() > 60) {
        printf("WEBSOCKETHOST TIMED OUT\n");
    } else {
        printf("WEBSOCKETHOST DONE\n");
    }
    return 0;
}
