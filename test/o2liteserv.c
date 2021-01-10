// o2liteserv.c -- simple test of message create and dispatch for o2lite
//
// Roger B. Dannenberg
// July 2020

/* 
This test:
- initialize o2lite
- wait for discovery
- wait for clock sync
- send a message to self over O2 with sift types
- respond to messages from o2litehost's client services
*/

/* does not define usleep when compiled with:

/usr/bin/cc -D_FORTIFY_SOURCE=0 -D_POSIX_C_SOURCE=201112L -I/home/rbd/o2/src  -std=c11 -mcx16 -g   -o CMakeFiles/o2liteserv.dir/test/o2liteserv.c.o   -c /home/rbd/o2/test/o2liteserv.c

*/

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "o2lite.h"
#include <string.h>

#define streql(a, b) (strcmp(a, b) == 0)

int n_addrs = 20;
bool running = true;
int msg_count = 0;
char **client_addresses = NULL;
char **server_addresses = NULL;

bool about_equal(double a, double b)
{
    return a / b > 0.999999 && a / b < 1.000001;
}

bool use_tcp = false;


// this is a handler for incoming messages. It simply sends a message
// back to one of the client addresses
//
void server_test(o2l_msg_ptr msg, const char *types, void *data, void *info)
{
    int got_i = o2l_get_int32();

    msg_count++;
    o2l_send_start(client_addresses[msg_count % n_addrs], 0, "i", use_tcp);
    o2l_add_int32(msg_count);
    o2l_send();

    if (msg_count % 10000 == 0) {
        printf("server received %d messages\n", msg_count);
    }
    if (msg_count < 100) {
        printf("server message %d is %d\n", msg_count, got_i);
    }
    if (got_i == -1) {
        running = false;
    } else {
        assert(msg_count == got_i);
    }
}


bool sift_called = false;

// handles types "ist"
void sift_han(o2l_msg_ptr msg, const char *types, void *data, void *info)
{
    printf("sift_han called\n");
    assert(info = (void *) 111);
    assert(streql(o2l_get_string(), "this is a test"));
    assert(o2l_get_int32() == 1234);
    assert(about_equal(o2l_get_float(), 123.4));
    assert(about_equal(o2l_get_time(), 567.89));
    sift_called = true;
}


int main(int argc, const char * argv[])
{
    printf("Usage: o2liteserv [tcp] [debug]\n"
           "    pass t to test with TCP, u (default) for UDP\n");
    if (argc >= 2) {
        if (strchr(argv[1], 't' )) {
            use_tcp = true;
        }
        printf("Using %s to reply to client\n", use_tcp ? "TCP" : "UDP");
    }
    if (argc > 2) {
        printf("WARNING: o2liteserv ignoring extra command line argments\n");
    }

    o2l_initialize("test");
    o2l_set_services("sift");

    o2l_method_new("/sift", "sift", true, &sift_han, (void *) 111);

    while (o2l_bridge_id < 0) { // not connected
        o2l_poll();
        o2_sleep(2); // 2ms
    }
    printf("main detected o2lite connected\n");

    o2l_send_start("/sift", 0, "sift", true);
    o2l_add_string("this is a test");
    o2l_add_int32(1234);
    o2l_add_float(123.4F);
    o2l_add_time(567.89);
    o2l_send();

    while (o2l_time_get() < 0) { // not synchronized
        o2l_poll();
        o2_sleep(2); // 2ms
    }
    printf("main detected o2lite clock sync\n");

    o2l_time start_wait = o2l_time_get();
    while (start_wait + 1 > o2l_time_get() && !sift_called) {
        o2l_poll();
        o2_sleep(2);
    }
    printf("main received loop-back message\n");

    // now create addresses and handlers to receive server messages
    client_addresses = (char **) malloc(sizeof(char **) * n_addrs);
    server_addresses = (char **) malloc(sizeof(char **) * n_addrs);
    for (int i = 0; i < n_addrs; i++) {
        char path[100];

        sprintf(path, "!client/benchmark/%d", i);
        client_addresses[i] = (char *) (malloc(strlen(path)));
        strcpy(client_addresses[i], path);

        sprintf(path, "/server/benchmark/%d", i);
        server_addresses[i] = (char *) (malloc(strlen(path)));
        strcpy(server_addresses[i], path);
        o2l_method_new(server_addresses[i], "i", true, &server_test, NULL);
    }
    // we are ready for the client, so announce the server services
    o2l_set_services("sift,server");

    while (running) {
        o2l_poll();
        o2_sleep(2);
    }

    printf("o2liteserv\nSERVER DONE\n");
}
