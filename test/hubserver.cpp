// hubserver.c - test hub discovery
//
// use with hubclient.c. The names "client" and "server" are arbitrary and
// meaningless. In this test, one process becomes the hub and the other is
// the client of the hub. This happens twice: Once with the high IP:port
// address as hub and once with the low IP:port address as hub, since the
// protocol is slightly different because TCP connections are asymetric
// (one side connects, the other side accepts). We test it both ways.
//
// To test both ways, first test either way. Report success. Then pick
// who will be hub at random and keep testing until the right order comes
// up. Functions test_other_as_hub() and test_self_as_hub() have parameters
//   EITHER: either self of other can have high IP:port address;
//   HIGH: self should have high IP:port address;
//   LOW: other should have high IP:port address;
// Both functions return LOW if the low IP:port was the hub, or
// HIGH if the high IP:port was the hub, or RETRY if the ordering was not
// the one we wanted to test.
//
// start_timer() is set after first discovery, so times will pretty closely
// match throughout execution even when client and server stop and restart
// o2. This was done to debug the complex interaction sequence.
//
// *** ORDINARY DISCOVERY TO SYNCHRONIZE ***
// STEP 1 Start server and client
//        Use ordinary discovery for client to discover this server at time t.
// STEP 2 All shut down.
// STEP 3 Server calls test_self_as_hub(EITHER)
//        Client calls test_other_as_hub(EITHER)
//
// *** TEST WITH LOW ADDRESS AS HUB ***
//         test_self_as_hub()  and  test_other_as_hub()
//         |                        |
//         V                        V
// STEP 4  | restart.               | restart.
// STEP 5  | wait for other.        | wait for other.
// WAIT 0.5s to make sure both client and server get other IP:port
// STEP 6  | call o2_hub(NULL) to stop discovery and become hub
// WAIT 0.5s (flush in-flight discovery messages)
// STEP 6B                          | shut down
// WAIT 0.5s
// STEP 7  | make sure other is shut down
// WAIT 0.5s
// STEP 8  |                        | call o2_hub(other IP:port)
//                                  | reinitialize. If EITHER or
//                                  |   (HIGH and other IP:port is higher) or
//                                  |   (LOW and other IP:port is lower) then
//                                  |   mode = "hi" else mode = retry
// STEP 9  | wait for other         | wait for other
// STEP 10 | compute LOW or HIGH    | check for expected client IP:port
// STEP 11 | send hi                | send mode (hi or retry)
// STEP 12 | wait for hi or retry   | wait for hi
// WAIT 0.5s
// STEP 13 | shut down, return      | shut down, return
//         |   LOW, HIGH, or RETRY  |   LOW, HIGH, or RETRY
//
// STEP 14 check result is LOW or HIGH
// STEP 15 pick who will be hub at random
// STEP 16 call either test_self_as_hub() or test_other_as_hub()
// STEP 17 check result is as expected. If RETRY, repeat STEP 15
// FINISH


#include "o2.h"
#include "hostip.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "assert.h"
#ifndef WIN32
#include <sys/time.h>
#else
#include <windows.h>
#include <timeapi.h>
#endif

#define RETRY 1
#define LOW 2
#define HIGH 3
#define EITHER 4

const char *test_to_string[] = {"nil", "RETRY", "LOW", "HIGH", "EITHER"};


#ifdef WIN32
long long current_timestamp()
{
    return timeGetTime();
}
#else
long long current_timestamp()
{
    struct timeval te;
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec * 1000LL + te.tv_usec / 1000;
    return milliseconds;
}
#endif

long long start_time = 0;
void start_timer()
{
    start_time = current_timestamp();
}

long elapsed_time()
{
    return (long) (current_timestamp() - start_time);
}


char client_pip[O2N_IP_LEN];
char client_iip[O2N_IP_LEN];
int client_port = -1;

void service_info_handler(O2msg_data_ptr data, const char *types,
                O2arg_ptr *argv, int argc, const void *user_data)
{
    const char *service_name = argv[0]->s;
    O2status status = (O2status) argv[1]->i32;
    const char *process = argv[2]->s;
    const char *properties = argv[3]->s;
#ifndef O2_NO_DEBUG
    const char *status_string = o2_status_to_string(status);
    printf("# service_info_handler called: %s at %s status %s properties %s\n",
           service_name, process, status_string, properties);
#else
    printf("# service_info_handler called: %s at %s status %d properties %s\n",
           service_name, process, status, properties);
#endif
    if (!properties || properties[0]) {
        printf("FAILURE -- expected empty string for properties\n");
        assert(false);
    }
    if (status == O2_UNKNOWN) {
        return;  // service has been removed
    }
    // our purpose is to detect the client and its IP and port
    if (streql(service_name, "client")) {
        o2_parse_name(process, client_pip, client_iip, &client_port);
        assert(client_pip[0] != 0);
    }
}
    

