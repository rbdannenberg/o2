//  clockmirror.c - clock synchronization test/demo
//
// Algorithm for test:
// - About every 1 sec:
//    - check status of server and client services.
//    - when server is found, record time as cs_time
//    - after 2 sec, stop
// Note that there really are no tests other than
// termination requires a server service in test ensemble.

#undef NDEBUG
#include "o2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

int keep_alive = false;
int polling_rate = 100;
O2time cs_time = 1000000.0;

void clockmirror(O2msg_data_ptr msg, const char *types,
                 O2arg_ptr *argv, int argc, const void *user_data)
{
    int ss = o2_status("server");
    int cs = o2_status("client");
    double mean_rtt, min_rtt;
    o2_roundtrip(&mean_rtt, &min_rtt);
    printf("clockmirror: local time %g global time %g "
           "ss %d cs %d mean %g min %g\n",
           o2_local_time(), o2_time_get(), ss, cs, mean_rtt, min_rtt);
    if (ss == O2_REMOTE) {
        if (o2_time_get() < cs_time) {
            cs_time = o2_time_get();
            printf("clockmirror sync time %g\n", cs_time);
        }
    }
    // stop 2s later
    if (o2_time_get() > cs_time + 2 && !keep_alive) {
        o2_stop_flag = true;
        printf("clockmirror set stop flag true at %g\n", o2_time_get());
    }
    // Since the clock mirror cannot immediately send scheduled messages
    // due to there being no global time reference, we will schedule
    // messages directly on the local scheduler
    o2_send_start();
    O2message_ptr m = o2_message_finish(o2_local_time() + 1,
                                         "!client/clockmirror", true);
    o2_schedule_msg(&o2_ltsched, m);
}


int main(int argc, const char * argv[])
{
    // flush everything no matter what (for getting as much info as possible when
    // there are problems):
    setvbuf (stdout, NULL, _IONBF, BUFSIZ);
    setvbuf (stderr, NULL, _IONBF, BUFSIZ);

    printf("Usage: clockmirror [debugflags] [1000z]\n"
           "    see o2.h for flags, use a for (almost) all, - for none\n"
           "    1000 (or another number) specifies O2 polling rate (optional, "
           "default 100)\n"
           "    use optional z flag to stay running for long-term tests\n");
    if (argc >= 2 && strcmp(argv[1], "-") != 0) {
        o2_debug_flags(argv[1]);
        printf("debug flags are: %s\n", argv[1]);
    }
    if (argc >= 3) {
        if (isdigit(argv[2][0])) {
            polling_rate = atoi(argv[2]);
            printf("O2 polling rate: %d\n", polling_rate);
        }
        if (strchr(argv[2], 'z') != NULL) {
            printf("clockmirror will not stop, kill with ^C to quit.\n\n");
            keep_alive = true;
        }
    }
    if (argc > 3) {
        printf("WARNING: clockmirror ignoring extra command line argments\n");
    }

    o2_initialize("test");
    o2_service_new("client");
    o2_method_new("/client/clockmirror", "", &clockmirror, NULL, false, false);
    // this particular handler ignores all parameters, so this is OK:
    // start polling and reporting status
    clockmirror(NULL, NULL, NULL, 0, NULL);
    o2_run(polling_rate);
    o2_finish();
    o2_sleep(1000);
    printf("CLOCKMIRROR DONE\n");
    return 0;
}
