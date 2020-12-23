//  hubclient.c - test o2_hub()
//
//  see hubserver.c for details


#include "o2usleep.h"
#include "o2.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "assert.h"
#include <sys/time.h>

#define RETRY 1
#define LOW 2
#define HIGH 3
#define EITHER 4

const char *test_to_string[] = {"nil", "RETRY", "LOW", "HIGH", "EITHER"};


long long current_timestamp() {
    struct timeval te; 
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000;
    return milliseconds;
}

long long start_time = 0;
void start_timer()
{
    start_time = current_timestamp();
}

long elapsed_time()
{
    return current_timestamp() - start_time;
}


#define streql(a, b) (strcmp(a, b) == 0)


char server_pip[O2_IP_LEN];
char server_iip[O2_IP_LEN];
int server_port = -1;


void client_info_handler(o2_msg_data_ptr data, const char *types,
               O2arg_ptr *argv, int argc, const void *user_data)
{
    const char *service_name = argv[0]->s;
    O2status status = (O2status) (argv[1]->i32);
    const char *status_string = o2_status_to_string(status);
    const char *process = argv[2]->s;
    const char *properties = argv[3]->s;
    printf("# client_info_handler called: %s at %s status %s properties %s\n", 
           service_name, process, status_string, properties);
    if (!properties || properties[0]) {
        printf("FAILURE -- expected empty string for properties\n");
        assert(false);
    }
    if (status == O2_UNKNOWN) {
        return;  // service has been removed
    }
    // our purpose is to detect the server and its IP and port
    if (streql(service_name, "server")) {
        o2_parse_name(process, (char *) server_pip, (char *) server_iip,
                      &server_port);
        assert(server_iip[0] != 0);
        assert(server_pip[0] != 0);
    }
}
    

int server_hi_count = 0;

void server_says_hi(o2_msg_data_ptr data, const char *types,
                    O2arg_ptr *argv, int argc, const void *user_data)
{
    printf("#   -> server_says_hi got %s\n", argv[0]->s);
    server_hi_count++;
}

void delay_for(double delay)
{
    // we are not the clock master so o2_time_get() may not always work
    long done = elapsed_time() + (long) (delay * 1000);
    while (elapsed_time() < done) {
        o2_poll();
        usleep(2000);
    }
}


void step(int n, const char *msg)
{
    printf("\n# STEP %d: %s at %ld.\n", n, msg, elapsed_time());
}


void substep(const char *msg)
{
    printf("#   -> %s at %ld.\n", msg, elapsed_time());
}


// This is STEP n, described by msg. Start O2 and enable discovery if
// disc_flag.
//
void startup(int n, const char *msg)
{
    step(n, msg);
    o2_initialize("test");
    o2_service_new("client");
    o2_method_new("/client/hi", "s", &server_says_hi, NULL, false, true);
    o2_method_new("/_o2/si", "siss", &client_info_handler, NULL, false, true);
    substep("O2 is started, waiting for server status");
}    


// wait for discovery of "server" service
//
void wait_for_server(void)
{
    int count = 0;
    while (o2_status("server") < O2_REMOTE) {
        o2_poll();
        usleep(2000); // 2ms
        if (count++ % 1000 == 0) {
            printf("#   -> still waiting for server, "
                   "server status is %s at %ld\n",
                   o2_status_to_string((O2status) o2_status("server")),
                                       elapsed_time());
        }
    }
    assert(server_pip[0]);
    assert(server_iip[0]);
    printf("#   -> server_pip %s server_iip %s server_port %d\n", server_pip,
           server_iip, server_port);
}    


void wait_for_pip(void)
{
    const char *pip;
    const char *iip;
    int my_port;
    while (true) {
        O2err err = o2_get_addresses(&pip, &iip, &my_port);
        assert(!err);
        if (pip[0]) {
            printf("#  -> wait_for_pip got %s\n", pip);
            assert(!streql(pip, "00000000"));
            return;
        }
        printf("#   -> waiting for public IP\n");
        delay_for(0.5);
    }
}


bool my_ipport_is_greater(const char *server_pip, 
                          const char *server_iip, int server_port)
{
    // compare port numbers
    char my_pip[O2_IP_LEN];
    char my_iip[O2_IP_LEN];
    const char *pip;
    const char *iip;
    int my_port;
    O2err err = o2_get_addresses(&pip, &iip, &my_port);
    assert(err == O2_SUCCESS);
    strcpy(my_pip, pip); // copy O2 ip to our variable
    strcpy(my_iip, iip); // copy O2 ip to our variable
    printf("#   -> my_pip %s my_iip %s my_port %d\n", my_pip, my_iip, my_port);
    printf("#   -> server_pip %s server_iip %s server_port %d\n", server_pip,
           server_iip, server_port);
    assert(*server_pip && streql(server_pip, my_pip));
    assert(*server_iip && streql(server_iip, my_iip));
    assert(server_port >= 0 && server_port != my_port);
    return my_port > server_port;
}


int step_11_to_13(bool good, int hi_low)
{
    const char *hi_or_not = (good ? "hi" : "retry");
    if (!good) {
        printf("##########################################################\n");
    }
    step(11, good ? "sending hi to server" : "sending retry to server");
    o2_send_cmd("!server/hi", 0, "s", hi_or_not);
    step(12, "waiting to get hi");
    int count = 0;
    while (o2_status("server") < O2_REMOTE || server_hi_count < 1) {
        o2_poll();
        usleep(2000); // 2ms
        count++;
        if (count % 1000 == 0) {
            substep("waiting for server service");
        }
    }
    printf("#   -> got hi at %ld\n", elapsed_time());
    delay_for(0.5);

    o2_finish();
    step(13, "shutting down");
    return good ? hi_low : RETRY;
}


