//  lo_oscsend.c - test to send simple OSC messages
//
//  this test is designed to run with oscrecvtest.c

#include "stdio.h"
#include "assert.h"
#include "lo/lo.h"
#include "string.h"

#ifdef WIN32
#include <windows.h> 
#else
#include <unistd.h>
#endif

lo_timetag start;

#define TWO32 4294967296.0

void timetag_add(lo_timetag *timetag, lo_timetag x, double y)
{
    double secs = x.sec + (x.frac / TWO32);
    secs += y;
    timetag->sec = (uint32_t) secs;
    timetag->frac = (uint32_t) ((secs - timetag->sec) * TWO32);
}


void wait_until(double offset)
{
    lo_timetag deadline;
    lo_timetag now;
    lo_timetag_now(&now);
    timetag_add(&deadline, start, offset);
    while (lo_timetag_diff(deadline, now) > 0) {
        usleep(2000);
        lo_timetag_now(&now);
    }
}

int main(int argc, const char * argv[])
{
    int tcpflag = 1;
    printf("Usage: lo_oscsend [u] (u means use UDP)\n");
    if (argc == 2) {
        tcpflag = (strchr(argv[1], 'u') == NULL);
    }
    printf("tcpflag %d\n", tcpflag);
    sleep(2); // allow some time for server to start
    
    lo_address client = lo_address_new_with_proto(tcpflag ? LO_TCP : LO_UDP,
                                                  "localhost", "8100");
    printf("client: %p\n", client);
    
    // send 12 messages, 1 every 0.5s, and stop
    for (int n = 0; n < 12; n++) {
        lo_send(client, "/i", "i", 1234);
        printf("sent 1234 to /i\n");
        // pause for 0.5s, but keep running O2 by polling
        for (int i = 0; i < 250; i++) {
            usleep(2000); // 2ms
        }
    }
    // send 10 messages with timestamps spaced by 0.1s
    lo_timetag_now(&start);
    for (int n = 0; n < 10; n++) {
        wait_until(n * 0.1);
        lo_send(client, "/i", "i", 2000 + n);
    }

    sleep(1); // make sure messages go out
    lo_address_free(client);

    printf("OSCSEND DONE\n");
    return 0;
}
