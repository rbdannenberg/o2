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


// this is a handler for incoming messages. It makes sure messages are
// delivered in order and shuts down when we get the last one.
//
void server_test(o2_msg_data_ptr msg, const char *types,
                 o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(argc == 2);
    assert(strcmp(types, "iB") == 0);
    assert(argv[0]->i32 == msg_count);
    msg_count++;
    if (msg_count % 5000 == 0) {
        printf("msg_count %d\n", msg_count);
    }
    if (argv[1]->B) {
        running = false;
    }
}


int main(int argc, const char * argv[])
{
    printf("Usage: nonblockrecv [flags]] "
           "(see o2.h for flags, use a for all)\n");
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
    
    // we are the master clock
    o2_clock_set(NULL, NULL);
    
    while (running) {
        o2_poll();
        // usleep(msg_count > 0 ? 100000 : 2000); // 100ms or 2ms
        usleep(500);
    }
    o2_send_cmd("!sender/done", 0, "");
    printf("Poll for 1s to make sure message is received\n");
    for (int i = 0; i < 1000; i++) {
        o2_poll();
        usleep(1000); // 1ms
    }
    
    o2_finish();
    sleep(1); // finish cleaning up sockets
    printf("SERVER DONE\n");
    return 0;
}
