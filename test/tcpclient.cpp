//  tcpclient.c - O2 over tcp check, and part of performance benchmark
//
//  see tcpserver.c for details

#undef NDEBUG
#include "o2.h"
#include <stdio.h>
#include <stdlib.h>   // atoi
#include <string.h>
#include <assert.h>


#define N_ADDRS 20

// this was 50000, not sure why -- very slow with 2ms polling
int max_msg_count = 1000; 

char *server_addresses[N_ADDRS];
int msg_count = 0;
bool running = true;

void client_test(O2msg_data_ptr data, const char *types,
                O2arg_ptr *argv, int argc, const void *user_data)
{
    msg_count++;
    int32_t i = msg_count + 1;
    // server will shut down when it gets data == -1
    if (msg_count >= max_msg_count) {
        i = -1;
        running = false;
    }
    o2_send_cmd(server_addresses[msg_count % N_ADDRS], 0, "i", i);
    if (msg_count % 10000 == 0) {
        printf("client received %d messages\n", msg_count);
    }
    if (msg_count < 100) {
        printf("client message %d is %d\n", msg_count, argv[0]->i32);
    }
    assert(msg_count == argv[0]->i32);
}


int main(int argc, const char * argv[])
{
    printf("Usage: tcpclient [msgcount [flags]] "
           "(see o2.h for flags, use a for (almost) all)\n");
    if (argc >= 2) {
        max_msg_count = atoi(argv[1]);
        printf("max_msg_count set to %d\n", max_msg_count);
    }
    if (argc >= 3) {
        o2_debug_flags(argv[2]);
        printf("debug flags are: %s\n", argv[2]);
    }
    if (argc > 3) {
        printf("WARNING: tcpclient ignoring extra command line arguments\n");
    }
    o2_initialize("test");
    o2_service_new("client");
    
    for (int i = 0; i < N_ADDRS; i++) {
        char path[100];
        sprintf(path, "/client/benchmark/%d", i);
        o2_method_new(path, "i", &client_test, NULL, false, true);
    }
    
    for (int i = 0; i < N_ADDRS; i++) {
        char path[100];
        sprintf(path, "!server/benchmark/%d", i);
        server_addresses[i] = O2_MALLOCNT(strlen(path), char);
        strcpy(server_addresses[i], path);
    }
    
    while (o2_status("server") < O2_LOCAL) {
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
    
    o2_send_cmd("!server/benchmark/0", 0, "i", 1);
    
    while (running) {
        o2_poll();
        //o2_sleep(2); // 2ms // as fast as possible
    }
    // poll some more to make sure last message goes out
    for (int i = 0; i < 100; i++) {
        o2_poll();
        o2_sleep(2); // 2ms
    }

    // clean up
    for (int i = 0; i < N_ADDRS; i++) {
        O2_FREE(server_addresses[i]);
    }
    
    o2_finish();
    o2_sleep(1000); // finish cleaning up sockets
    printf("CLIENT DONE\n");
    return 0;
}