int client_hi_count = 0;

void client_says_hi(O2msg_data_ptr data, const char *types,
                    O2arg_ptr *argv, int argc, const void *user_data)
{
    printf("#   -> client_says_hi got %s\n", argv[0]->s);
    client_hi_count++;
}


void delay_for(double delay)
{
    // we are not the clock master so o2_time_get() may not always work
    long done = elapsed_time() + (long) (delay * 1000);
    while (elapsed_time() < done) {
        o2_poll();
        o2_sleep(2);
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
    O2err err = o2_initialize("test");
    if (err) {
        printf("ERROR %s in o2_initialize()\n", o2_error_to_string(err));
    }
    assert(!err);
    o2_service_new("server");
    o2_method_new("/server/hi", "s", &client_says_hi, NULL, false, true);
    o2_method_new("/_o2/si", "siss", &service_info_handler, NULL, false, true);
    o2_clock_set(NULL, NULL);  // always be the clock reference
    substep("O2 is started, waiting for client status");
}    


// wait for discovery of "client" service
//
void wait_for_client()
{
    int count = 0;
    while (o2_status("client") < O2_REMOTE || !client_pip[0]) {
        o2_poll();
        o2_sleep(2); // 2ms
        if (count++ % 1000 == 0) {
#ifndef O2_NO_DEBUG
            printf("#   -> still waiting for client, "
                   "client status is %s at %ld\n",
                   o2_status_to_string((O2status) o2_status("client")),
                   elapsed_time());
#else
            printf("#   -> still waiting for client, "
                   "client status is %d at %ld\n", o2_status("client"),
                   elapsed_time());
#endif
        }
    }
    assert(client_pip[0]);
    printf("#   -> client_pip %s client_iip %s, client_port %d\n",
           client_pip, client_iip, client_port);
}    


void wait_for_pip()
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
        printf("#  -> waiting for public IP\n");
        delay_for(0.5);
    }
}


bool my_ipport_is_greater(const char *client_pip, 
                          const char *client_iip, int client_port)
{
    char my_pip[O2N_IP_LEN];
    char my_iip[O2N_IP_LEN];
    const char *pip;
    const char *iip;
    int my_port;
    O2err err = o2_get_addresses(&pip, &iip, &my_port);
    assert(err == O2_SUCCESS);
    strcpy(my_pip, pip); // copy O2 public ip to our variable
    strcpy(my_iip, iip); // copy O2 internal ip to our variable
    printf("#   -> my_pip %s my_iip %s my_port %d\n", my_pip, my_iip, my_port);
    printf("#   -> client_pip %s client_iip %s, client_port %d\n",
           client_pip, client_iip, client_port);
    assert(*client_pip && streql(client_pip, my_pip) &&
           streql(client_iip, my_iip));
    assert(client_port >= 0 && client_port != my_port);
    return my_port > client_port;
}


int step_11_to_13(bool good, int hi_low)
{
    const char *hi_or_not = (good ? "hi" : "retry");
    if (!good) {
        printf("##########################################################\n");
    }
    step(11, good ? "sending hi to client" : "sending retry to client");
    o2_send_cmd("!client/hi", 0, "s", hi_or_not);
    step(12, "waiting to get hi");
    int count = 0;
    while (o2_status("client") < O2_REMOTE || client_hi_count < 1) {
        o2_poll();
        o2_sleep(2); // 2ms
        count++;
        if (count % 1000 == 0) {
            substep("waiting for client service");
        }
    }
    printf("#   -> got hi at %ld\n", elapsed_time());
    delay_for(0.5);

    o2_finish();
    step(13, "shutting down");
    return good ? hi_low : RETRY;
}