int test_self_as_hub(int order)
{
    server_pip[0] = 0;
    server_iip[0] = 0;
    startup(4, "test self as hub");
    printf("#   -> order is %s\n", test_to_string[order]);
    step(5, "wait for server");
    wait_for_server();
    delay_for(0.5);
    step(6, "caling o2_hub(NULL)");
    o2_hub(NULL, NULL, 0);
    delay_for(0.5);
    substep("6B: server should shut down now");
    delay_for(0.5);
    step(7, "make sure server is shut down");
    assert(o2_status("server") == O2_FAIL);
    delay_for(0.5);
    step(8, "server expected to reinitialize and call o2_hub()");
    step(9, "wait for server");
    wait_for_server();
    bool client_greater = my_ipport_is_greater(server_pip, server_iip, 
                                               server_port);
    substep(client_greater ? "hubserver (them) needs to connect to hub (us)" :
                             "hub (us) needs to connect to hubserver (them)");
    step(10, "got server, compute LOW/HIGH");
    // compare IP:port's
    int actual = 0;
    if (client_greater) {
        actual = HIGH;
    } else {
        actual = LOW;
    }
    bool good = (order == EITHER || (order == actual));
    printf("#   -> requested order is %s actual is %s\n",
           test_to_string[order], test_to_string[actual]);
    return step_11_to_13(good, actual);
}


int test_other_as_hub(int order)
{
    server_pip[0] = 0;
    server_iip[0] = 0;
    startup(4, "test other as hub");
    printf("#   -> order is %s\n", test_to_string[order]);
    step(5, "wait for server");
    wait_for_server();
    delay_for(0.5);
    step(6, "server stops discovery");
    delay_for(0.5); // flush in flight discovery messages
    substep("6B: shut down client");
    o2_finish();
    delay_for(0.5);
    step(7, "server should test that we are shut down now");

    delay_for(0.5);

    // clear record of server now before hub has a chance to say "hi"
    char server_pip_copy[O2_IP_LEN];
    char server_iip_copy[O2_IP_LEN];
    int server_port_copy = server_port;
    strcpy(server_pip_copy, server_pip);
    strcpy(server_iip_copy, server_iip);
    server_pip[0] = 0; // clear server_ip so we can detect getting it again
    server_iip[0] = 0; // clear server_ip so we can detect getting it again
    server_port = 0;
    
    startup(8, "reinitialize and call o2_hub()");
    char pip_dot[O2_IP_LEN];
    o2_hex_to_dot(server_pip_copy, pip_dot);
    char iip_dot[O2_IP_LEN];
    o2_hex_to_dot(server_iip_copy, iip_dot);
    O2err err = o2_hub(pip_dot, iip_dot, server_port_copy);
    assert(err == O2_SUCCESS);
    wait_for_pip();
    bool client_greater = my_ipport_is_greater(server_pip_copy, 
                             server_iip_copy, server_port_copy);
    substep(client_greater ? "hub (them) needs to connect to hubclient (us)" :
                             "hubclient (us) needs to connect to hub (them)");
    // compare IP:port's
    int actual = 0;
    if (client_greater) {
        actual = HIGH;
    } else {
        actual = LOW;
    }
    bool good = (order == EITHER || (order == actual));
    
    step(9, "wait for server");
    wait_for_server();
    // see if we discovered what we expected
    step(10, "check that we discovered expected server IP:port");
    printf("#   -> hub says server is %s:%s:%x\n", server_pip, server_iip,
           server_port);
    assert(streql(server_pip, server_pip_copy));
    assert(streql(server_iip, server_iip_copy));
    assert(server_port == server_port_copy);
    return step_11_to_13(good, actual);
}


int main(int argc, const char *argv[])
{
    srand(100);
    printf("Usage: hubclient [debugflags]\n"
           "    see o2.h for flags, use a for all, - for none\n");
    if (argc >= 2) {
        o2_debug_flags(argv[1]);
        printf("debug flags are: %s\n", argv[1]);
    }
    if (argc > 2) {
        printf("WARNING: hubclient ignoring extra command line argments\n");
    }
    server_pip[0] = 0;
    server_iip[0] = 0;
    startup(0, "first time to sync up, discover server");
    wait_for_server();
    start_timer();
    printf("\n********************** T0 *************************\n\n");
    step(1, "discovered the server");
    delay_for(0.5);
    step(2, "shut down");
    o2_finish();
    int rslt = test_other_as_hub(EITHER);
    printf("#   -> test_other_as_hub returned %s\n", test_to_string[rslt]);
    step(14, "check for expected LOW/HIGH result");
    assert(rslt == LOW || rslt == HIGH);
    while (true) {
        step(15, "pick who will be hub");
        int r = rand() & 1;
        printf("#   -> rand() gives %d, %s to be hub\n", r,
               r ? "server" : "client");
        step(16, "run a hub test");
        int rslt2 = 0;
        if (r) {
            rslt2 = test_other_as_hub(rslt == LOW ? HIGH : LOW);
        } else {
            rslt2 = test_self_as_hub(rslt);
        }
        step(17, "check result as expected");
        printf("#   -> rslt2 is %s\n", test_to_string[rslt2]);
        if (rslt2 != RETRY) {
            break;
        }
        printf("######################## RETRY ##########################\n");
    }
    printf("######################## FINISH ##########################\n");
    step(18, "finish");
    o2_finish();
    printf("HUBCLIENT DONE\n at %ld\n", elapsed_time());
    return 0;
}
