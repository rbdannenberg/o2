//  unisub.c - subscriber to unipub.c, a test for Unicode in O2
//
//  see unipub.c for details

#include "o2.h"
#include <stdio.h>
#include <stdlib.h>  // atoi
#include <string.h>
#include <assert.h>

// send this many messages followed by -1
int MAX_MSG_COUNT = 200;

char **server_addresses;
int n_addrs = 2;
int use_tcp = false;

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
        o2_sleep(2);
    }
}

    
void client_test(O2msg_data_ptr data, const char *types,
                 O2arg_ptr *argv, int argc, const void *user_data)
{
    if (!running) {
        return;
    }
    assert(argc == 3);
    if (msg_count < 10) {
        printf("client message %d: s=%s S=%s i=%d\n", msg_count, 
               argv[0]->s, argv[1]->S, argv[2]->i32);
    }
    assert(streql(argv[0]->s, "Iñtërnâtiônà£ißætiøn☃😎 "));
    assert(streql(argv[1]->S, "Iñtërnâtiônà£ißætiøn☃😎 "));

    msg_count++;
    if (argv[2]->i32 == -1) {
        assert(msg_count == MAX_MSG_COUNT + 1);
        running = false;
    } else {
        assert(msg_count == argv[2]->i32 + 1);
        int i = msg_count < MAX_MSG_COUNT ? msg_count : -1;
        o2_send_cmd(server_addresses[msg_count % n_addrs], 0, "sSi", 
                "Iñtërnâtiônà£ißætiøn☃😎 ", "Iñtërnâtiônà£ißætiøn☃😎 ", i);
    }
    if (msg_count % 100 == 0) {
        printf("client received %d messages\n", msg_count);
    }
}

static int copy_count = 0;

void copy_sSi(O2msg_data_ptr data, const char *types,
              O2arg_ptr *argv, int argc, const void *user_data)
{
    assert(argc == 3);
    if (copy_count < 5 * n_addrs) { // print the first 5 messages
        printf("copy_sSi got %s s=%s S=%s i=%d\n", data->address,
               argv[0]->s, argv[1]->S, argv[2]->i);
    }
    assert(streql(argv[0]->s, "Iñtërnâtiônà£ißætiøn☃😎 "));
    assert(streql(argv[1]->S, "Iñtërnâtiônà£ißætiøn☃😎 "));
    if (argv[2]->i != -1) {
        assert(argv[2]->i == copy_count);
    }
    copy_count += n_addrs;
}

int list_properties()
{
    assert(o2_services_list() == O2_SUCCESS);
    // find the pub*0 service:
    int pub0 = 0;
    const char *pubname;
    while ((pubname = o2_service_name(pub0))) {
        if (o2_service_type(pub0) != O2_TAP &&
            streql(pubname, "pubIñtërnâtiônà£ißætiøn☃😎 0")) {
            return pub0;
        }
        pub0++;
    }
    printf("Could not find pubIñtërnâtiônà£ißætiøn☃😎0 in services\n");
    assert(false);
    return -1;
}


