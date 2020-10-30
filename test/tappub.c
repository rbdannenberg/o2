//  tappub.c - test for taps across processes
//
//  This program works with tapsub.c. It is a publish/subscribe
//  example that tests taps across processes.
//
//  This process creates n_addr services: /publish0, /publish1, /publish2 ...
//  and methods /publish0/i, /publish1/i, /publish2/i ...
//  The handler just looks for valid messages to the right service.
//
//  The subscriber (tapsub.c) creates n_addr services: /subscribe0,
//  /subscribe1, /subscribe2 ..., and methods /subscribe0/i, /subscribe1/i,
//  /subscribe2/i .... It also taps each publish service with a subscribe
//  service as the tapper.
//
//  This process also taps /publish0 with /subscribe0 and sets up a handler.
//
//  To run, up to 1000 messages are sent from tapsub.c to /publish services in
//  round-robin order (mod n_addr). all services check for expected messages.
//
//  After 500 messages, both publisher and subscriber make a services list
//  and check all the entries.
//
//  After 600 messages, all taps are removed. Since tap propagation is
//  potentially asynchronous, keep processing messages if any.
//
//  After 1 second, both publisher and subscriber make a services list
//  and check all the entries.
//
//  Shut down cleanly.

#ifdef __GNUC__
// define usleep:
#define _XOPEN_SOURCE 500
#define _POSIX_C_SOURCE 200112L
#endif

#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"
#define streql(a, b) (strcmp(a, b) == 0)

#ifdef WIN32
#include "usleep.h" // special windows implementation of sleep/usleep
#else
#include <unistd.h>
#endif


// To put some weight on fast address lookup, we create n_addrs
// different addresses to use.
//
char **client_addresses;
int n_addrs = 3;

#define MAX_MSG_COUNT 1000

int msg_count = 0;
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
        usleep(2000);
    }
}



// this is a handler for incoming messages. It simply sends a message
// back to one of the client addresses
//
void server_test(o2_msg_data_ptr msg, const char *types,
                 o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(argc == 1);
    if (msg_count < 10) {
        printf("server message %d is %d\n", msg_count, argv[0]->i32);
    }

    if (argv[0]->i32 >= 500) {
        printf("server_test got %s i=%d\n", msg->address, argv[0]->i);
        running = false;
    } else {
        assert(msg_count == argv[0]->i32);
    }
    msg_count++;
    if (msg_count  % 100 == 0) {
        printf("server received %d messages\n", msg_count);
    }
}

static int copy_count = 0;

void copy_i(o2_msg_data_ptr msg, const char *types,
                  o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(argc == 1);
    if (copy_count < 5 * n_addrs) { // print the first 5 messages
        printf("copy_i got %s i=%d\n", msg->address, argv[0]->i);
    }
    if (argv[0]->i != -1) {
        assert(argv[0]->i == copy_count);
    }
    copy_count += n_addrs;
}


int main(int argc, const char *argv[])
{
    printf("Usage: tappub [debugflags] [n_addrs]\n"
           "    see o2.h for flags, use a for all, - for none\n"
           "    n_addrs is number of addresses to use, default 3\n");
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

    o2_initialize("test");
    
    // add our handler for incoming messages to each server address
    for (int i = 0; i < n_addrs; i++) {
        char path[100];
        sprintf(path, "/publish%d", i);
        o2_service_new(path + 1);
        strcat(path, "/i");
        o2_method_new(path, "i", &server_test, NULL, false, true);
    }

    assert(o2_tap("publish0", "copy0") == O2_SUCCESS);
    assert(o2_service_new("copy0") == O2_SUCCESS);
    assert(o2_method_new("/copy0/i", "i", &copy_i,
                         NULL, false, true) == O2_SUCCESS);
    
    // we are the master clock
    o2_clock_set(NULL, NULL);
    
    // wait for client service to be discovered.
    while (o2_status("subscribe0") < O2_REMOTE) {
        o2_poll();
        usleep(2000); // 2ms
    }
    
    printf("We discovered the client at time %g.\n", o2_time_get());
    
    while (running) {
        o2_poll();
        usleep(2000); // 2ms
    }
    // remove our tap
    assert(o2_untap("publish0", "copy0") == O2_SUCCESS);

    run_for_awhile(1); // allow time for taps to disappear

    // check all taps are gone
    assert(o2_services_list() == O2_SUCCESS);
    // find tapper and tappee as services
    for (int i = 0; i < n_addrs; i++) {
        char tappee[32];
        char tapper[32];
        sprintf(tappee, "publish%d", i);
        sprintf(tapper, "subscribe%d", i);
        search_for_non_tapper(tapper, true);
        search_for_non_tapper(tappee, true); // might as well check
    }
    search_for_non_tapper("copy0", true);

    // With modulo arithmetick so copy_i only gets 1/n_addrs messages, let's
    // just check that it got most of the messages and not test for an exact count.
    assert(copy_count >= 500 / n_addrs - 1);
    assert(msg_count >= 500 - 1);

    o2_finish();
    printf("SERVER DONE\n");
    return 0;
}
