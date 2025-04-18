// nonblocksend.c - check for nonblocking send via tcp
//
// How the test works: Wait until we have "server" as a service
// Begin sending messages to /server/test until the send would block
// Check that some initial messages do not block.
// After the socket would block, wait until the socket would not block.
// Send 2 times as many mesages without checking to create a blocking
// condition and test that blocking works.
//
// Messages all have sequence numbers and a "last message" flag.
// 
// The server should operate normally until the first message is received.
// Since there could be thousands of buffered messages, it takes too long
// to receive them at a low rate, e.g. even 1000 per second could take
// minutes if Linux TCP buffers 1MB. But if we receive too fast, then the
// sender will have to send even more messages before blocking is reached.
//
// The solution is have the server receive slowly (500/sec) until the
// sender blocks. Then receive 500 messages per second for an twice
// the amount of *time*, which should ensure that the sender has time
// to test that blocking and unblocking are working. Then receive at
// full speed to check that all messages are sent.
//
// To detect when the sender blocks, we'll have it send 10 UDP messages.
// Even though TCP is blocked and may have 10's of thousands of messages
// in the queue, a UDP message will come on a different socket and should
// be processed almost immediately.
//
// Messages:
//    Normal sequence of TCP messages: /server/test "iB" msg_count true
//    UDP message to say we've reached a blocking state:
//                                     /server/stat "i" msg_count
//    End of sequence:                 /server/test "iB" msg_count false


#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "testassert.h"

int msg_count = 0;
bool running = true;
#define NOTYET -999
O2time start_sending = NOTYET;

// at the end, we get a message to /sender/done
void sender_done(O2msg_data_ptr msg, const char *types,
                 O2arg_ptr *argv, int argc, const void *user_data)
{
    o2assert(argc == 0);
    o2assert(strlen(types) == 0);
    running = false;
}


int main(int argc, const char * argv[])
{
    printf("Usage: nonblocksend [flags]] "
           "(see o2.h for flags, use a for (almost) all)\n");
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
        o2_sleep(2); // 2ms
    }
    printf("We discovered the server.\ntime is %g.\n", o2_time_get());
    
    double now = o2_time_get();
    while (o2_time_get() < now + 1) {
        o2_poll();
        o2_sleep(2);
    }
    
    printf("Here we go! ...\ntime is %g.\n", o2_time_get());
    start_sending = o2_time_get();
    while (o2_can_send("server") == O2_SUCCESS) {
        o2_send_cmd("!server/test", 0, "iB", msg_count, false);
        msg_count++;
        o2_poll();
    }
    o2assert(msg_count > 1); // first message should not have blocked
    // it's possible 2nd message blocked and is queued
    printf("Blocked after %d msgs and %g s from start_sending.\n",
           msg_count, o2_time_get() -  start_sending);

    // send multiple messages to make sure one gets through
    // use UDP to bypass the blocked queue of TCP messages
    // tell server that we have blocked
    for (int i = 0; i < 10; i++) {
        o2_send("!server/stat", 0, "iB", msg_count, true);
    }

    // poll until server unblocks
    while (o2_can_send("server") == O2_BLOCKED) {    
        o2_poll();
        o2_sleep(2); // 2ms
    }
    o2assert(o2_can_send("server") == O2_SUCCESS);

    printf("Unblocked after %d msgs and %g s from start_sending.\n",
           msg_count, o2_time_get() -  start_sending);

    // tell server that we have unblocked
    for (int i = 0; i < 10; i++) {
        o2_send("!server/stat", 0, "iB", msg_count, false);
    }
    printf("Resuming sends after blocked message.\n");

    // send until blocks again
    while (o2_can_send("server") == O2_SUCCESS) {
        o2_send_cmd("!server/test", 0, "iB", msg_count, false);
        msg_count++;
        o2_poll();
    }
    printf("Blocked again after %d msgs and %g s after start_sending.\n",
           msg_count, o2_time_get() - start_sending);

    // send 2 * msg_count more messages to make sure blocking works
    int n = 2 * msg_count;
    for (int i = 0; i < n; i++) {
        o2_send_cmd("!server/test", 0, "iB", msg_count, false);
        msg_count++;
        o2_poll();
        if (msg_count % 5000 == 0) {
            printf("msg_count %d\n", msg_count);
        }
    }        
    printf("Sent %d more messages to make sure blocking works\n", n);

    // send last message
    o2_send_cmd("!server/test", 0, "iB", msg_count, true);
    printf("Sent %d messages total in %g s.\n",
           msg_count, o2_time_get() - start_sending);

    // now, the problem is we could have 1000's of buffered
    // messages being received at 1000 per second, so how long
    // do we need to wait before we delete the socket? Let's
    // get an ACK from the receiver.

    printf("Poll until we get a done message from receiver at O2 time %g.\n",
           o2_time_get());
    while (running) {
        o2_poll();
        o2_sleep(2); // 2ms
    }
    
    printf("Finish at O2 clock time %g\n", o2_time_get());
    o2_finish();
    o2_sleep(1000); // finish cleaning up sockets
    printf("CLIENT DONE\n");
    return 0;
}
