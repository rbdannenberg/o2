//  clockmaster.c - clock synchronization test/demo
//
//  This program works with clockslave.c. It monitors clock
//  synchronization and status updates.
//

#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"

#ifdef WIN32
#include "usleep.h" // special windows implementation of sleep/usleep
#else
#include <unistd.h>
#endif

#define streql(a, b) (strcmp(a, b) == 0)

o2_time cs_time = 1000000.0;

// this is a handler that polls for current status
// it runs about every 1s
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


int rtt_sent = FALSE;
char client_ip_port[32];

void service_info(o2_msg_data_ptr msg, const char *types,
                  o2_arg_ptr *argv, int argc, void *user_data)
{
    const char *service_name = argv[0]->s;
    int new_status = argv[1]->i32;
    const char *ip_port = argv[2]->s;
    printf("service_info: service %s status %d ip_port %s\n",
           service_name, new_status, ip_port);
    if (streql(service_name, "client") && (new_status == O2_REMOTE)) {
        // client has clock sync
        if (!rtt_sent) {
            char address[32];
            strcpy(client_ip_port, ip_port); // save it for rtt_reply check
            sprintf(address, "%s%s%s", "!", ip_port, "/cs/rt");
            o2_send_cmd(address, 0.0, "s", "!server/rtt");
            printf("Sent message to %s\n", address);
            rtt_sent = TRUE;
        }
    }
}


int rtt_received = FALSE;

void rtt_reply(o2_msg_data_ptr msg, const char *types,
               o2_arg_ptr *argv, int argc, void *user_data)
{
    const char *service_name = argv[0]->s;
    float mean = argv[1]->f;
    float minimum = argv[2]->f;
    printf("rtt_reply: service %s mean %g min %g\n",
           service_name, mean, minimum);
    assert(rtt_sent);
    assert(streql(service_name, client_ip_port));
    assert(mean >= 0 && mean < 1);
    assert(minimum >= 0 && minimum < 1);
    rtt_received = TRUE;
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
    o2_method_new("/_o2/si", "sis", &service_info, NULL, FALSE, TRUE);
    o2_method_new("/server/rtt/get-reply", "sff", &rtt_reply, NULL, FALSE, TRUE);
    // we are the master clock
    o2_clock_set(NULL, NULL);
    o2_send("!server/clockmaster", 0.0, ""); // start polling
    o2_run(100);
    o2_finish();
    sleep(1);
    if (rtt_received) {
        printf("CLOCKMASTER DONE\n");
    } else {
        printf("CLOCKMASTER FAILED (no rtt message)\n");
    }
    return 0;
}
