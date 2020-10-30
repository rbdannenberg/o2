// oscsendtest.c - test o2_osc_delegate()
//
// this test is designed to run with oscrecvtest.c
//
// Usage: oscsendtest [flags] (see o2.h for flags, 
//             use a for all, also u for UDP, @ for clock ref)
//
// The test:
//   initialize as a clock reference or mirror depending on @ flag
//   if we are reference, assume we are talking to a liblo server,
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


int main(int argc, const char * argv[])
{
    printf("Usage: oscsendtest [flags] (see o2.h for flags,\n"
           "use a for all, also u for UDP, @ for reference, p for \n"
           "pause before sending to closed port.)\n");

    bool tcpflag = true;
    bool clockref = false;
    bool pauseflag = false;
    if (argc == 2) {
        o2_debug_flags(argv[1]);
        tcpflag = (strchr(argv[1], 'u') == NULL);
        clockref = (strchr(argv[1], '@') != NULL);
        pauseflag = (strchr(argv[1], 'p') != NULL);
    }
    if (argc > 2) {
        printf("WARNING: oscsendtest ignoring extra command line argments\n");
    }
    printf("tcpflag %d clockref %d\n", tcpflag, clockref);

    o2_initialize("test");

    // you can make this run without an O2 server by passing "R" flag
    if (clockref)
        o2_clock_set(NULL, NULL);
    else if (argc > 2)
        printf("Usage: oscsendtest [clockref]\n");
    
    if (clockref) sleep(2); // wait for liblo server if we are clockref
    printf("Waiting for clock sync\n");
    while (!o2_clock_is_synchronized) {
        usleep(2000);
        o2_poll();
    }
    printf("*** Clock sync obtained @ %g\n", o2_time_get());

    o2_err_t err = o2_osc_delegate("oscsend", "localhost", 8100, tcpflag);
    assert(err == O2_SUCCESS);
    
    // send 12 messages, 1 every 0.5s, and stop
    for (int n = 0; n < 12; n++) {
        err = o2_send("/oscsend/i", 0, "i", 1234);
        assert(err == O2_SUCCESS);
        printf("sent 1234 to /oscsend/i @ %g\n", o2_time_get());
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
    printf("*** sent 2000 - 2009 starting at %g ending at %g\n",
           now, now + 0.9);
    printf("*** Waiting 2s for 2000 series to go out @ %g\n", o2_time_get());
    // pause for 2s to make sure messages are sent and service is deleted
    for (int i = 0; i < 1000; i++) {
        o2_poll();
        usleep(2000); // 2ms
    }
    if (pauseflag) {
        printf("Type return to continue by sending to expected closed port: ");
        while (getchar() != '\n') ;
        printf("*** Polling O2 after pause @ %g\n", o2_time_get());
        // in case we were paused, run o2 to process service removed msgs
        for (int i = 0; i < 500; i++) {
            o2_poll();
            usleep(2000); // 2ms
        }
    }
    // receiver should close port now and check that these
    // messages are NOT received:
    printf("*** Sending to closed port (we expect) @ %g\n", o2_time_get());
    err = o2_send("/oscsend/i", 0, "i", 5678);
    printf("Return value is %d %s\n", err, o2_error_to_string(err));
    assert(err == (tcpflag ? O2_NO_SERVICE : O2_SUCCESS));
    o2_service_free("oscsend");
    printf("*** Calling o2_finish @ %g\n", o2_time_get());
    o2_finish();
    sleep(1); // finish closing sockets
    printf("OSCSEND DONE\n");
    return 0;
}
