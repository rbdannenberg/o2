//  clockmaster.c - clock synchronization test/demo
//
//  This program works with clockslave.c. It monitors clock
//  synchronization and status updates.
//

#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"
#include "ctype.h"

#ifdef WIN32
#include "usleep.h" // special windows implementation of sleep/usleep
#else
#include <unistd.h>
#endif

#define streql(a, b) (strcmp(a, b) == 0)
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

int keep_alive = FALSE;
int timing_info = FALSE;
int polling_rate = 100;
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
    if (o2_time_get() > cs_time + 10 && !keep_alive) {
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

/* o2_run with modifications to explore timing */
/* modifications are marked with TIMING_INFO */
#define TIMING_INFO 1
int o2_run_special(int rate)
{
	if (rate <= 0) rate = 1000; // poll about every ms
	int sleep_usec = 1000000 / rate;
	o2_stop_flag = FALSE;
#if TIMING_INFO
	double maxtime = 0.0;
	double mintime = 100.0;
	double lasttime = 0;
	int count = 0;
#endif
	while (!o2_stop_flag) {
		o2_poll();
		usleep(sleep_usec);
#if TIMING_INFO
		count++;
		if (timing_info) {
			double now = o2_local_time();
			double looptime = now - lasttime;
			lasttime = now;
			maxtime = MAX(maxtime, looptime);
			mintime = MIN(mintime, looptime);
			if (count % 1000 == 0) {
				printf("now %g maxtime %g mintime %g looptime %g, sleep_usec %d\n", now, maxtime, mintime, looptime, sleep_usec);
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
    printf("Usage: clockmaster [debugflags] [zd]\n"
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
			printf("clockmaster will not stop, kill with ^C to quit.\n\n");
			keep_alive = TRUE;
		}
		if (strchr(argv[2], 'd') != NULL) {
			printf("d flag found - printing extra clock and polling info\n\n");
			timing_info = TRUE;
		}
	}
    if (argc > 3) {
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
    o2_run_special(polling_rate);
    o2_finish();
    sleep(1);
    if (rtt_received) {
        printf("CLOCKMASTER DONE\n");
    } else {
        printf("CLOCKMASTER FAILED (no rtt message)\n");
    }
    return 0;
}
