//  o2server.c - benchmark for local message passing
//
//  This program works with o2client.c. It is a performance test
//  that sends a message back and forth between a client and server.
//

#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"

#ifdef WIN32
#include "usleep.h" // special windows implementation of sleep/usleep
#else
#include <unistd.h>
#endif


// To put some weight on fast address lookup, we create n_addrs
// different addresses to use.
//
char **client_addresses;
int n_addrs = 20;

#define MAX_MSG_COUNT 50000

int msg_count = 0;
int running = TRUE;

// this is a handler for incoming messages. It simply sends a message
// back to one of the client addresses
//
void server_test(o2_msg_data_ptr msg, const char *types,
                 o2_arg_ptr *argv, int argc, void *user_data)
{
    assert(argc == 1);
    msg_count++;
    o2_send(client_addresses[msg_count % n_addrs], 0, "i", msg_count);
    if (msg_count % 10000 == 0) {
        printf("server received %d messages\n", msg_count);
    }
    if (msg_count < 100) {
        printf("server message %d is %d\n", msg_count, argv[0]->i32);
    }
    if (argv[0]->i32 == -1) {
        running = FALSE;
    } else {
        assert(msg_count == argv[0]->i32);
    }
}


int main(int argc, const char *argv[])
{
    printf("Usage: o2server [debugflags] [n_addrs]\n"
           "    see o2.h for flags, use a for all, - for none\n"
           "    n_addrs is number of addresses to use, default 20\n");
    if (argc >= 2) {
        if (argv[1][0] != '-') {
            o2_debug_flags(argv[1]);
            printf("debug flags are: %s\n", argv[1]);
        }
    }
    if (argc >= 3) {
        n_addrs = atoi(argv[2]);
        printf("n_addrs is %d\n", n_addrs);
    }
    if (argc > 3) {
        printf("WARNING: o2server ignoring extra command line argments\n");
    }

    o2_initialize("test");
    o2_service_new("server");
    
    // add our handler for incoming messages to each server address
    for (int i = 0; i < n_addrs; i++) {
        char path[100];
        sprintf(path, "/server/benchmark/%d", i);
        o2_method_new(path, "i", &server_test, NULL, FALSE, TRUE);
    }
    
    // create an address for each destination so we do not have to
    // do string manipulation to send a message
    client_addresses = (char **) malloc(sizeof(char **) * n_addrs);
    for (int i = 0; i < n_addrs; i++) {
        char path[100];
        sprintf(path, "!client/benchmark/%d", i);
        client_addresses[i] = (char *) (O2_MALLOC(strlen(path)));
        strcpy(client_addresses[i], path);
    }

    // we are the master clock
    o2_clock_set(NULL, NULL);
    
    // wait for client service to be discovered
    while (o2_status("client") < O2_REMOTE) {
        o2_poll();
        usleep(2000); // 2ms
    }
    
    printf("We discovered the client at time %g.\n", o2_time_get());
    
    // delay 1 second
    double now = o2_time_get();
    while (o2_time_get() < now + 1) {
        o2_poll();
        usleep(2000);
    }
    
    printf("Here we go! ...\ntime is %g.\n", o2_time_get());
    
    while (running) {
        o2_poll();
        //usleep(2000); // 2ms // as fast as possible
    }

    o2_finish();
    printf("SERVER DONE\n");
    return 0;
}
