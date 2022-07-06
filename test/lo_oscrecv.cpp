//  lo_oscrecv.c - test program to receive simple OSC messages
//
//  this test is designed to run with oscsendtest.c

#include "stdio.h"
#include "assert.h"
#include "lo/lo.h"
#include "string.h"

#ifdef WIN32
#include <windows.h> 
#include "usleep.h" // special windows implementation of sleep/usleep
#else
#include <unistd.h>
#endif

int message_count = 0;
int timed_count = 0;
double timed_start = 0;

int small(double x) { return (x > -0.02) && (x < 0.02); }


#define JAN_1970 0x83aa7e80     /* 2208988800 1970 - 1900 in seconds */

// we'll use secs since 1970 for a little more precision
double timetag_to_secs(lo_timetag tt)
{
    return (tt.sec - JAN_1970) + tt.frac * 0.00000000023283064365;
}


int osc_i_handler(const char *path, const char *types,
                   lo_arg **argv, int argc, void *msg, void *user_data)
{
    assert(argv);
    assert(argc == 1);
    int i = argv[0]->i;
    if (i == 1234) {
        printf("osc_i_handler received 1234 at /osc/i\n");
        message_count++;
    } else if (i == 2000) {
        lo_timetag tt;
        lo_timetag_now(&tt);
        timed_start = timetag_to_secs(tt);
        timed_count = 1;
    } else if (2000 < i && i < 2010) {
        lo_timetag tt;
        lo_timetag_now(&tt);
        double now = timetag_to_secs(tt);
        printf("osc_i_handler received %d at elapsed %g\n", i,
               now - timed_start);
        i -= 2000;
        assert(i == timed_count);
        assert(small(timed_start + i * 0.1 - now));
        timed_count++;
    } else {
        assert(0); // unexpected message
    }
    return 0;
}


int main(int argc, const char * argv[])
{
    // flush everything no matter what (for getting as much info as possible when
    // there are problems):
    setvbuf (stdout, NULL, _IONBF, BUFSIZ);
    setvbuf (stderr, NULL, _IONBF, BUFSIZ);

    int tcpflag = 1;
    printf("Usage: lo_oscrecv [u] (u means use UDP)\n");
    if (argc == 2) {
        tcpflag = (strchr(argv[1], 'u') == NULL);
    }
    printf("tcpflag %d\n", tcpflag);
    
    lo_server server = lo_server_new_with_proto("8100", tcpflag ? LO_TCP : LO_UDP, NULL);

    lo_server_add_method(server, "/i", "i", &osc_i_handler, NULL);

    while (message_count < 10 || timed_count < 10) {
        lo_server_recv_noblock(server, 0);
        usleep(10000); // 10ms
    }
    printf("OSCRECV DONE\n");
    return 0;
}
