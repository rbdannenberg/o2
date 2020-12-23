//  oscrecvtest.c - test o2_osc_port_new()
//
//  this test is designed to run with oscsendtest.c
//  see oscsendtest.c for details


#include "o2usleep.h"
#include "stdio.h"
#include "o2.h"
#include "string.h"
#include "assert.h"

int message_count = 0;
O2time timed_start = 0;
int timed_count = 0;

int approx(double x) { return (x > -0.04) && (x < 0.04); }


void osc_i_handler(o2_msg_data_ptr data, const char *types,
                   O2arg_ptr *argv, int argc, const void *user_data)
{
    assert(argv);
    assert(argc == 1);
    int i = argv[0]->i;
    if (i == 1234) {
        printf("osc_i_handler received 1234 at /oscrecv/i @ %g\n",
               o2_time_get());
        message_count++;
    } else if (i == 2000) {
        timed_start = o2_time_get();
        timed_count = 1;
        printf("osc_i_handler received %d @ %g\n",
               i, o2_time_get());
    } else if (2000 < i && i < 2010) {
        printf("osc_i_handler received %d at elapsed %g\n", i,
               o2_time_get() - timed_start);
        i -= 2000;
        assert(i == timed_count);
#ifndef NDEBUG
        O2time now = o2_time_get(); // only needed in assert()
#endif
        assert(approx(timed_start + i * 0.1 - now));
        timed_count++;
    } else if (i == 5678) {
        printf("osc_i_handler received 5678 @ %g but port should be closed.\n",
               o2_time_get());
        assert(false);
    } else if (i == 6789) {
        printf("osc_i_handler received 6789 @ %g but port should be closed.\n",
               o2_time_get());
        assert(false);
    } else {
        assert(false); // unexpected message
    }
}


int main(int argc, const char * argv[])
{
    printf("Usage: oscrecvtest [flags] "
           "(see o2.h for flags, use a for all, also u for UDP)\n");
    int tcpflag = true;
    if (argc == 2) {
        o2_debug_flags(argv[1]);
        tcpflag = (strchr(argv[1], 'u') == NULL);
    }
    if (argc > 2) {
        printf("WARNING: o2server ignoring extra command line argments\n");
    }

    o2_initialize("test");

    printf("tcpflag %d\n", tcpflag);
    int err = o2_osc_port_new("oscrecv", 8100, tcpflag);
    assert(err == O2_SUCCESS);
    
    o2_clock_set(NULL, NULL);
    o2_service_new("oscrecv");
    o2_method_new("/oscrecv/i", "i", osc_i_handler, NULL, false, true);
    while (message_count < 10 || timed_count < 10) {
        o2_poll();
        usleep(2000); // 2ms
    }
    err = o2_osc_port_free(8100);
    assert(err == O2_SUCCESS);
    printf("*** osc port freed @ %g\n", o2_time_get());

    // now wait for 4 seconds and check for more messages
    // sender will send after 1 seconds
    for (int i = 0; i < 2000; i++) {
        o2_poll();
        usleep(2000); // 2ms
    }
    printf("*** calling o2_finish @ %g\n", o2_time_get());
    o2_finish();
    printf("OSCRECV DONE\n");
    sleep(1); // allow TCP to finish up
    return 0;
}
