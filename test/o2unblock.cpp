//  o2unblock.c - client that received from o2block.c
//
//  see o2block.c for details

#include "o2usleep.h"
#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"

int MAX_MSG_COUNT = 100000;

int msg_count = 0;
bool running = true;

void client_test(o2_msg_data_ptr data, const char *types,
                 O2arg_ptr *argv, int argc, const void *user_data)
{
    msg_count++;
    if (msg_count == 1) {
        sleep(5); // cause sender to block
        o2_send_cmd("/server/hello", 0, "i", 1);
    }

    if (msg_count >= MAX_MSG_COUNT) {
        running = false;
        o2_send_cmd("/server/hello", 0, "i", msg_count);
    }
    if (msg_count % 5000 == 0) {
        printf("client received %d messages\n", msg_count);
    }
    if (msg_count < 5) {
        printf("client message %d is %d\n", msg_count, argv[0]->i32);
    }
    assert(msg_count == argv[0]->i32);
}


int main(int argc, const char *argv[])
{
    printf("Usage: o2unblock [debugflags]\n"
           "    see o2.h for flags, use a for all, - for none\n");
    if (argc >= 2) {
        if (argv[1][0] != '-') {
            o2_debug_flags(argv[1]);
            printf("debug flags are: %s\n", argv[1]);
        }
    }
    if (argc > 2) {
        printf("WARNING: o2client ignoring extra command line argments\n");
    }

    o2_initialize("test");
    o2_service_new("client");
    o2_method_new("/client/hello", "i", &client_test, NULL, false, true);
    
    while (o2_status("server") < O2_REMOTE) {
        o2_poll();
        usleep(2000); // 2ms
    }
    printf("We discovered the server.\ntime is %g.\n", o2_time_get());
    
    // no wait because server is looking for "0" to start
    
    printf("Here we go! ...\ntime is %g.\n", o2_time_get());
    
    o2_send_cmd("!server/hello", 0, "i", 0);
    
    while (running) {
        o2_poll();
    }

    // delay 0.1 second to make sure last message is sent
    double now = o2_time_get();
    while (o2_time_get() < now + 0.1) {
        o2_poll();
        usleep(2000);
    }

    o2_finish();
    printf("CLIENT DONE\n");
    return 0;
}
