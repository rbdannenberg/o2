
//  infotest2.c -- test if we get info via /_o2/si
//

#include <stdio.h>
#include "o2.h"
#include "string.h"
#ifdef WIN32
#include "usleep.h" // special windows implementation of sleep/usleep
#else
#include <unistd.h>
#endif

#define streql(a, b) (strcmp(a, b) == 0)

#define N_ADDRS 10

const char *status_strings[] = {
    "O2_LOCAL_NOTIME",
    "O2_REMOTE_NOTIME",
    "O2_BRIDGE_NOTIME",
    "O2_TO_OSC_NOTIME",
    "O2_LOCAL",
    "O2_REMOTE",
    "O2_BRIDGE",
    "O2_TO_OSC" };

const char *status_to_string(int status)
{
    static char unknown[32];
    if (status >= 0 && status <= 7) {
        return status_strings[status];
    } else if (status == O2_FAIL) {
        return "O2_FAIL";
    }
    sprintf(unknown, "UNKNOWN(%d)", status);
    return unknown;
}


void service_one(o2_msg_data_ptr data, const char *types,
                 o2_arg_ptr *argv, int argc, void *user_data)
{
    printf("Service one received a message\n");
}


o2_time cs_time = 1000000.0;

// this is a handler that polls for current status
//
void clockmaster(o2_msg_data_ptr msg, const char *types,
                 o2_arg_ptr *argv, int argc, void *user_data)
{
    int ss = o2_status("server");
    int cs = o2_status("client");
    printf("infotest2: local time %g global time %g "
           "server status %d client status %d\n",
           o2_local_time(), o2_time_get(), ss, cs);
    // record when the client synchronizes
    if (cs == O2_REMOTE) {
        if (o2_time_get() < cs_time) {
            cs_time = o2_time_get();
            printf("infotest2 sync time %g\n", cs_time);
        }
    }
    // stop 12s later to make sure clockslave shuts down first and
    // we get the status info (/_o2/si) messages about it
    if (o2_time_get() > cs_time + 12) {
        o2_stop_flag = TRUE;
        printf("infotest2 set stop flag TRUE at %g\n", o2_time_get());
    }
    o2_send("!server/clockmaster", o2_time_get() + 1, "");
}


char my_ip_port[32];
char remote_ip_port[32];

int check_service_name(const char *service, const char **names, int index)
{
    const char *expected = names[index];
    if (streql(expected, "ip:port")) {
        expected = my_ip_port;
    } else if (streql(expected, "remote")) {
        expected = remote_ip_port;
    }
    return streql(service, expected);
}



int si_msg_count = 0;

// first wave of status info is local before set_clock
#define EXPECTED_1 5
const char *expected_si_service_1[] = {"_o2", "ip:port", "one", "server", "_cs"};
int expected_si_status_1[] = {O2_LOCAL_NOTIME, O2_LOCAL_NOTIME,
        O2_LOCAL_NOTIME, O2_LOCAL_NOTIME, O2_LOCAL};

// second wave of status info is local after set_clock
#define EXPECTED_2 (4 + (EXPECTED_1))
const char *expected_si_service_2[] = {"_o2", "ip:port", "one", "server"};

// third wave of status info is for remote process
#define EXPECTED_3 (2 + (EXPECTED_2))
const char *expected_si_service_3[] = {"remote", "client"};

// fourth wave of status info is for remote process
#define EXPECTED_4 (2 + (EXPECTED_3))

// fifth wave of status info is for remote process closing down
#define EXPECTED_5 (2 + (EXPECTED_4))

