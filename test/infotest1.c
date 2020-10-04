//  infotest1.c -- test if we get info via /_o2/si
//

#include <stdio.h>
#include "o2.h"
#include "string.h"

#define N_ADDRS 10
#define EXPECTED_COUNT 6

int fail_and_exit = false;

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

void service_two(o2_msg_data_ptr data, const char *types,
                 o2_arg_ptr *argv, int argc, void *user_data)
{
    printf("Service two received a message\n");
}

#define streql(a, b) (!strcmp(a, b))

#define FIRST_COUNT 3
int si_msg_count = 0;
char *expected_si_service_first[] = {"one", "two", "_cs"};
int expected_si_status_first[] = {
        O2_LOCAL_NOTIME, O2_LOCAL_NOTIME, O2_LOCAL};
char *expected_si_service_later[] = {"_o2", "one", "two"};

void service_info_handler(o2_msg_data_ptr data, const char *types,
                 o2_arg_ptr *argv, int argc, void *user_data)
{
    const char *service_name = argv[0]->s;
    int status = argv[1]->i32;
    const char *status_string = status_to_string(status);
    const char *process = argv[2]->s;
    const char *properties = argv[3]->s;
    printf("service_info_handler called: %s at %s status %s properties %s\n", 
           service_name, process, status_string, properties);
    if (!properties || properties[0]) {
        printf("FAILURE -- expected empty string for properties\n");
    }
    // ***** this check is not really relevant anymore because we
    // ***** do not use the ip:port name (now we use _o2 instead)
    // ***** but I left this check here because it is not being
    // ***** tested anywhere else -- why not?
    // here are 2 ways to get the IP:Port name of this process:
    // (1) construct from IP string and Port number
    const char *my_ip = NULL;
    int my_port = -1;
    o2_get_address(&my_ip, &my_port);
    char my_ip_port[O2_MAX_PROCNAME_LEN];
    if (!my_ip) my_ip = "none";
    snprintf(my_ip_port, O2_MAX_PROCNAME_LEN, "%s:%d", my_ip, my_port);

    // (2) get the name from o2_context (now part of O2 API):
    const char *o2_ip_port = o2_get_ip_port_string();

    // make sure two methods agree; just a sanity check
    if (!streql(my_ip_port, o2_ip_port)) {
        printf("FAILURE -- problem with naming IP and Port for process\n");
        fail_and_exit = true;
        return;
    }
    // **** END OF o2_get_ip_port_string() AND o2_get_address() TESTS

    // the first 3 /_o2/si messages are listed in expected_si_service_first
    if (si_msg_count < FIRST_COUNT) {
        char *expected_service = expected_si_service_first[si_msg_count];
        if (!streql(expected_service, service_name) ||
            !streql(process, "_o2") ||
            status != expected_si_status_first[si_msg_count]) {
            printf("FAILURE: unexpected service_name %s\n", service_name);
            fail_and_exit = true;
        }
    // EXPECTED_COUNT messages are expected, so si_msg_count will go from
    //  0 through EXPECTED_COUNT - 1
    } else if (si_msg_count >= EXPECTED_COUNT) {
        printf("FAILURE: si_msg_count >= EXPECTED_COUNT\n");
        fail_and_exit = true;
    // after the first 3 messages, the clock becomes master, and we expect
    // 3 more messages in some order and we become O2_LOCAL. The services
    // are in expected_si_service_later[], which we search and remove from
    // to allow for any order of notification (order could vary because
    // O2 will enumerate what's in a hash table.
    } else if (si_msg_count >= FIRST_COUNT) {
        int found_it = 0;
        int i;
        for (i = 0; i < 3; i++) {
            char *expected_service = expected_si_service_later[i];
            if (streql(expected_service, service_name) &&
                status == O2_LOCAL &&
                streql(process, "_o2")) {
                expected_si_service_later[i] = "";
                found_it = 1;
            }
        }
        if (!found_it) {
            printf("FAILURE: !found_it, service_name %s\n", service_name);
            fail_and_exit = true;
        }
    }
    si_msg_count++;
}


int main(int argc, const char * argv[])
{
    // o2_debug_flags("a");
    printf("Usage: infotest1 flags\n");
    if (argc == 2) {
        printf("Calling o2_debug_flags(\"%s\")\n", argv[1]);
        o2_debug_flags(argv[1]);
    }
    o2_initialize("test");    
    o2_method_new("/_o2/si", "siss", &service_info_handler, NULL, false, true);

    o2_service_new("one");
    for (int i = 0; i < N_ADDRS; i++) {
        char path[100];
        sprintf(path, "/one/benchmark/%d", i);
        o2_method_new(path, "i", &service_one, NULL, false, false);
    }
    
    o2_service_new("two");
    for (int i = 0; i < N_ADDRS; i++) {
        char path[100];
        sprintf(path, "/two/benchmark/%d", i);
        o2_method_new(path, "i", &service_two, NULL, false, false);
    }

    o2_send("/one/benchmark/0", 0, "i", 0);
    for (int i = 0; i < 1000; i++) {
        o2_poll();
    }

    o2_clock_set(NULL, NULL);
    for (int i = 0; i < 1000; i++) {
        o2_poll();
        if (fail_and_exit) break;
    }

    o2_finish();
    if (si_msg_count != EXPECTED_COUNT) {
        printf("FAILURE - wrong si_msg_count (%d), expected %d\n",
               si_msg_count, EXPECTED_COUNT);
    } else if (!fail_and_exit) {
        printf("DONE\n");
    }
    return 0;
}
