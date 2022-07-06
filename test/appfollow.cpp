//  appfollow.c - change ensemble test/demo
////
//  see applead.c for details


#include "o2.h"
#include "stdio.h"
#include "string.h"

O2time cs_time = 1000000.0;

void appfollow(O2msg_data_ptr msg, const char *types,
               O2arg_ptr *argv, int argc, const void *user_data)
{
    int ss = o2_status("server");
    int cs = o2_status("client");
    double mean_rtt, min_rtt;
    o2_roundtrip(&mean_rtt, &min_rtt);
    printf("appfollow: local time %g global time %g "
           "ss %d cs %d mean %g min %g\n",
           o2_local_time(), o2_time_get(), ss, cs, mean_rtt, min_rtt);
    if (ss == O2_REMOTE) {
        if (o2_time_get() < cs_time) {
            cs_time = o2_time_get();
            printf("appfollow sync time %g, sending hello to server\n",
                   cs_time);
            o2_send_cmd("!server/hello", 0, "");
        }
    }
    // stop 10s later
    if (o2_time_get() > cs_time + 10) {
        o2_stop_flag = true;
        printf("appfollow set stop flag TRUE at %g\n", o2_time_get());
    }
    // wake up and try again in 1s:
    o2_send_start();
    O2message_ptr m = o2_message_finish(o2_local_time() + 1,
                                         "!client/appfollow", true);
    o2_schedule_msg(&o2_ltsched, m);
}


int main(int argc, const char * argv[])
{
    // flush everything no matter what (for getting as much info as possible when
    // there are problems):
    setvbuf (stdout, NULL, _IONBF, BUFSIZ);
    setvbuf (stderr, NULL, _IONBF, BUFSIZ);

    printf("Usage: appfollow [debugflags] "
           "(see o2.h for flags, use a for all)\n");
    if (argc == 2) {
        o2_debug_flags(argv[1]);
        printf("debug flags are: %s\n", argv[1]);
    }
    if (argc > 2) {
        printf("WARNING: appfollow ignoring extra command line argments\n");
    }

    o2_initialize("test1");
    o2_service_new("client");
    o2_method_new("/client/appfollow", "", &appfollow, NULL, false, false);
    // this particular handler ignores all parameters, so this is OK:
    // start polling and reporting status
    appfollow(NULL, NULL, NULL, 0, NULL);
    o2_run(100);
    o2_finish();

    cs_time = 1000000.0;
    o2_stop_flag = false;
    o2_initialize("test2");
    o2_service_new("client");
    o2_method_new("/client/appfollow", "", &appfollow, NULL, false, false);
    // this particular handler ignores all parameters, so this is OK:
    // start polling and reporting status
    appfollow(NULL, NULL, NULL, 0, NULL);
    o2_run(100);
    o2_finish();

    o2_sleep(1000);
    printf("APPFOLLOW DONE\n");
    return 0;
}
