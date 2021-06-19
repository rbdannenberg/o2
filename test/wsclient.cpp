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

int max_msg_count = 500;

char **server_addresses;
int n_addrs = 20;
int use_tcp = false;

int msg_count = 0;
bool running = true;

void client_test(o2_msg_data_ptr data, const char *types,
                 O2arg_ptr *argv, int argc, const void *user_data)
{
    msg_count++;
    // the value we send is arbitrary, but we've already sent
    // 1 message with value 1, so the 2nd message will have 2, etc...
    int32_t i = msg_count + 1;

    // server will shut down when it gets data == -1
    if (msg_count >= max_msg_count) {
        i = -1;
        running = false;
    }
    if (use_tcp) o2_send_cmd(server_addresses[msg_count % n_addrs], 0, "i", i);
    else o2_send(server_addresses[msg_count % n_addrs], 0, "i", i);
    if (msg_count % 10000 == 0) {
        printf("client received %d messages\n", msg_count);
    }
    if (msg_count < 100) {
        printf("client message %d is %d\n", msg_count, argv[0]->i32);
    }
    assert(msg_count == argv[0]->i32);
}


void time_check()
{
    return; // DEBUGGING - DO NOT QUIT TOO EARLY! 
    // remove the previous line normally so unit tests complete faster
    if (o2_local_time() > 60) {
        o2_finish();
        printf("websockhost timeout FAILURE exiting now\n");
        exit(1);
    }
}


int main(int argc, const char *argv[])
{
    printf("Usage: websockhost [maxmsgs] [debugflags]\n"
           "    see o2.h for flags, use a for all, - for none\n"
           "    default maxmsgs is 500\n"
           "    end maxmsgs with t, e.g. 10000t, to test with TCP\n");
    if (argc >= 2) {
        max_msg_count = atoi(argv[1]);
        printf("max_msg_count set to %d\n", max_msg_count);
        if (strchr(argv[1], 't' )) {
            use_tcp = true;
            printf("Using TCP\n");
        }
    }
    if (argc >= 3) {
        o2_debug_flags(argv[2]);
        printf("debug flags are: %s\n", argv[2]);
    }
    if (argc > 3) {
        printf("WARNING: websockhost ignoring extra command line argments\n");
    }
#ifdef O2_NO_WEBSOCKETS
    printf("O2_NO_WEBSOCKETS defined, so this program does no testing.\n");
    printf("CLIENT DONE\n");
#else
    o2_initialize("test");

    // enable websockets
    O2err rslt = http_initialize("wstest", 8080, "www"); 
    assert(rslt == O2_SUCCESS);

    o2_clock_set(NULL, NULL); // become the master clock
    o2_service_new("client");
    
    for (int i = 0; i < n_addrs; i++) {
        char path[100];
        sprintf(path, "/client/benchmark/%d", i);
        o2_method_new(path, "i", &client_test, NULL, false, true);
    }
    
    server_addresses = O2_MALLOCNT(n_addrs, char *);
    for (int i = 0; i < n_addrs; i++) {
        char path[100];
        sprintf(path, "!server/benchmark/%d", i);
        server_addresses[i] = O2_MALLOCNT(strlen(path), char);
        strcpy(server_addresses[i], path);
    }

    while (o2_status("server") < O2_BRIDGE_NOTIME) {
        time_check();
        o2_poll();
        o2_sleep(2); // 2ms
    }
    printf("We discovered the server.\ntime is %g.\n", o2_time_get());


    while (o2_status("server") != O2_BRIDGE) {
        time_check();
        o2_poll();
        o2_sleep(2); // 2ms
    }
    printf("The server has clock sync.\ntime is %g.\n", o2_time_get());
    
    double now = o2_time_get();
    while (o2_time_get() < now + 1) {
        time_check();
        o2_poll();
        o2_sleep(2);
    }
    
    printf("Here we go! ...\ntime is %g.\n", o2_time_get());
    
    if (use_tcp) o2_send_cmd("!server/benchmark/0", 0, "i", 1);
    else o2_send("!server/benchmark/0", 0, "i", 1);
    
    while (running) {
        time_check();
        o2_poll();
        o2_sleep(2); // 2ms (you could delete this line for benchmarking)
    }

    for (int i = 0; i < n_addrs; i++) {
        O2_FREE(server_addresses[i]);
    }
    O2_FREE(server_addresses);
    o2_finish();
#endif
    printf("CLIENT DONE\n");
    return 0;
}
