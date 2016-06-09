//  o2server.c - benchmark for local message passing
//
//  This program works with o2client.c. It is a performance test
//  that sends a message back and forth between a client and server.
//

#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "unistd.h"

// To put some weight on fast address lookup, we create N_ADDRS
// different addresses to use.
//
#define N_ADDRS 20

char *client_addresses[N_ADDRS];
int msg_count = 0;

// this is a handler for incoming messages. It simply sends a message
// back to one of the client addresses
//
int server_test(o2_message_ptr msg, const char *types,
                o2_arg ** argv, int argc, void *user_data)
{
    o2_send(client_addresses[msg_count % N_ADDRS], 0, "i", msg_count);
    if (msg_count % 10000 == 0) {
        printf("server received %d messages\n", msg_count);
    }
    msg_count++;
    return O2_SUCCESS;
}


int main(int argc, const char * argv[])
{
    o2_initialize("test");
    o2_add_service("server");
    
    // add our handler for incoming messages to each server address
    for (int i = 0; i < N_ADDRS; i++) {
        char path[100];
        sprintf(path, "/server/benchmark/%d", i);
        o2_add_method(path, "i", &server_test, NULL, FALSE, FALSE);
    }
    
    // create an address for each destination so we do not have to
    // do string manipulation to send a message
    for (int i = 0; i < N_ADDRS; i++) {
        char path[100];
        sprintf(path, "!client/benchmark/%d", i);
        client_addresses[i] = (char *) O2_MALLOC(strlen(path));
        strcpy(client_addresses[i], path);
    }
    
    // we are the master clock
    o2_set_clock(NULL, NULL);
    
    // wait for client service to be discovered
    while (o2_status("client") < O2_LOCAL) {
        o2_poll();
        usleep(2000); // 2ms
    }
    
    printf("We discovered the client at time %g.\n", o2_get_time());
    
    // delay 5 seconds
    double now = o2_get_time();
    while (o2_get_time() < now + 5) {
        o2_poll();
        usleep(2000);
    }
    
    printf("Here we go! ...\ntime is %g.\n", o2_get_time());
    
    while (TRUE) {
        o2_poll();
        //usleep(2000); // 2ms // as fast as possible
    }

    o2_finish();
    return 0;
}
