//  O2_NOT_INITIALIZED - o2_clock_Set - 496
// if o2_clock_swt is called before initializing o2_application it will throw O2_NOT_INITIALIZED error.

#include <stdio.h>
#include "o2.h"
#include "string.h"

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

void service_two(o2_msg_data_ptr data, const char *types,
                 o2_arg_ptr *argv, int argc, void *user_data)
{
    printf("Service two received a message\n");
}

#define streql(a, b) (!strcmp(a, b))

#define FIRST_COUNT 5
int si_msg_count = 0;
char *expected_si_service_first[] = {"_o2", "ip:port", "one", "two", "_cs"};
int expected_si_status_first[] = {O2_LOCAL_NOTIME, O2_LOCAL_NOTIME,
        O2_LOCAL_NOTIME, O2_LOCAL_NOTIME, O2_LOCAL};
char *expected_si_service_later[] = {"_o2", "ip:port", "one", "two"};

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
    char my_ip_port[32];
    if (!my_ip) my_ip = "none";
    sprintf(my_ip_port, "%s:%d", my_ip, my_port);
    // the first 5 /_o2/si messages are listed in expected_si_service_first,
    // where "ip:port" is replaced by the real ip:port string.
    if (si_msg_count < FIRST_COUNT) {
        char *expected_service = expected_si_service_first[si_msg_count];
        if (streql(expected_service, "ip:port")) {
            expected_service = my_ip_port;
        }
        if (!streql(expected_service, service_name) ||
            !streql(my_ip_port, ip_port) ||
            status != expected_si_status_first[si_msg_count]) {
            printf("FAILURE\n");
            exit(-1);
        }
    // 9 messages are expected, so si_msg_count will go from 0 through 8
    } else if (si_msg_count > 8) {
        printf("FAILURE\n");
        exit(-1);
    // after the first 5 messages, the clock becomes master, and we expect
    // 4 more messages in some order and we become O2_LOCAL. The services
    // are in expected_si_service_later[], which we search and remove from
    // to allow for any order of notification (order could vary because
    // O2 will enumerate what's in a hash table.
    } else if (si_msg_count >= FIRST_COUNT) {
        int found_it = 0;
        int i;
        for (i = 0; i < 4; i++) {
            char *expected_service = expected_si_service_later[i];
            if (streql(expected_service, "ip:port")) {
                expected_service = my_ip_port;
            }
            if (streql(expected_service, service_name) &&
                status == O2_LOCAL &&
                streql(ip_port, my_ip_port)) {
                expected_si_service_later[i] = "";
                found_it = 1;
            }
        }
        if (!found_it) {
            printf("FAILURE\n");
            exit(-1);
        }
    }
    si_msg_count++;
}


int main(int argc, const char * argv[])
{
    // o2_debug_flags("a");
   int var = o2_clock_set(NULL, NULL); //aded to check
   printf("%d \n",var);	
    o2_initialize("test");    
  /*  o2_method_new("/_o2/si", "sis", &service_info_handler, NULL, FALSE, TRUE);

    o2_service_new("one");
    for (int i = 0; i < N_ADDRS; i++) {
        char path[100];
        sprintf(path, "/one/benchmark/%d", i);
        o2_method_new(path, "i", &service_one, NULL, FALSE, FALSE);
    }
    
    o2_service_new("two");
    for (int i = 0; i < N_ADDRS; i++) {
        char path[100];
        sprintf(path, "/two/benchmark/%d", i);
        o2_method_new(path, "i", &service_two, NULL, FALSE, FALSE);
    }

    o2_send("/one/benchmark/0", 0, "i", 0);
    for (int i = 0; i < 1000; i++) {
        o2_poll();
    }

    o2_clock_set(NULL, NULL);
    for (int i = 0; i < 1000; i++) {
        o2_poll();
    }

    o2_finish();
    if (si_msg_count != 9) {
        printf("FAILURE - wrong si_msg_count (%d)\n", si_msg_count);
    } else {
        printf("DONE\n");
    }*/
    return 0;
}
