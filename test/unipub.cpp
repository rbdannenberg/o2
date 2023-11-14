// unipub.c - test for unicode handling across processes
//
// This program works with unisub.cpp. It is a publish/subscribe
// example that tests taps across processes using unicode strings
//
// This process creates 2 services: /pubIñtërnâtiônà£ißætiøn☃😎0
// and /pubIñtërnâtiônà£ißætiøn☃😎1, 
// and methods /pubIñtërnâtiônà£ißætiøn☃😎0/äta and
// /pubIñtërnâtiônà£ißætiøn☃😎1/äta
// The handler just looks for valid messages to the right service.
//
// The subscriber (unisub.c) creates n_addrs=2 services:
// /subIñtërnâtiônà£ißætiøn☃😎0, /subIñtërnâtiônà£ißætiøn☃😎1
// and methods /subIñtërnâtiônà£ißætiøn☃😎0/äta,
// /subIñtërnâtiônà£ißætiøn☃😎1/äta
// It also taps each publish service with a subscribe service as the tapper.
//
// This process also taps /pubIñtërnâtiônà£ißætiøn☃😎0 with
// /subIñtërnâtiônà£ißætiøn☃😎0 and sets up a handler.
//
// To run, up to 1000 messages are sent from unisub.c to /pub services in
// round-robin order (mod n_addrs=2). all services check for expected messages.
//
// After 500 messages, both publisher and subscriber make a services list
// and check all the entries.
//
// After 600 messages, all taps are removed. Since tap propagation is
// potentially asynchronous, keep processing messages if any.
//
// After 1 second, both publisher and subscriber make a services list
// and check all the entries.
//
// Shut down cleanly.
//
// SUMMARY: SERVER                        CLIENT
//         pubIñtërnâtiônà£ißætiøn☃😎?  subIñtërnâtiônà£ißætiøn☃😎?
//              /äta -> server_test           /äta -> copy_sSi
//         subIñtërnâtiônà£ißætiøn☃😎0  copyIñtërnâtiônà£ißætiøn☃😎0
//              /äta -> copy_sSi              /äta -> copy0_sSi
//   CLIENT sends to pub, causing tap messages to go to sub services at
//   both SERVER and CLIENT and pub*0 messages go to copy*0 tap at CLIENT
//
// To further test unicode, we want to put unicode strings in:
//   - messages: as string, symbol 
//       send Iñtërnâtiônà£ißætiøn☃😎 
//   - ensemble name:
//       use Iñtërnâtiônà£ißætiøn☃😎
//   - properties: attribute, value
//       use ;attr_Iñtërnâtiônà£ißætiøn☃😎:value_Iñtërnâtiônà£ißætiøn☃😎;
//           attr1:value1;norwegian:Blåbærsyltetøy;
//     Set the property of service pubIñtërnâtiônà£ißætiøn☃😎0 and test
//     the property values in unisub.cpp

#include "o2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// To put some weight on fast address lookup, we create n_addrs
// different addresses to use.
//
char **client_addresses;
int n_addrs = 2;

// receive this many messages followed by -1:
#define MAX_MSG_COUNT 200

static int msg_count = 0;   // count messages to pub*?
static int copy_count = 0;  // count messages to sub*0
bool running = true;

void search_for_non_tapper(const char *service, bool must_exist)
{
    bool found_it = false;
    int i = 0;
    while (true) { // search for tappee. We have to search everything
        // because if there are taps, there will be multiple matches to
        // the service -- the service properties, and one entry for each
        // tap on the service.
        const char *name = o2_service_name(i);
        if (!name) {
            if (must_exist != found_it) {
                printf("search_for_non_tapper %s must_exist %s\n",
                       service, must_exist ? "true" : "false");
                assert(false);
            }
            return;
        }
        if (streql(name, service)) { // must not show as a tap
            assert(o2_service_type(i) != O2_TAP);
            assert(!o2_service_tapper(i));
            found_it = true;
        }
        i++;
    }
}


void run_for_awhile(double dur)
{
    double now = o2_time_get();
    while (o2_time_get() < now + dur) {
        o2_poll();
        o2_sleep(2);
    }
}

int check_args(O2arg_ptr *argv, int argc)
{
    assert(argc == 3);
    assert(streql(argv[0]->s, "Iñtërnâtiônà£ißætiøn☃😎 "));
    assert(streql(argv[1]->S, "Iñtërnâtiônà£ißætiøn☃😎 "));
    return argv[2]->i;
}


// this is a handler for incoming messages. It simply sends a message
// back to one of the client addresses
//
void server_test(O2msg_data_ptr msg, const char *types,
                 O2arg_ptr *argv, int argc, const void *user_data)
{
    int i = check_args(argv, argc);
    if (msg_count < 10) {
        printf("server message %d is %d\n", msg_count, i);
    }

    if (i == -1) {
        printf("server_test got %s i=%d\n", msg->address, i);
        running = false;
    } else {
        assert(msg_count == i);
    }
    msg_count++;
    if (msg_count  % 100 == 0) {
        printf("server received %d messages\n", msg_count);
    }
}


