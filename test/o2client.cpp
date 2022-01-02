//  o2client.cpp - part of performance benchmark
//
//  see o2server.cpp for details

#include "o2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

int max_msg_count = 1000;

char **server_addresses;
int n_addrs = 20;
int use_tcp = false;

int msg_count = 0;
bool running = true;

void client_test(O2msg_data_ptr data, const char *types,
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


int main(int argc, const char *argv[])
{
    printf("Usage: o2client [maxmsgs] [debugflags] [n_addrs]\n"
           "    see o2.h for flags, use a for all, - for none\n"
           "    n_addrs is number of addresses to use, default 20\n"
           "    n_addrs must match the number used by o2server\n"
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
        if (argv[1][0] != '-') {
            o2_debug_flags(argv[2]);
            printf("debug flags are: %s\n", argv[2]);
        }
    }
    if (argc >= 4) {
        n_addrs = atoi(argv[3]);
        printf("n_addrs is %d\n", n_addrs);
    }
    if (argc > 4) {
        printf("WARNING: o2client ignoring extra command line argments\n");
    }

    o2_initialize("test");
#ifndef O2_NO_BRIDGES
    o2lite_initialize(); // enable o2lite - this test is used with o2litedisc
#endif
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
        server_addresses[i] = O2_MALLOCNT(strlen(path) + 1, char);
        strcpy(server_addresses[i], path);
    }

    while (o2_status("server") < O2_REMOTE) {
        o2_poll();
        o2_sleep(2); // 2ms
    }
    printf("We discovered the server.\ntime is %g.\n", o2_time_get());
    
    double now = o2_time_get();
    while (o2_time_get() < now + 1) {
        o2_poll();
        o2_sleep(2);
    }
    
    printf("Here we go! ...\ntime is %g.\n", o2_time_get());
    
    if (use_tcp) o2_send_cmd("!server/benchmark/0", 0, "i", 1);
    else o2_send("!server/benchmark/0", 0, "i", 1);
    
    while (running) {
        o2_poll();
        o2_sleep(2); // 2ms // as fast as possible
    }

    // run some more to make sure messages get sent
    now = o2_time_get();
    while (o2_time_get() < now + 0.1) {
        o2_poll();
        o2_sleep(2);
    }
    
    for (int i = 0; i < n_addrs; i++) {
        O2_FREE(server_addresses[i]);
    }
    O2_FREE(server_addresses);
    o2_finish();
    printf("CLIENT DONE\n");
    return 0;
}
