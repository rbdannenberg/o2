//  clockmaster.c - clock synchronization test/demo
//
//  This program works with clockslave.c. It monitors clock
//  synchronization and status updates.
//

#include "o2.h"
#include "stdio.h"
#include "string.h"

#ifdef WIN32
#include <windows.h> 
#else
#include <unistd.h>
#endif

o2_time cs_time = 1000000.0;

// this is a handler that polls for current status
//
void clockmaster(o2_msg_data_ptr msg, const char *types,
                 o2_arg_ptr *argv, int argc, void *user_data)
{
    int ss = o2_status("server");
    int cs = o2_status("client");
    printf("clockmaster: local time %g global time %g "
           "server status %d client status %d\n",
           o2_local_time(), o2_time_get(), ss, cs);
    // record when the client synchronizes
    if (cs == O2_REMOTE) {
        if (o2_time_get() < cs_time) {
            cs_time = o2_time_get();
            printf("clockmaster sync time %g\n", cs_time);
        }
    }
    // stop 10s later
    if (o2_time_get() > cs_time + 10) {
        o2_stop_flag = TRUE;
        printf("clockmaster set stop flag TRUE at %g\n", o2_time_get());
    }
    o2_send("!server/clockmaster", o2_time_get() + 1, "");
}


int main(int argc, const char * argv[])
{
    printf("Usage: clockmaster [debugflags] "
           "(see o2.h for flags, use a for all)\n");
    if (argc == 2) {
        o2_debug_flags(argv[1]);
        printf("debug flags are: %s\n", argv[1]);
    }
    if (argc > 2) {
        printf("WARNING: clockmaster ignoring extra command line argments\n");
    }

    o2_initialize("test");
    o2_service_new("server");
    o2_method_new("/server/clockmaster", "", &clockmaster, NULL, FALSE, FALSE);
    // we are the master clock
    o2_clock_set(NULL, NULL);
    o2_send("!server/clockmaster", 0.0, ""); // start polling
    o2_run(100);
    o2_finish();
    sleep(1);
    printf("CLOCKMASTER DONE\n");
    return 0;
}