void copy_sSi(O2msg_data_ptr msg, const char *types,
              O2arg_ptr *argv, int argc, const void *user_data)
{
    int i = check_args(argv, argc);
    if (copy_count < 5 * n_addrs) { // print the first 5 messages
        printf("copy_sSi got %s s=%s S=%s i=%d\n", msg->address, 
               argv[0]->s, argv[1]->S, i);
    }
    if (i != -1) {
        assert(i == copy_count);
    }
    copy_count += n_addrs;
}


int main(int argc, const char *argv[])
{
    printf("Usage: unipub [debugflags] [n_addrs]\n"
           "    see o2.h for flags, use a for all, - for none\n"
           "    n_addrs is number of addresses to use, default %d\n", n_addrs);
    if (argc >= 2) {
        if (argv[1][0] != '-') {
            o2_debug_flags(argv[1]);
            printf("debug flags are: %s\n", argv[1]);
        }
    }
    if (argc >= 3) {
        n_addrs = atoi(argv[2]);
        printf("n_addrs is %d\n", n_addrs);
    }
    if (argc > 3) {
        printf("WARNING: tappub ignoring extra command line argments\n");
    }

    o2_initialize("Iñtërnâtiônà£ißætiøn☃😎 ");
    
    // add our handler for incoming messages to each server address
    for (int i = 0; i < n_addrs; i++) {
        char path[128];
        sprintf(path, "/pubIñtërnâtiônà£ißætiøn☃😎%d", i);
        o2_service_new(path + 1);
        strcat(path, "/äta");
        o2_method_new(path, "sSi", &server_test, NULL, false, true);
    }

    o2_service_set_property("pubIñtërnâtiônà£ißætiøn☃😎0",
                            "attr_Iñtërnâtiônà£ißætiøn☃😎 ",
                            "value_Iñtërnâtiônà£ißætiøn☃😎 ");
    o2_service_set_property("pubIñtërnâtiônà£ißætiøn☃😎0",
                            "attr1", "value1");
    o2_service_set_property("pubIñtërnâtiônà£ißætiøn☃😎0",
                            "norwegian", "Blåbærsyltetøy");

    assert(o2_tap("pubIñtërnâtiônà£ißætiøn☃😎0",
                  "subIñtërnâtiônà£ißætiøn☃😎0", 
                  TAP_RELIABLE) == O2_SUCCESS);
    assert(o2_service_new("subIñtërnâtiônà£ißætiøn☃😎0") == O2_SUCCESS);
    assert(o2_method_new("/subIñtërnâtiônà£ißætiøn☃😎0/äta", "sSi", &copy_sSi,
                         NULL, false, true) == O2_SUCCESS);
    
    // we are the master clock
    o2_clock_set(NULL, NULL);
    
    while (running) {
        o2_poll();
        o2_sleep(2); // 2ms
    }
    // remove our tap
    assert(o2_untap("pubIñtërnâtiônà£ißætiøn☃😎0",
                    "subIñtërnâtiônà£ißætiøn☃😎0") == O2_SUCCESS);
    // remove properties
    o2_service_property_free("pubIñtërnâtiônà£ißætiøn☃😎0",
                            "attr_Iñtërnâtiônà£ißætiøn☃😎 ");
    o2_service_property_free("pubIñtërnâtiônà£ißætiøn☃😎0", "attr1");
    o2_service_property_free("pubIñtërnâtiônà£ißætiøn☃😎0", "norwegian");

    // unisub will wait one second and then check for properties and taps
    // to be gone
    run_for_awhile(1); // allow time for taps to disappear

    // check all taps are gone
    assert(o2_services_list() == O2_SUCCESS);
    // find tapper and tappee as services
    for (int i = 0; i < n_addrs; i++) {
        char tappee[128];
        char tapper[128];
        sprintf(tappee, "pubIñtërnâtiônà£ißætiøn☃😎%d", i);
        sprintf(tapper, "subIñtërnâtiônà£ißætiøn☃😎%d", i);
        search_for_non_tapper(tapper, true);
        search_for_non_tapper(tappee, true); // might as well check
    }

    run_for_awhile(1); // allow time for unisub to finish checks

    // copy_count incremented every n_addrs messages by n_addrs,
    // starting with the first. Note there are actually 
    // MAX_MSG_COUNT+1 messages sent,
    // so the expression for total expected is tricky.
    assert(copy_count / n_addrs == (MAX_MSG_COUNT / n_addrs + 1));
    assert(msg_count == MAX_MSG_COUNT + 1);

    o2_finish();
    printf("SERVER DONE\n");
    return 0;
}
