//  oscrecvtest.c - test o2_osc_port_create()
//
//  this test is designed to run with oscsendtest.c


#include "stdio.h"
#include "o2.h"
#include "string.h"
#include "assert.h"

#ifdef WIN32
#include <windows.h> 
#else
#include <unistd.h>
#endif

int message_count = 0;
o2_time timed_start = 0;
int timed_count = 0;

int small(double x) { return (x > -0.02) && (x < 0.02); }


void osc_i_handler(o2_msg_data_ptr data, const char *types,
                   o2_arg_ptr *argv, int argc, void *user_data)
{
    assert(argv);
    assert(argc == 1);
    int i = argv[0]->i;
    if (i == 1234) {
        printf("osc_i_handler received 1234 at /osc/i\n");
        message_count++;
    } else if (i == 2000) {
        timed_start = o2_time_get();
        timed_count = 1;
    } else if (2000 < i && i < 2010) {
        printf("osc_i_handler received %d at elapsed %g\n", i,
               o2_time_get() - timed_start);
        i -= 2000;
        assert(i == timed_count);
        assert(small(timed_start + i * 0.1 - o2_time_get()));
        timed_count++;
    } else {
        assert(FALSE); // unexpected message
    }
}


int main(int argc, const char * argv[])
{
    printf("Usage: oscrecvtest [flags] "
           "(see o2.h for flags, use a for all, also u for UDP)\n");
    int tcpflag = TRUE;
    if (argc == 2) {
        o2_debug_flags(argv[1]);
        tcpflag = (strchr(argv[1], 'u') == NULL);
    }
    if (argc > 2) {
        printf("WARNING: o2server ignoring extra command line argments\n");
    }

    o2_initialize("test");

    printf("tcpflag %d\n", tcpflag);
    assert(o2_osc_port_create("osc", 8100, tcpflag) == O2_SUCCESS);
    
    o2_clock_set(NULL, NULL);
    o2_add_method("/osc/i", "i", osc_i_handler, NULL, FALSE, TRUE);
    while (message_count < 10 || timed_count < 10) {
        o2_poll();
        usleep(10000); // 10ms
    }
    o2_osc_port_remove(8100);
    o2_finish();
    printf("OSCRECV DONE\n");
    sleep(1); // allow TCP to finish up
    return 0;
}
