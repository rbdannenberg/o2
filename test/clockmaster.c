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
        if (o2_time_get() < cs_time) cs_time = o2_time_get();
    }
    // stop 10s later
    if (o2_time_get() + 10 > cs_time) o2_stop_flag = TRUE;
    o2_send("!server/clockmaster", o2_time_get() + 1, "");
}


int main(int argc, const char * argv[])
{
    o2_initialize("test");
    o2_service_add("server");
    o2_add_method("/server/clockmaster", "", &clockmaster, NULL, FALSE, FALSE);
    // we are the master clock
    o2_clock_set(NULL, NULL);
    o2_send("!server/clockmaster", 0.0, ""); // start polling
    o2_run(100);
    o2_finish();
    sleep(1);
    printf("CLOCKMASTER DONE\n");
    return 0;
}
