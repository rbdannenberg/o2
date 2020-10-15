// mqttserver.c - test for MQTT as bridge for O2 messages
//
// This program works with mqttclient.c. It sends a message
// back and forth between a client and server.

/* DESCRIPTION:
To test MQTT, we need to be on two networks, which makes testing difficult,
but at least for now, I can get on two networks by putting one machine on
a VPN and the other on the local network. I don't see a way to test on a
single machine.

The test should work as follows: 

1. Both machines start on ensemble "test". Server is the clock reference.
1A. Server creates service called "server."
2. Client waits for discovery of "server". Report status.
2A. Server waits for discovery of "client". Report status.
3. Client waits for clock sync with "server". Report status.
3A. Server waits for clock sync with "client". Report status.
4. Client reports round-trip time for clock synchronization.
5. Client sends message with sequence number to server, starting with 1.
6. Server replies with sequence number + 100000.
7. Steps 5 and 6 repeat 9 more times for a total of 10 messages.
8. Client reports average O2 message round-trip time.
9. Client sends "goodbye" message to server.
10. Server exits when it receives the "goodbye" message.
11. Client exists after a 1s delay (to make sure the TCP send completes).

For development, this test should work without MQTT on a single machine.
*/

#include "o2.h"
#include "stdio.h"
#include "assert.h"

#ifdef WIN32
#include "usleep.h" // special windows implementation of sleep/usleep
#else
#include <unistd.h>
#endif


#define MAX_MSG_COUNT 10

int msg_count = 0;
bool running = true;

// this is a handler for incoming messages. It simply sends a message
// back to one of the client addresses
//
void server_fn(o2_msg_data_ptr msg, const char *types,
               o2_arg_ptr *argv, int argc, void *user_data)
{
    assert(argc == 1);
    msg_count++;
    o2_send_cmd("/client/client", 0, "i", msg_count + 1000);
    printf("server received %d messages\n", msg_count);
    printf("msg_count %d incoming %d\n", msg_count, argv[0]->i32);
    assert(msg_count == argv[0]->i32);
}


void server_done_fn(o2_msg_data_ptr msg, const char *types,
                    o2_arg_ptr *argv, int argc, void *user_data)
{
    printf("server received \"goodbye\" message.\n");
    running = false;
}


int main(int argc, const char *argv[])
{
    printf("Usage: mqttserver [flags]\n"
           "    see o2.h for flags, use a for all.\n");
    if (argc >= 2) {
        o2_debug_flags(argv[1]);
        printf("debug flags are: %s\n", argv[1]);
    }
    if (argc > 2) {
        printf("WARNING: o2server ignoring extra command line argments\n");
    }

    o2_initialize("test");
    o2_mqtt_enable(NULL);
    o2_service_new("server");
    
    // add our handler for incoming messages
    o2_method_new("/server/server", "i", &server_fn, NULL, false, true);
    o2_method_new("/server/goodbye", "i", &server_done_fn, NULL, false, true);
    
    // we are the master clock
    o2_clock_set(NULL, NULL);
    
    // wait for client service to be discovered
    while (o2_status("client") < O2_REMOTE_NOTIME) {
        o2_poll();
        usleep(2000); // 2ms
    }
    printf("We discovered the client at time %g.\n", o2_local_time());

    // wait for client service to be discovered
    while (o2_status("client") < O2_REMOTE) {
        o2_poll();
        usleep(2000); // 2ms
    }
    printf("Clock sync with client at time %g.\n", o2_time_get());
    
    // delay 1 second
    double now = o2_time_get();
    while (o2_time_get() < now + 1) {
        o2_poll();
        usleep(2000);
    }
    
    printf("Here we go! ...\ntime is %g.\n", o2_time_get());
    
    while (running) {
        o2_poll();
        usleep(2000); // 2ms // as fast as possible
    }

     assert(msg_count == MAX_MSG_COUNT);

    o2_finish();
    printf("SERVER DONE\n");
    return 0;
}
