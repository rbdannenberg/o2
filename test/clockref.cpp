//  clockref.c - clock synchronization test/demo
//
//  This program works with clockmirror.c. It monitors clock
//  synchronization and status updates.
//

#include "o2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

bool keep_alive = false;
bool timing_info = false;
int polling_rate = 100;
O2time cs_time = 1000000.0;

// this is a handler that polls for current status
// it runs about every 1s
//
void clockref(o2_msg_data_ptr msg, const char *types,
                 O2arg_ptr *argv, int argc, const void *user_data)
{
    int ss = o2_status("server");
    int cs = o2_status("client");
    printf("clockref: local time %g global time %g "
           "server status %d client status %d\n",
           o2_local_time(), o2_time_get(), ss, cs);
    // record when the client synchronizes
    if (cs == O2_REMOTE) {
        if (o2_time_get() < cs_time) {
            cs_time = o2_time_get();
            printf("clockref sync time %g\n", cs_time);
        }
    }
    // stop 10s later
    if (o2_time_get() > cs_time + 2 && !keep_alive) {
        o2_stop_flag = true;
        printf("clockref set stop flag true at %g\n", o2_time_get());
    }
    o2_send("!server/clockref", o2_time_get() + 1, "");
}


bool rtt_sent = false;
char client_ip_port[O2_MAX_PROCNAME_LEN];

void service_info(o2_msg_data_ptr msg, const char *types,
                  O2arg_ptr *argv, int argc, const void *user_data)
{
    const char *service_name = argv[0]->s;
    int new_status = argv[1]->i32;
    const char *ip_port = argv[2]->s;
    const char *properties = argv[3]->s;
    printf("service_info: service %s status %d ip_port %s properties \"%s\"\n",
           service_name, new_status, ip_port, properties);
    if (streql(service_name, "client") && (new_status == O2_REMOTE)) {
        // client has clock sync
        if (!rtt_sent) {
            char address[O2_MAX_PROCNAME_LEN];
            strcpy(client_ip_port, ip_port); // save it for rtt_reply check
            snprintf(address, O2_MAX_PROCNAME_LEN, "%s%s%s",
                     "!", ip_port, "/cs/rt");
            o2_send_cmd(address, 0.0, "s", "!server/rtt/ans");
            printf("Sent message to %s\n", address);
            rtt_sent = true;
        }
    }
}


bool rtt_received = false;

void rtt_reply(o2_msg_data_ptr msg, const char *types,
               O2arg_ptr *argv, int argc, const void *user_data)
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
    rtt_received = true;
}


/* o2_run with modifications to explore timing */
/* modifications are marked with TIMING_INFO */
#define TIMING_INFO 1
int o2_run_special(int rate)
{
    if (rate <= 0) rate = 1000; // poll about every ms
    int sleep_ms = 1000 / rate;
    if (sleep_ms < 1) sleep_ms = 1; // maximum rate is 1000 (1 ms period)
    o2_stop_flag = false;
#if TIMING_INFO
    double maxtime = 0.0;
    double mintime = 100.0;
    double lasttime = 0;
    int count = 0;
#endif
    while (!o2_stop_flag) {
        o2_poll();
        o2_sleep(sleep_ms);
#if TIMING_INFO
        count++;
        if (timing_info) {
            double now = o2_local_time();
            double looptime = now - lasttime;
            lasttime = now;
            maxtime = MAX(maxtime, looptime);
            mintime = MIN(mintime, looptime);
            if (count % 1000 == 0) {
                printf("now %g maxtime %g mintime %g looptime %g, "
                       "sleep_ms %d\n", now, maxtime, mintime,
                       looptime, sleep_ms);
                lasttime = o2_local_time();
                mintime = 100.0;
                maxtime = 0.0;
            }
        }
        if (count % 10000 == 0) {
            printf("o2_time_get: %.3f\n", o2_time_get());
        }
#endif
    }
    return O2_SUCCESS;
}


int main(int argc, const char * argv[])
{
    printf("Usage: clockref [debugflags] [zd]\n"
           "    see o2.h for flags, use a for all, - for none\n"
           "    1000 (or another number) specifies O2 polling rate (optional, "
           "default 100)\n"
           "    use optional z flag to stay running for long-term tests\n"
           "    use optional d flag to print details of local clock time and polling\n");
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
            printf("clockref will not stop, kill with ^C to quit.\n\n");
            keep_alive = true;
        }
        if (strchr(argv[2], 'd') != NULL) {
            printf("d flag found - printing extra clock and polling info\n\n");
            timing_info = true;
        }
    }
    if (argc > 3) {
        printf("WARNING: clockref ignoring extra command line argments\n");
    }

    if (o2_initialize("test")) {
        printf("FAIL\n");
        return -1;
    }
    o2_service_new("server");
    o2_method_new("/server/clockref", "", &clockref, NULL, false, false);
    o2_method_new("/_o2/si", "siss", &service_info, NULL, false, true);
    o2_method_new("/server/rtt/ans", "sff", &rtt_reply, NULL, false, true);
    // we are the ref clock
    o2_clock_set(NULL, NULL);
    o2_send("!server/clockref", 0.0, ""); // start polling
    o2_run_special(polling_rate);
    o2_finish();
    o2_sleep(1000);
    if (rtt_received) {
        printf("CLOCKREF DONE\n");
    } else {
        printf("CLOCKREF FAILED (no rtt message)\n");
    }
    return 0;
}
