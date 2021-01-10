//  tcpserver.c - O2 over TCP check and benchmark for message passing
//
//  This program works with tcpclient.c. It is a performance test
//  that sends a message back and forth between a client and server.
//

#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"

// To put some weight on fast address lookup, we create N_ADDRS
// different addresses to use.
//
#define N_ADDRS 20

char *client_addresses[N_ADDRS];
int msg_count = 0;
bool running = true;

// this is a handler for incoming messages. It simply sends a message
// back to one of the client addresses
//
void server_test(o2_msg_data_ptr msg, const char *types,
                 O2arg_ptr *argv, int argc, const void *user_data)
{
    assert(argc == 1);
    msg_count++;
    o2_send_cmd(client_addresses[msg_count % N_ADDRS], 0, "i", msg_count);
    if (msg_count % 10000 == 0) {
        printf("server received %d messages\n", msg_count);
    }
    if (msg_count < 100) {
        printf("server message %d is %d\n", msg_count, argv[0]->i32);
    }
    if (argv[0]->i32 == -1) {
        running = false;
    } else {
        assert(msg_count == argv[0]->i32);
    }
}


int main(int argc, const char * argv[])
{
    printf("Usage: tcpserver [debugflags] "
           "(see o2.h for flags, use a for all)\n");
    if (argc == 2) {
        o2_debug_flags(argv[1]);
        printf("debug flags are: %s\n", argv[1]);
    }
    if (argc > 2) {
        printf("WARNING: tcpserver ignoring extra command line argments\n");
    }

    o2_initialize("test");
    o2_service_new("server");
    
    // add our handler for incoming messages to each server address
    for (int i = 0; i < N_ADDRS; i++) {
        char path[100];
        sprintf(path, "/server/benchmark/%d", i);
        o2_method_new(path, "i", &server_test, NULL, false, true);
    }
    
    // create an address for each destination so we do not have to
    // do string manipulation to send a message
    for (int i = 0; i < N_ADDRS; i++) {
        char path[100];
        sprintf(path, "!client/benchmark/%d", i);
        client_addresses[i] = O2_MALLOCNT(strlen(path), char);
        strcpy(client_addresses[i], path);
    }
    
    // we are the master clock
    o2_clock_set(NULL, NULL);
    
    // wait for client service to be discovered
    while (o2_status("client") < O2_LOCAL) {
        o2_poll();
        o2_sleep(2); // 2ms
    }
    
    printf("We discovered the client at time %g.\n", o2_time_get());
    
    // delay 1 second
    double now = o2_time_get();
    while (o2_time_get() < now + 1) {
        o2_poll();
        o2_sleep(2);
    }
    
    printf("Here we go! ...\ntime is %g.\n", o2_time_get());
    
    while (running) {
        o2_poll();
    }

    o2_finish();
    o2_sleep(1000); // clean up sockets
    printf("SERVER DONE\n");
    return 0;
}
