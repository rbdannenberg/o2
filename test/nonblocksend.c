// nonblocksend.c - check for nonblocking send via tcp
//
// How the test works: Wait until we have "server" as a service
// Begin sending messages to /server/test until the send would block
// Check that some initial messages do not block.
// After the socket would block, wait until the socket would not block.
// Send again until socket would block, then send 5 more without checking.
// (This last step should force a blocking send. We want to make sure that
// works too.)
//
// Messages all have sequence numbers and a "last message" flag.
// 
// The server should operate normally until the first message is received.
// Then the server should recieve (only) 10 messages per second so that
// the sender, which can send *much* faster, will eventually block.
// Keep receiving 10 messages per second until the last message is received.


#ifdef __GNUC__
// define usleep:
#define _XOPEN_SOURCE 500
#define _POSIX_C_SOURCE 200112L
#endif

#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"

#ifdef WIN32
#include "usleep.h" // special windows implementation of sleep/usleep
#else
#include <unistd.h>
#endif

int msg_count = 0;
bool running = true;


// at the end, we get a message to /sender/done
void sender_done(o2_msg_data_ptr msg, const char *types,
                 o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(argc == 0);
    assert(strlen(types) == 0);
    running = false;
}


int main(int argc, const char * argv[])
{
    printf("Usage: nonblocksend [flags]] "
           "(see o2.h for flags, use a for all)\n");
    if (argc >= 2) {
        o2_debug_flags(argv[1]);
        printf("debug flags are: %s\n", argv[1]);
    }
    if (argc > 2) {
        printf("WARNING: tcpclient ignoring extra command line arguments\n");
    }
    o2_initialize("test");
    o2_service_new("sender"); // that's us
    o2_method_new("/sender/done", "", &sender_done, NULL, false, true);
    
    while (o2_status("server") < O2_LOCAL) {
        o2_poll();
        usleep(2000); // 2ms
    }
    printf("We discovered the server.\ntime is %g.\n", o2_time_get());
    
    double now = o2_time_get();
    while (o2_time_get() < now + 1) {
        o2_poll();
        usleep(2000);
    }
    
    printf("Here we go! ...\ntime is %g.\n", o2_time_get());
    while (o2_can_send("server") == O2_SUCCESS) {
        o2_send_cmd("!server/test", 0, "iB", msg_count, false);
        msg_count++;
        o2_poll();
    }
    assert(msg_count > 1); // first message should not have blocked
    // it's possible 2nd message blocked and is queued
    printf("Blocked after %d messages.\n", msg_count);

    // poll until server unblocks
    while (o2_can_send("server") == O2_BLOCKED) {    
        o2_poll();
        usleep(2000); // 2ms
    }
    assert(o2_can_send("server") == O2_SUCCESS);
    printf("Resuming sends after blocked message.\n");

    // send until blocks again
    while (o2_can_send("server") == O2_SUCCESS) {
        o2_send_cmd("!server/test", 0, "iB", msg_count, false);
        msg_count++;
        o2_poll();
    }
    printf("Blocked again after %d messages.\n", msg_count);

    // send 2 * msg_count more messages to make sure blocking works
    int n = 2 * msg_count;
    for (int i = 0; i < n; i++) {
        o2_send_cmd("!server/test", 0, "iB", msg_count, false);
        msg_count++;
        o2_poll();
        if (msg_count % 2000 == 0) {
            printf("msg_count %d\n", msg_count);
        }
    }        
    printf("Sent %d more messages to make sure blocking works\n", n);

    // send last message
    o2_send_cmd("!server/test", 0, "iB", msg_count, true);
    printf("Sent %d messages total.\n", msg_count);

    // now, the problem is we could have 1000's of buffered
    // messages being received at 1000 per second, so how long
    // do we need to wait before we delete the socket? Let's
    // get an ACK from the receiver.

    printf("Poll until we get a done message from receiver.\n");
    while (running) {
        o2_poll();
        usleep(2000); // 2ms
    }
    
    o2_finish();
    sleep(1); // finish cleaning up sockets
    printf("CLIENT DONE\n");
    return 0;
}