void service_info_handler(o2_msg_data_ptr data, const char *types,
                 o2_arg_ptr *argv, int argc, void *user_data)
{
    const char *service_name = argv[0]->s;
    int status = argv[1]->i32;
    const char *status_string = status_to_string(status);
    const char *ip_port = argv[2]->s;
     printf("service_info_handler called: %s at %s status %s\n", 
           service_name, ip_port, status_string);
    const char *my_ip = NULL;
    int my_port = -1;
    o2_get_address(&my_ip, &my_port);
    if (!my_ip) my_ip = "none";
    sprintf(my_ip_port, "%s:%d", my_ip, my_port);
    // the first 4 /_o2/si messages are listed in expected_si_service_1,
    // where "ip:port" is replaced by the real ip:port string.

    if (si_msg_count < EXPECTED_1) {
        if (!check_service_name(service_name, expected_si_service_1, si_msg_count) ||
            !streql(my_ip_port, ip_port) ||
            status != expected_si_status_1[si_msg_count]) {
            printf("FAILURE\n");
            exit(-1);
        }

    // after the first 5 messages, the clock becomes master, and we expect
    // 4 more messages in some order and we become O2_LOCAL. The services
    // are in expected_si_service_2[], which we search and remove from
    // to allow for any order of notification (order could vary because
    // O2 will enumerate what's in a hash table.
    } else if (si_msg_count < EXPECTED_2) {
        int found_it = 0;
        int i;
        for (i = 0; i < 4; i++) {
            if (check_service_name(service_name, expected_si_service_2, i) &&
                status == O2_LOCAL &&
                streql(ip_port, my_ip_port)) {
                expected_si_service_2[i] = "";
                found_it = 1;
            }
        }
        if (!found_it) {
            printf("FAILURE\n");
            exit(-1);
        }
    } else if (si_msg_count < EXPECTED_3) {
        if (remote_ip_port[0] == 0) {
            strcpy(remote_ip_port, service_name);
        }
        if (!check_service_name(service_name, expected_si_service_3,
                                si_msg_count - EXPECTED_2) ||
            !streql(ip_port, remote_ip_port) ||
            status != O2_REMOTE_NOTIME) {
            printf("FAILURE\n");
            exit(-1);
        }
    } else if (si_msg_count < EXPECTED_4) {
        if (remote_ip_port[0] == 0) {
            strcpy(remote_ip_port, service_name);
        }
        if (!check_service_name(service_name, expected_si_service_3,
                                si_msg_count - EXPECTED_3) ||
            !streql(ip_port, remote_ip_port) ||
            status != O2_REMOTE) {
            printf("FAILURE\n");
            exit(-1);
        }
    } else if (si_msg_count < EXPECTED_5) {
        if (remote_ip_port[0] == 0) {
            strcpy(remote_ip_port, service_name);
        }
        if (!check_service_name(service_name, expected_si_service_3,
                                si_msg_count - EXPECTED_4) ||
            !streql(ip_port, remote_ip_port) ||
            status != O2_FAIL) {
            printf("FAILURE\n");
            exit(-1);
        }
    } else {
        printf("FAILURE\n");
        exit(-1);
    }
    si_msg_count++;
}


int main(int argc, const char * argv[])
{
    printf("Usage: infotest2 [debugflags] "
           "(see o2.h for flags, use a for all)\n");
    if (argc == 2) {
        o2_debug_flags(argv[1]);
        printf("debug flags are: %s\n", argv[1]);
    }
    if (argc > 2) {
        printf("WARNING: infotest2 ignoring extra command line argments\n");
    }

    remote_ip_port[0] = 0; // initialize to empty string meaning "unknown"
    o2_initialize("test");    
    o2_method_new("/_o2/si", "sis", &service_info_handler, NULL, FALSE, TRUE);

    o2_service_new("one");
    for (int i = 0; i < N_ADDRS; i++) {
        char path[100];
        sprintf(path, "/one/benchmark/%d", i);
        o2_method_new(path, "i", &service_one, NULL, FALSE, FALSE);
    }
    
    o2_service_new("server");
    o2_method_new("/server/clockmaster", "", &clockmaster, NULL, FALSE, FALSE);

    o2_send("/one/benchmark/0", 0, "i", 0);
    for (int i = 0; i < 1000; i++) {
        o2_poll();
    }

    // we are the master clock
    o2_clock_set(NULL, NULL);
    o2_send("!server/clockmaster", 0.0, ""); // start polling
    o2_run(100);
    o2_finish();
    sleep(1);
    if (si_msg_count != 15) {
        printf("FAILURE - wrong si_msg_count (%d)\n", si_msg_count);
    } else {
        printf("INFOTEST2 DONE\n");
    }
    return 0;
}