int main(int argc, const char *argv[])
{
    printf("Usage: unisub [debugflags] [n_addrs]\n"
           "    see o2.h for flags, use a for all, - for none\n"
           "    n_addrs is number of addresses to use, default %d\n", n_addrs);
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

    o2_initialize("Iñtërnâtiônà£ißætiøn☃😎 ");
    
    for (int i = 0; i < n_addrs; i++) {
        char path[128];
        sprintf(path, "/subIñtërnâtiônà£ißætiøn☃😎%d", i);
        o2_service_new(path + 1);
        strcat(path, "/äta");
        o2_method_new(path, "sSi", &client_test, NULL, false, true);
    }
    
    // make one tap before the service
    assert(o2_tap("pubIñtërnâtiônà£ißætiøn☃😎0", 
                  "copyIñtërnâtiônà£ißætiøn☃😎0", 
                  TAP_RELIABLE) == O2_SUCCESS);
    assert(o2_service_new("copyIñtërnâtiônà£ißætiøn☃😎0") == O2_SUCCESS);
    assert(o2_method_new("/copyIñtërnâtiônà£ißætiøn☃😎0/äta", "sSi",
                         &copy_sSi, NULL, false, true) == O2_SUCCESS);

    server_addresses = O2_MALLOCNT(n_addrs, char *);
    for (int i = 0; i < n_addrs; i++) {
        char path[100];
        sprintf(path, "!pubIñtërnâtiônà£ißætiøn☃😎%d/äta", i);
        server_addresses[i] = O2_MALLOCNT(strlen(path), char);
        strcpy(server_addresses[i], path);
    }

    while (o2_status("pubIñtërnâtiônà£ißætiøn☃😎0") < O2_REMOTE) {
        o2_poll();
        o2_sleep(2); // 2ms
    }
    printf("We discovered pubIñtërnâtiônà£ißætiøn☃😎0 sevice.\ntime is %g.\n",
           o2_time_get());
    
    run_for_awhile(1);


    // check properties
    int pub0 = list_properties();

    const char *value = 
            o2_service_getprop(pub0, "attr_Iñtërnâtiônà£ißætiøn☃😎 ");
    assert(streql(value, "value_Iñtërnâtiônà£ißætiøn☃😎 "));
    O2_FREE((char *) value);

    value = o2_service_getprop(pub0, "attr1");
    assert(streql(value, "value1"));
    O2_FREE((char *) value);

    value = o2_service_getprop(pub0, "norwegian");
    assert(streql(value, "Blåbærsyltetøy"));
    O2_FREE((char *) value);

    // search for unicode substrings of values:
    assert(pub0 == o2_service_search(0, "attr_Iñtërnâtiônà£ißætiøn☃😎 ",
                                     "nâtiônà£"));
    assert(pub0 == o2_service_search(0, "norwegian", "æ"));

    assert(o2_services_list_free() == O2_SUCCESS);


    // now install all taps
    for (int i = 0; i < n_addrs; i++) {
        char tappee[128];
        char tapper[128];
        sprintf(tappee, "pubIñtërnâtiônà£ißætiøn☃😎 %d", i);
        sprintf(tapper, "subIñtërnâtiônà£ißætiøn☃😎 %d", i);
        assert(o2_tap(tappee, tapper, TAP_RELIABLE) == O2_SUCCESS);
    }
    // another second to deliver/install taps
    run_for_awhile(1);

    printf("Here we go! ...\ntime is %g.\n", o2_time_get());

    o2_send_cmd("!pubIñtërnâtiônà£ißætiøn☃😎 0/äta", 0, "sSi",
                "Iñtërnâtiônà£ißætiøn☃😎 ", "Iñtërnâtiônà£ißætiøn☃😎 ", 0);
    
    while (running) {
        o2_poll();
        o2_sleep(2); // 2ms
    }
    // we have now sent a message with -1
    // shut down all taps
    for (int i = 0; i < n_addrs; i++) {
        char tappee[128];
        char tapper[128];
        sprintf(tappee, "pubIñtërnâtiônà£ißætiøn☃😎%d", i);
        sprintf(tapper, "subIñtërnâtiônà£ißætiøn☃😎%d", i);
        assert(o2_untap(tappee, tapper) == O2_SUCCESS);
    }
    assert(o2_untap("pubIñtërnâtiônà£ißætiøn☃😎0",
                    "copyIñtërnâtiônà£ißætiøn☃😎0") == O2_SUCCESS);
    
    // publisher removes properties; wait a second for them to disappear
    run_for_awhile(1);

    // check for no properties
    pub0 = list_properties();
    value = o2_service_getprop(pub0, "attr_Iñtërnâtiônà£ißætiøn☃😎 ");
    assert(value == NULL);

    value = o2_service_getprop(pub0, "attr1");
    assert(value == NULL);

    value = o2_service_getprop(pub0, "norwegian");
    assert(value == NULL);
    assert(o2_services_list_free() == O2_SUCCESS);


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
    search_for_non_tapper("copyIñtërnâtiônà£ißætiøn☃😎0", true);

    // another second for unipub to finish checks
    run_for_awhile(1);

    assert(copy_count / n_addrs == (MAX_MSG_COUNT / n_addrs + 1));
    assert(msg_count == MAX_MSG_COUNT + 1);
    for (int i = 0; i < n_addrs; i++) O2_FREE(server_addresses[i]);
    O2_FREE(server_addresses);
    o2_finish();
    printf("CLIENT DONE\n");
    return 0;
}
