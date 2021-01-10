//  o2block.c - test for blocking
//
//  This program works with o2unblock.c. It waits for client to
//  sent 0 to "/server/hello"; then we start sending 1000 messages
//  but we pause when the message stream blocks. As soon as we block
//  we wait for client to send 1. Then we continue. After MAX_MSG_COUNT
//  messages are sent, we should get MAX_MSG_COUNT back from client.
//

#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"

#include "o2base.h"

// To put some weight on fast address lookup, we create n_addrs
// different addresses to use.
//
int msg_count = 0;
bool running = true;

// this is a handler for incoming messages. It makes sure messages are
// delivered in order and shuts down when we get the last one.
//
void server_test(o2_msg_data_ptr msg, const char *types,
                 o2_arg_ptr *argv, int argc, void *user_data)
{
    assert(argc == 2);
    assert(strcmp(types, "iB") == 0);
    assert(argv[0]->i32 == msg_count);
    msg_count++;
    if (argv[1]->B) {
        running = false;
    }
}


int main(int argc, const char *argv[])
{
    printf("Usage: o2 [debugflags]\n"
           "    see o2.h for flags, use a for all, - for none\n");
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
    
    // delay 1 second
    double now = o2_time_get();
    while (o2_time_get() < now + 1) {
        o2_poll();
        o2_sleep(2);
    }
    assert(got_start);
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
            assert(got_one);
            blocked = true; // only expected got_one once
        }
        o2_poll();
    }
    // after we're done sending, look for got_max
    now = o2_time_get();
    while (o2_time_get() < now + 1) {
        o2_poll();
        o2_can_send("client"); // what happens when client disappears?
        o2_sleep(2);
    }
    assert(got_max);
    assert(o2_can_send("client") == O2_FAIL); // does not exist

    o2_finish();
    printf("SERVER DONE\n");
    return 0;
}