#ifndef O2_NO_HUB
int test_self_as_hub(int order)
{
    client_pip[0] = 0;
    client_iip[0] = 0;
    startup(4, "test self as hub");
    printf("#   -> order is %s\n", test_to_string[order]);
    step(5, "wait for client");
    wait_for_client();
    delay_for(0.5);
    step(6, "caling o2_hub(NULL)");
    o2_hub(NULL, NULL, 0, 0);
    delay_for(0.5);
    substep("6B: server should shut down now");
    delay_for(0.5);
    step(7, "make sure client is shut down");
    assert(o2_status("client") == O2_FAIL);
    delay_for(0.5);
    step(8, "client expected to reinitialize and call o2_hub()");
    step(9, "wait for client");
    wait_for_client();
    step(10, "got client, compute LOW/HIGH");
    bool server_greater = my_ipport_is_greater(client_pip, client_iip, client_port);
    substep(server_greater ? "hubclient (them) needs to connect to hub (us)" :
                             "hub (us) need to connect to hubclient (them)");
    // compare IP:port's
    int actual = 0;
    if (server_greater) {
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
    client_pip[0] = 0;
    client_iip[0] = 0;
    startup(4, "test other as hub");
    printf("#   -> order is %s\n", test_to_string[order]);
    step(5, "wait for client");
    wait_for_client();
    delay_for(0.5);
    step(6, "client stops discovery");
    delay_for(0.5); // flush in flight discovery messages
    substep("6B: shutting down server");
    o2_finish();
    delay_for(0.5);
    step(7, "client should test that we are shut down now");

    delay_for(0.5);

    // clear record of client now before hub has a chance to say "hi"
    char client_pip_copy[O2N_IP_LEN];
    char client_iip_copy[O2N_IP_LEN];
    int client_port_copy = client_port;
    strcpy(client_pip_copy, client_pip);
    strcpy(client_iip_copy, client_iip);
    client_pip[0] = 0; // clear client_ip so we can detect getting it again
    client_iip[0] = 0; // clear client_ip so we can detect getting it again
    client_port = 0;

    startup(8, "reinitialize and call o2_hub()");
    char pip_dot[O2N_IP_LEN];
    o2_hex_to_dot(client_pip_copy, pip_dot);
    char iip_dot[O2N_IP_LEN];
    o2_hex_to_dot(client_iip_copy, iip_dot);
    O2err err = o2_hub(pip_dot, iip_dot, client_port_copy, client_port_copy);

    assert(err == O2_SUCCESS);
    wait_for_pip();
    bool server_greater = my_ipport_is_greater(client_pip_copy, 
                             client_iip_copy, client_port_copy);
    substep(server_greater ? "They (hub) need to connect to us (hubserver)" :
                             "We (hubserver) need to connect to them (hub)");
    // compare IP:port's
    int actual = 0;
    if (server_greater) {
        actual = HIGH;
    } else {
        actual = LOW;
    }
    bool good = (order == EITHER || (order == actual));

    step(9, "wait for client");  // waits for client status and client_pip from /si
    wait_for_client();
    // see if we discovered what we expected
    step(10, "check that we discovered expected client IP:port");
    printf("#   -> hub says client is %s:%s:%04x\n", client_pip, client_iip,
           client_port);
    assert(streql(client_pip, client_pip_copy));
    assert(streql(client_iip, client_iip_copy));
    assert(client_port == client_port_copy);
    return step_11_to_13(good, actual);
}
#endif

int main(int argc, const char *argv[])
{
    srand(100);
    printf("Usage: hubserver [debugflags]\n"
           "    see o2.h for flags, use a for all, - for none\n");
    if (argc >= 2) {
        o2_debug_flags(argv[1]);
        printf("debug flags are: %s\n", argv[1]);
    }
    if (argc > 2) {
        printf("WARNING: hubserver ignoring extra command line argments\n");
    }
#ifndef O2_NO_HUB
    client_pip[0] = 0;
    client_iip[0] = 0;
    startup(0, "first time to sync up, discover client");
    wait_for_client();
    start_timer();
    printf("\n********************** T0 *************************\n");
    step(1, "discovered the client");
    delay_for(0.5);
    step(2, "shut down");
    o2_finish();
    int rslt = test_self_as_hub(EITHER);
    printf("#   -> test_self_as_hub returned %s\n", test_to_string[rslt]);
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
            rslt2 = test_self_as_hub(rslt == LOW ? HIGH : LOW);
        } else {
            rslt2 = test_other_as_hub(rslt);
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
    printf("At time %ld, \n", elapsed_time());
#else
    printf("O2_NO_HUB defined, so there are no tests that can fail\n");
#endif
    printf("HUBSERVER DONE\n");
    return 0;
}
