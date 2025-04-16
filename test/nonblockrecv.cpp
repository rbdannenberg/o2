// nonblockrecv.c - receiving end for check for nonblocking send via tcp
//
// See nonblocksend.c for how the test works.
//
// Messages all have sequence numbers and a "last message" flag.
// 
// The server operates normally until the first message is received.
// Then the server should recieve (only) 10 messages per second so that
// the sender, which can send *much* faster, will eventually block.
// Keep receiving 10 messages per second until the last message is received.


#undef NDEBUG
#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"

int msg_count = 0;
bool running = true;
#define NOTYET -999
O2time start_sending = NOTYET;
static O2time block_time = NOTYET;
static int block_count = NOTYET;
static O2time unblock_time = NOTYET;
static int unblock_count = NOTYET;

// this is a handler for incoming messages. It makes sure messages are
// delivered in order and shuts down when we get the last one.
//
void server_test(O2msg_data_ptr msg, const char *types,
                 O2arg_ptr *argv, int argc, const void *user_data)
{
    assert(argc == 2);
    assert(strcmp(types, "iB") == 0);
    assert(argv[0]->i32 == msg_count);
    if (start_sending == NOTYET) {
        start_sending = o2_time_get();
        printf("Starting to receive from sender.\n");
    }
    msg_count++;
    if (msg_count % 5000 == 0) {
        printf("  msg_count %d\n", msg_count);
    }
    if (argv[1]->B) {
        running = false;
    }
}


void server_stat(O2msg_data_ptr msg, const char *types,
                 O2arg_ptr *argv, int argc, const void *user_data)
{
    bool blocked = argv[1]->B;
    if (blocked && block_count == NOTYET) {  // ignore after first /stat message
        block_time = o2_time_get();
        block_count = argv[0]->i32;
        printf("Sender blocked after %d msgs and %g s from start_sending.\n",
               block_count, block_time - start_sending);
    } else if (!blocked && unblock_count == NOTYET) {
        unblock_time = o2_time_get();
        unblock_count = argv[0]->i32;
        printf("Sender unblocked after %d msgs and %g s from start_sending.\n",
               unblock_count, unblock_time - start_sending);
    }        
}


int main(int argc, const char * argv[])
{
    printf("Usage: nonblockrecv [flags]] "
           "(see o2.h for flags, use a for (almost) all)\n");
    if (argc >= 2) {
        o2_debug_flags(argv[1]);
        printf("debug flags are: %s\n", argv[1]);
    }
    if (argc > 2) {
        printf("WARNING: tcpclient ignoring extra command line arguments\n");
    }
    o2_initialize("test");
    o2_service_new("server");
    o2_method_new("/server/test", "iB", &server_test, NULL, false, true);
    o2_method_new("/server/stat", "iB", &server_stat, NULL, false, true);
    
    // we are the master clock
    o2_clock_set(NULL, NULL);
    
    // we want to receive slowly until sender blocks, then keep receiving
    // for double that time to allow the sender to test unblocking and
    // blocking again.
    while (running &&
           (unblock_count == NOTYET ||
            (o2_time_get() <
             unblock_time + 2 * (unblock_time - start_sending)))) {
        o2_poll();
        o2_sleep(2); // we have a lot of messages to receive
    }
    printf("Sender testing time is up %g s after start_sending.\n",
           o2_time_get() - start_sending);
    while (running) {  // flush remaining messages
        o2_poll();     // no waiting, receive as fast as possible
    }
    printf("Received last message: count %d elapsed time %g s.\n",
           msg_count, o2_time_get() - start_sending);
    o2_send_cmd("!sender/done", 0, "");
    printf("Poll for 1s to make sure done message is received\n");
    for (int i = 0; i < 500; i++) {
        o2_poll();
        o2_sleep(2);
    }

    printf("Finish at O2 clock time %g\n", o2_time_get());
    o2_finish();
    o2_sleep(1000); // finish cleaning up sockets
    printf("SERVER DONE\n");
    return 0;
}
