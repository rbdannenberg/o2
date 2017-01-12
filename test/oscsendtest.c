//  oscsendtest.c - test o2_osc_delegate()
//
//  this test is designed to run with oscrecvtest.c

#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"

#ifdef WIN32
#include "usleep.h"
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

    // you can make this run without an O2 server by passing "master"
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

    assert(o2_osc_delegate("oscsend", 
                           "localhost", 8100, tcpflag) == O2_SUCCESS);
    
    // send 12 messages, 1 every 0.5s, and stop
    for (int n = 0; n < 12; n++) {
        assert(o2_send("/oscsend/i", 0, "i", 1234) == O2_SUCCESS);
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
    o2_service_free("oscsend");
    o2_finish();
    sleep(1); // finish closing sockets
    printf("OSCSEND DONE\n");
    return 0;
}
