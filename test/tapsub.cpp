//  tapsub.c - subscriber to tappub.c, a test for taps acrosss processes
//
//  see tappub.c for details
//
// my services are:
//     subscribe0, subscribe1, subscribe2
//         subscribe0 taps publish0
//         subscribe1 taps publish1
//         subscribe2 taps publish2
//     copy0 (taps publish0)

#include "o2.h"
#include "debug.h"
#include <stdio.h>
#include <stdlib.h>  // atoi
#include <string.h>
#include <assert.h>

// send this many messages followed by -1
int MAX_MSG_COUNT = 200;

char **server_addresses;
int n_addrs = 3;
int use_tcp = false;

int msg_count = 0;
bool running = true;

// search for service. The service must either exist (expected==true)
//    or not exist (expected==false). If it exists, it must not be
//    tapped.
void search_for_non_tapper(const char *service, bool expected)
{
    bool found_it = false;
    int i = 0;
    while (true) { // search for tappee. We have to search everything
        // because if there are taps, there will be multiple matches to
        // the service -- the service properties, and one entry for each
        // tap on the service.
        const char *name = o2_service_name(i);
        if (!name) {
            if (expected != found_it) {
                printf("search_for_non_tapper %s expected %s\n",
                       service, expected ? "true" : "false");
                o2_print_path_tree();
            }
            assert(expected == found_it);
            return;
        }
        if (streql(name, service)) { // must not show as a tap
            int st = o2_service_type(i);
            if (st == O2_TAP) {
                printf("Unexpected that %s has a TAP (%s)\n", service,
                       o2_service_tapper(i));
                o2_print_path_tree();
            }
            assert(st != O2_TAP);
            assert(!o2_service_tapper(i));
            found_it = true;
        }
        i++;
    }
}


void run_for_awhile(double dur)
{
    printf("rfa start %g\n", o2_time_get());
    double now = o2_time_get();
    while (o2_time_get() < now + dur) {
        o2_poll();
        o2_sleep(2);
    }
    printf("rfa stop %g\n", o2_time_get());
}

    
void client_test(O2msg_data_ptr data, const char *types,
                 O2arg_ptr *argv, int argc, const void *user_data)
{
    if (!running) {
        return;
    }
    assert(argc == 1);
    if (msg_count < 10) {
        printf("client message %d is %d\n", msg_count, argv[0]->i32);
    }
    msg_count++;
    if (argv[0]->i32 == -1) {
        assert(msg_count == MAX_MSG_COUNT + 1);
        running = false;
    } else {
        assert(msg_count == argv[0]->i32 + 1);
        int i = msg_count < MAX_MSG_COUNT ? msg_count : -1;
        o2_send_cmd(server_addresses[msg_count % n_addrs], 0, "i", i);
    }
    if (msg_count % 100 == 0) {
        printf("client received %d messages\n", msg_count);
    }
}

static int copy_count = 0;

void copy_i(O2msg_data_ptr data, const char *types,
            O2arg_ptr *argv, int argc, const void *user_data)
{
    assert(argc == 1);
    if (copy_count < 5 * n_addrs) { // print the first 5 messages
        printf("copy_i got %s i=%d\n", data->address, argv[0]->i);
    }
    if (argv[0]->i32 != -1) {
        assert(argv[0]->i == copy_count);
    }
    copy_count += n_addrs;
}


int main(int argc, const char *argv[])
{
    // flush everything no matter what (for getting as much info as possible when
    // there are problems):
    setvbuf (stdout, NULL, _IONBF, BUFSIZ);
    setvbuf (stderr, NULL, _IONBF, BUFSIZ);

    printf("Usage: tapsub [debugflags] [n_addrs]\n"
           "    see o2.h for flags, use a for all, - for none\n"
           "    n_addrs is number of addresses to use, default %d\n",
           n_addrs);
    if (argc >= 2) {
        if (argv[1][0] != '-') {
            o2_debug_flags(argv[1]);
            printf("debug flags are: %s\n", argv[2]);
        }
    }
    if (argc >= 3) {
        n_addrs = atoi(argv[2]);
        printf("n_addrs is %d\n", n_addrs);
    }
    if (argc > 3) {
        printf("WARNING: tapsub ignoring extra command line argments\n");
    }

    o2_initialize("test");
    
    for (int i = 0; i < n_addrs; i++) {
        char path[100];
        sprintf(path, "/subscribe%d", i);
        o2_service_new(path + 1);
        strcat(path, "/i");
        o2_method_new(path, "i", &client_test, NULL, false, true);
    }
    
    // make one tap before the service
    assert(o2_tap("publish0", "copy0", TAP_RELIABLE) == O2_SUCCESS);
    assert(o2_service_new("copy0") == O2_SUCCESS);
    assert(o2_method_new("/copy0/i", "i", &copy_i, NULL, false, true) ==
           O2_SUCCESS);

    server_addresses = O2_MALLOCNT(n_addrs, char *);
    for (int i = 0; i < n_addrs; i++) {
        char path[100];
        sprintf(path, "!publish%d/i", i);
        server_addresses[i] = O2_MALLOCNT(strlen(path), char);
        strcpy(server_addresses[i], path);
    }

    while (o2_status("publish0") < O2_REMOTE) {
        o2_poll();
        o2_sleep(2); // 2ms
    }
    printf("We discovered publish0 sevice.\ntime is %g.\n", o2_time_get());
    
    run_for_awhile(1);

    // now install all taps
    for (int i = 0; i < n_addrs; i++) {
        char tappee[32];
        char tapper[32];
        sprintf(tappee, "publish%d", i);
        sprintf(tapper, "subscribe%d", i);
        assert(o2_tap(tappee, tapper, TAP_RELIABLE) == O2_SUCCESS);
    }
    // another second to deliver/install taps
    run_for_awhile(1);

    printf("Here we go! ...\ntime is %g.\n", o2_time_get());

    o2_send_cmd("!publish0/i", 0, "i", 0);
    
    while (running && msg_count < 500) {
        o2_poll();
        o2_sleep(2); // 2ms
    }

    // we have now sent a message with i=500
    printf("Finished %d messages at %g\n", msg_count, o2_time_get());
    // shut down all taps
    for (int i = 0; i < n_addrs; i++) {
        char tappee[32];
        char tapper[32];
        sprintf(tappee, "publish%d", i);
        sprintf(tapper, "subscribe%d", i);
        assert(o2_untap(tappee, tapper) == O2_SUCCESS);
    }
    assert(o2_untap("publish0", "copy0") == O2_SUCCESS);
    
    // run for a second and check lists
    run_for_awhile(1);
    
    assert(o2_services_list() == O2_SUCCESS);
    // find tapper and tappee as services
    for (int i = 0; i < n_addrs; i++) {
        char tappee[32];
        char tapper[32];
        sprintf(tappee, "publish%d", i);
        sprintf(tapper, "subscribe%d", i);
        search_for_non_tapper(tapper, true);
        // Both processes stop about the same time, so processN is still
        // around:
        search_for_non_tapper(tappee, true);
    }
    search_for_non_tapper("copy0", true);

    // another second to deliver shutdown message to tappub
    run_for_awhile(1);

    assert(msg_count == MAX_MSG_COUNT + 1);
    assert(copy_count / n_addrs == (MAX_MSG_COUNT / n_addrs + 1));
    for (int i = 0; i < n_addrs; i++) O2_FREE(server_addresses[i]);
    O2_FREE(server_addresses);
    o2_finish();
    printf("CLIENT DONE\n");
    return 0;
}
