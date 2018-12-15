// oscsendtest.c - test o2_osc_delegate()
//
// this test is designed to run with oscrecvtest.c
//
// Usage: oscsendtest [flags] (see o2.h for flags, 
//             use a for all, also u for UDP, M for master)
//
// The test:
//   initialize as a clock slave or master depending on M flag
//   if we are master, assume we are talking to a liblo server,
//         so sleep 2 seconds allowing liblo server to launch
//         (you should launch it first if running manually)
//   send 12 messages, 1 every 0.5s, and stop,
//         message are /oscsend/i 1234
//   send 10 messages with timestamps,
//         messages are /oscsend/i <2000+i>
//   reciever can now call o2_osc_port_free to test closing the port
//   wait 1 second
//   send 1 message: /oscsend/i 5678
//   wait 0.1 seconds
//   send 1 message with timestamp: /oscsend/i 6789
//   wait 1 second
//   shut everything down

#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"

#ifdef WIN32
#include "usleep.h" // special windows implementation of sleep/usleep
#else
#include <unistd.h>
#endif


int main(int argc, const char * argv[])
{
    printf("Usage: oscsendtest [flags] (see o2.h for flags, "
           "use a for all, also u for UDP, M for master)\n");

    int tcpflag = TRUE;
    int master = FALSE;
    if (argc == 2) {
        o2_debug_flags(argv[1]);
        tcpflag = (strchr(argv[1], 'u') == NULL);
        master = (strchr(argv[1], 'M') != NULL);
    }
    if (argc > 2) {
        printf("WARNING: o2server ignoring extra command line argments\n");
    }
    printf("tcpflag %d master %d\n", tcpflag, master);

    o2_initialize("test");

    // you can make this run without an O2 server by passing "M" flag
    if (master)
        o2_clock_set(NULL, NULL);
    else if (argc > 2)
        printf("Usage: oscsendtest [master]\n");
    
    if (master) sleep(2); // wait for liblo server to come up if we are master
    printf("Waiting for clock sync\n");
    while (!o2_clock_is_synchronized) {
        usleep(2000);
        o2_poll();
    }

    int err = o2_osc_delegate("oscsend", "localhost", 8100, tcpflag);
    assert(err == O2_SUCCESS);
    
    // send 12 messages, 1 every 0.5s, and stop
    for (int n = 0; n < 12; n++) {
        err = o2_send("/oscsend/i", 0, "i", 1234);
        assert(err == O2_SUCCESS);
        printf("sent 1234 to /oscsend/i\n");
        // pause for 0.5s, but keep running O2 by polling
        for (int i = 0; i < 250; i++) {
            o2_poll();
            usleep(2000); // 2ms
        }
    }
    // send 10 messages with timestamps spaced by 0.1s
    o2_time now = o2_time_get();
    for (int n = 0; n < 10; n++) {
        o2_send("/oscsend/i", now + n * 0.1, "i", 2000 + n);
    }
    // pause for 1s to make sure messages are sent
    for (int i = 0; i < 500; i++) {
        o2_poll();
        usleep(2000); // 2ms
    }
    // receiver may want to close port now and check that these
    printf("Time to close receiver's port if you want to test that.\n");
    // messages are NOT received:
    err = o2_send("/oscsend/i", 0, "i", 5678);
    assert(err == O2_SUCCESS);
    printf("sent 5678 to /oscsend/i\n");
    // pause for 0.1s, but keep running O2 by polling
    for (int i = 0; i < 50; i++) {
        o2_poll();
        usleep(2000); // 2ms
    }
    double ts = o2_time_get() + 0.1;
    o2_send("/oscsend/i", ts, "i", 6789);
    printf("sent 6789 to /oscsend/i with timestamp %g\n", ts);
    // pause for 1s, but keep running O2 by polling
    for (int i = 0; i < 500; i++) {
        o2_poll();
        usleep(2000); // 2ms
    }
    o2_service_free("oscsend");
    o2_finish();
    sleep(1); // finish closing sockets
    printf("OSCSEND DONE\n");
    return 0;
}
