//  o2block.c - test for blocking
//
//  This program works with o2unblock.c. It waits for client to
//  send 0 to "/server/hello"; then we start sending 1000 messages
//  but we pause when the message stream blocks. As soon as we block
//  we wait for client to send 1. Then we continue. After MAX_MSG_COUNT
//  messages are sent, we should get MAX_MSG_COUNT back from client.
//

#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "testassert.h"

// This number must be big enough to cause TCP to block. 50000 is big
// enough for macOS, but Ubuntu linux required 100000, which means it
// buffered between 3 and 6MB (!)
#define MAX_MSG_COUNT 100000

int msg_count = 0;
bool running = true;

bool got_start = false;
bool got_one = false;
bool got_max = false;


// this is a handler for incoming messages. It simply sends a message
// back to one of the client addresses
//
void server_test(O2msg_data_ptr msg, const char *types,
                 O2arg_ptr *argv, int argc, const void *user_data)
{
    o2assert(argc == 1);
    o2assert(strcmp(types, "i") == 0);
    if (argv[0]->i32 == 0) {
        got_start = true;
        printf("Got start (0) at %g\n", o2_time_get());
    }
    if (argv[0]->i32 == 1) {
        got_one = true;
        printf("Got one (1) at %g\n", o2_time_get());
    }
    if (argv[0]->i32 == MAX_MSG_COUNT) {
        got_max = true;
        printf("Got MAX_MSG_COUNT (%d) at %g\n", MAX_MSG_COUNT, o2_time_get());
    }
}


int main(int argc, const char *argv[])
{
    printf("Usage: o2server [debugflags]\n"
           "    see o2.h for flags, use a for (almost) all, - for none\n");
    if (argc >= 2) {
        if (argv[1][0] != '-') {
            o2_debug_flags(argv[1]);
            printf("debug flags are: %s\n", argv[1]);
        }
    }
    if (argc > 2) {
        printf("WARNING: o2server ignoring extra command line argments\n");
    }

    o2_initialize("test");
    o2_service_new("server");
    o2_method_new("/server/hello", "i", &server_test, NULL, false, true);
    
    // we are the master clock
    o2_clock_set(NULL, NULL);
    
    // wait for client service to be discovered
    while (o2_status("client") < O2_REMOTE) {
        o2_poll();
        o2_sleep(2); // 2ms
    }
    
    printf("We discovered the client at time %g.\n", o2_time_get());
    
    // delay 1 second (maybe not needed)
    double now = o2_time_get();
    while (o2_time_get() < now + 1) {
        o2_poll();
        o2_sleep(2);
    }
    o2assert(got_start);
    printf("Here we go! ...\ntime is %g.\n", o2_time_get());
    int blocked = false;
    while (msg_count < MAX_MSG_COUNT) {
        if (o2_can_send("client") == O2_SUCCESS) {
            msg_count++;
            o2_send_cmd("/client/hello", 0, "i", msg_count);
            if (msg_count % 5000 == 0) {
                printf("msg_count %d\n", msg_count);
            }
        } else if (!blocked) { // first time
            now = o2_time_get();
            while (o2_time_get() < now + 6 && !got_one) {
                o2_poll();
                o2_sleep(2);
            }
            o2assert(got_one);
            blocked = true; // only expected got_one once
        }
        o2_poll();
    }
    // now we wait for client to get all MAX_MSG_COUNT messages and
    // reply with MAX_MSG_COUNT -- might take awhile if we are way ahead
    now = o2_time_get();
    while (!got_max && o2_time_get() < now + 5) {
        o2_poll();
        o2_sleep(2);
    }
    o2assert(got_max);
    // after got_max, client waits 1 sec and exits, so if we "got_max"
    // and wait 2 sec, then we should see that the client does not exist
    now = o2_time_get();
    while (o2_time_get() < now + 2) {
        o2_poll();
        o2_sleep(2);
    }
    o2assert(o2_can_send("client") == O2_FAIL); // does not exist

    o2_finish();
    printf("SERVER DONE\n");
    return 0;
}
