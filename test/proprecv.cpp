//  proprecv.c -- test properties across pair of processes
//
// Plan:
//    create a service one on propsend
//    create service two on proprecv
//    set an attr/value on propsend
//    get the properties from service one on propsend
//    get the properties from service one on proprecv
//    set an attr/value on proprecv
//    on both proprecv and propsend
//        search for services with attr and exact value
//        search for services with attr and value pattern with :
//        search for services with attr and value pattern with ;
//        search for services with attr and value pattern within
//        search for services with attr and exact value
//    on both proprecv and propsend
//        change value
//    on both proprecv and propsend
//        get the changed value
//    on both proprecv and propsend
//        remove the value
//    on both proprecv and propsend
//        fail to get the value
//    on both proprecv and propsend
//        add several new attr/values 2 3 4 5 6
//    on both proprecv and propsend
//        remove attrs 3 5
//    on both proprecv and propsend
//        get and check full properties string
// Implementation Notes
//     Coordinating the more-or-less asynchronous service property
// updates with the control flow of the test program has been
// difficult. Here's the current strategy:
//
// Testing is done in two ways:
//   1) in the service_info_handler, we check for correct incoming
//      property strings
//   2) in the "main" code, we query and check for properties
// 
// The sequencing relies on sync_peers which takes each process
// through a sequence of states 0, 1, 2, ... as follows:
//   1) sync_peers(i) for some new value of i
//   2) call o2_service_set_property() to set a new value
//   3) sync_peers(i+1) to wait for propagation of properties
//   4) test current property values
// These 4 stages are repeated for each test.
//
// sync_peers() has a race condition with property updates although
// for TCP in the current O2 implementation, all service property 
// updates should be delivered before the next message from sync_peers().
// In any case, in sync_peers(), each process sends to the other and
// waits for the other's message. After the message is received, there
// is an additional 100ms delay to make sure you don't start changing
// state before the other process gets the message you sent and updates
// it's last_sync variable. Thus the only time last_sync can have
// different values in the two processes is while they are in
// sync_peers() (assuming message delivery is never held up for 100ms).


#undef NDEBUG
#include <stdio.h>
#include <stdlib.h> // exit
#include <assert.h>
#include <string.h>
#include "o2.h"

void delay(int ms)
{
    // wait for client service to be discovered
    for (int i = 0; i < ms; i += 2) {
        o2_poll();
        o2_sleep(2); // 2ms
    }
}

int sync_value = -1;
int last_sync = -1;


void service_two(O2msg_data_ptr data, const char *types,
                 O2arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "i") == 0);
    sync_value = argv[0]->i;
    printf("[service_two: /two/sync %d]\n", sync_value);
}

// round-trip with other process for syncrhonization
void sync_peers(int i)
{
    printf("* Sending /one/sync %d, waiting for %d ...\n", i, i);
    fflush(stdout);
    o2_send_cmd("/one/sync", 0, "i", i);
    while (sync_value == -1) {
        delay(10);
    }
    assert(sync_value == i);
    printf("... received /two/sync %d\n", i); fflush(stdout);
    last_sync = i;
    sync_value = -1; // atomic reset to -1 to prepare for next msg
    delay(100); // delay after sync to make sure properties propagate
}


int one; // index for service "one"
int two; // index for service "two"
void lookup()
{
    assert(o2_services_list() == O2_SUCCESS);
    int i = 0;
    one = -1;
    two = -1;
    const char *sn = o2_service_name(0);
    while (sn) {
        if (streql(sn, "one"))  one  = i;
        if (streql(sn, "two"))  two  = i;
        i++;
        sn = o2_service_name(i);
    }
    assert(one > -1);
    assert(two > -1);
}

int si_msg_count = 0;

void service_info_handler(O2msg_data_ptr data, const char *types,
                          O2arg_ptr *argv, int argc, const void *user_data)
{
    const char *service_name = argv[0]->s;
    int status = argv[1]->i32;
    const char *ip_port = argv[2]->s;
    const char *properties = argv[3]->s;
#ifndef O2_NO_DEBUG
    const char *status_string = o2_status_to_string(status);
    printf("## %d service_info_handler called: %s at %s status %s msg %d "
           "properties %s\n", si_msg_count, service_name, ip_port,
           status_string, si_msg_count, properties);
#else
printf("## %d service_info_handler called: %s at %s status %d msg %d "
       "properties %s\n", si_msg_count, service_name, ip_port,
       status, si_msg_count, properties);
#endif
    si_msg_count++;
    if (streql(service_name, "_cs") || streql(service_name, "_o2")) {
        assert(streql(properties, ""));
    } else if (status == O2_UNKNOWN) {
        printf("**** service_info_handler says %s has died. ****\n",
               service_name);
    } else if (streql(service_name, "two")) {
        const char *correct = "";
        if (last_sync == 0) correct = "attr2:value2;";
        else if (last_sync == 2) correct = "attr0:twovalue1two;attr2:value2;";
        else if (last_sync == 4) correct = "attr0:newvalue2;attr2:value2;";
        else if (last_sync == 4) correct = "attr0:newvalue2;attr2:value2;";
        else if (last_sync == 6) correct = "attr2:value2;";
        else if (last_sync == 6) correct = "attr2:value2;";
        else if (last_sync == 8) {
            assert(streql(properties, "attr1:value1;attr2:value2;") ||
                   streql(properties, "attr2:value2;attr1:value1;") ||
                   streql(properties, "attr3:value3;"
                          "attr2:value2;attr1:value1;") ||
                   streql(properties, "attr4:value4;attr3:value3;"
                          "attr2:value2;attr1:value1;") ||
                   streql(properties, "attr5:value5;attr4:value4;"
                          "attr3:value3;attr2:value2;attr1:value1;"));
            return;
        } else if (last_sync == 10) {
            assert(streql(properties, "attr5:value5;attr4:value4;"
                          "attr3:value3;attr2:value2;") ||
                   streql(properties, "attr5:value5;attr4:value4;"
                          "attr2:value2;") ||
                   streql(properties, "attr4:value4;attr2:value2;"));
            return;
        } else if (last_sync == 12) {
            assert(streql(properties, "attr1:\\\\\\;\\\\\\:\\\\\\\\;"
                          "attr4:value4;attr2:value2;") ||
                   streql(properties, "attr2:\\\\\\:value2\\\\\\;;"
                          "attr1:\\\\\\;\\\\\\:\\\\\\\\;attr4:value4;") ||
                   streql(properties, "attr3:val\\\\\\\\\\\\\\\\ue3;attr2:"
                          "\\\\\\:value2\\\\\\;;"
                          "attr1:\\\\\\;\\\\\\:\\\\\\\\;attr4:value4;") ||
                   streql(properties,
                          "attr4:\\\\\\\\\\\\\\\\\\\\\\;\\\\\\:value4;"
                          "attr3:val\\\\\\\\\\\\\\\\ue3;"
                          "attr2:\\\\\\:value2\\\\\\;;"
                          "attr1:\\\\\\;\\\\\\:\\\\\\\\;"));
            return;
        }
        if (!streql(properties, correct)) {
            printf("properties \"%s\" correct \"%s\"\n", properties, correct);
            assert(false);
        }
        return;
    } else if (streql(service_name, "one")) {
        const char *correct = "";
        if (last_sync == 0 || last_sync == 1) correct = "attr1:value1;";
        else if (last_sync == 2) correct =
                                     "attr0:onevalue1one;attr1:value1;";
        else if (last_sync == 4) correct =
                                     "attr0:newvalue1;attr1:value1;";
        else if (last_sync == 6) correct = "attr1:value1;";
        else if (last_sync == 8) {
            assert(streql(properties, "attr1:value1;") ||
                   streql(properties, "attr2:value2;attr1:value1;") ||
                   streql(properties, "attr3:value3;attr2:value2;"
                          "attr1:value1;") ||
                   streql(properties, "attr4:value4;attr3:value3;"
                          "attr2:value2;attr1:value1;") ||
                   streql(properties, "attr5:value5;attr4:value4;"
                          "attr3:value3;attr2:value2;attr1:value1;") ||
                   // we don't handle all permutations, but at least
                   // allow reverse order (generated by websocket bridge)
                   streql(properties, "attr1:value1;attr2:value2;") ||
                   streql(properties, "attr1:value1;attr2:value2;"
                                      "attr3:value3;") ||
                   streql(properties, "attr1:value1;attr2:value2;"
                                      "attr3:value3;attr4:value4;") ||
                   streql(properties, "attr1:value1;attr2:value2;"
                                      "attr3:value3;attr4:value4;attr5:value5;")
                );
            return;
        } else if (last_sync == 10) {
            assert(streql(properties, "attr5:value5;attr4:value4;"
                                      "attr3:value3;attr2:value2;") ||
                   streql(properties, "attr5:value5;attr4:value4;"
                                      "attr2:value2;") ||
                   streql(properties, "attr4:value4;attr2:value2;") ||
                   // we don't handle all permutations, but at least
                   // allow reverse order (generated by websocket bridge)
                   streql(properties, "attr2:value2;attr3:value3;"
                                      "attr4:value4;attr5:value5;") ||
                   streql(properties, "attr2:value2;attr4:value4;"
                                      "attr5:value5;") ||
                   streql(properties, "attr2:value2;attr4:value4;"));
            return;
        } else if (last_sync == 12) {
            assert(streql(properties, "attr1:\\\\\\;\\\\\\:\\\\\\\\;"
                          "attr4:value4;attr2:value2;") ||
                   streql(properties, "attr2:\\\\\\:value2\\\\\\;;"
                          "attr1:\\\\\\;\\\\\\:\\\\\\\\;attr4:value4;") ||
                   streql(properties, "attr3:val\\\\\\\\\\\\\\\\ue3;"
                          "attr2:\\\\\\:value2\\\\\\;;"
                          "attr1:\\\\\\;\\\\\\:\\\\\\\\;"
                          "attr4:value4;") ||
                   streql(properties,
                          "attr4:\\\\\\\\\\\\\\\\\\\\\\;\\\\\\:value4;"
                          "attr3:val\\\\\\\\\\\\\\\\ue3;"
                          "attr2:\\\\\\:value2\\\\\\;;"
                          "attr1:\\\\\\;\\\\\\:\\\\\\\\;") ||
                   // we don't handle all permutations, but at least
                   // allow reverse order (generated by websocket bridge)
                   // not sure what order these should be in. With
                   // propsend.htm, only the last streql test below is used.
                   streql(properties, "attr2:value2;attr4:value4;"
                                      "attr1:\\\\\\;\\\\\\:\\\\\\\\;") ||
                   streql(properties, "attr2:\\\\\\:value2\\\\\\;;"
                          "attr1:\\\\\\;\\\\\\:\\\\\\\\;attr4:value4;") ||
                   streql(properties, "attr3:val\\\\\\\\\\\\\\\\ue3;"
                          "attr2:\\\\\\:value2\\\\\\;;"
                          "attr1:\\\\\\;\\\\\\:\\\\\\\\;"
                          "attr4:value4;") ||
                   streql(properties,
                          "attr1:\\\\\\;\\\\\\:\\\\\\\\;"
                          "attr2:\\\\\\:value2\\\\\\;;"
                          "attr3:val\\\\\\\\\\\\\\\\ue3;"
                          "attr4:\\\\\\\\\\\\\\\\\\\\\\;\\\\\\:value4;"));            
            return;
        }
        if (!streql(properties, correct)) {
            printf("properties \"%s\" correct \"%s\"\n", properties, correct);
            assert(false);
        }
        return;
    } else {
        printf("****** /si properties not checked on this callback *******\n");
    }
}


int main(int argc, const char * argv[])
{
    if (argc == 2) {
        o2_debug_flags(argv[1]);
    }
    if (argc > 2) {
        printf("WARNING: proprecv ignoring extra command line argments\n");
    }
        
    if (o2_initialize("test") != O2_SUCCESS) {
        printf("o2_initialize failed\n");
        exit(1);
    };
    // o2_debug_flags("a");
    o2_method_new("/_o2/si", "siss", &service_info_handler, NULL, false, true);
    o2_service_new("two");
    printf("NOTE: OFFERING SERVICE \"two\" FROM TEST proprecv\n");
    o2_method_new("/two/sync", "i", &service_two, NULL, false, true);
    o2_clock_set(NULL, NULL);

    // wait for client service to be discovered
    while (o2_status("one") < O2_REMOTE) {
        o2_poll();
        o2_sleep(2); // 2ms
    }    
    lookup(); // confirm we have expected services one and two
    assert(o2_service_type(two) == O2_LOCAL);

    const char *pip;
    const char *iip;
    char procname[O2_MAX_PROCNAME_LEN];
    int port;
    O2err err = o2_get_addresses(&pip, &iip, &port);
    assert(err == O2_SUCCESS);
    sprintf(procname, "%s:%s:%04x", pip, iip, port);
    printf("%s == %s?\n", o2_service_process(two), procname);
    assert(streql(o2_service_process(two), "_o2"));
    assert(o2_service_tapper(two) == NULL);
    assert(streql(o2_service_properties(one), ""));
    assert(streql(o2_service_properties(two), ""));

    sync_peers(0);

    // set an attr/value
    assert(o2_service_set_property("bad", "attr0", "value0") == O2_FAIL);
    assert(o2_service_set_property("two", "attr2", "value2") == O2_SUCCESS);
    assert(o2_services_list_free() == O2_SUCCESS);

    sync_peers(1);  // wait for properties info
    
    // get the properties from service 1
    lookup();
    assert(o2_services_list() == O2_SUCCESS);
    printf("one %d props: %s\n", one, o2_service_properties(one));
    assert(streql(o2_service_properties(one), "attr1:value1;"));

    // get empty properties from service 2
    printf("two %d props: %s\n", two, o2_service_properties(two));
    assert(streql(o2_service_properties(two), "attr2:value2;"));
    // get the value from service 2
    const char *gp = o2_service_getprop(two, "attr2");
    assert(streql(gp, "value2"));
    O2_FREE((char *) gp);

    // search for services with attr and value pattern within
    assert(o2_service_search(0, "attr1", "val") == one);
    assert(o2_service_search(0, "attr2", "val") == two);
    
    sync_peers(2);
    // search for services with attr and value pattern with :
    // (will match value1)
    o2_service_set_property("two", "attr0", "twovalue1two");
    assert(o2_services_list_free() == O2_SUCCESS);

    sync_peers(3);
    lookup();
    assert(o2_service_search(0, "attr0", ":value1") == -1);
    assert(o2_service_search(0, "attr0", ":onevalue") == one);
    assert(o2_service_search(0, "attr0", ":twovalue") == two);
    // search for services with attr and value pattern with ;
    assert(o2_service_search(0, "attr0", "value1one;") == one);
    assert(o2_service_search(0, "attr0", "value1two;") == two);
    assert(o2_service_search(0, "attr0", "value1;") == -1);
    // search for services with attr and exact value
    assert(o2_service_search(0, "attr0", ":onevalue1one;") == one);
    assert(o2_service_search(0, "attr0", ":twovalue1two;") == two);
    assert(o2_service_search(0, "attr0", ":value1two;") == -1);

    sync_peers(4);
    // change value
    assert(o2_service_set_property("two", "attr0", "newvalue2") == O2_SUCCESS);
    assert(o2_services_list_free() == O2_SUCCESS);

    sync_peers(5);
    // get the changed value
    lookup();
    gp = o2_service_getprop(one, "attr0");
    assert(streql(gp, "newvalue1"));
    O2_FREE((char *) gp);
    gp = o2_service_getprop(two, "attr0");
    assert(streql(gp, "newvalue2"));
    O2_FREE((char *) gp);

    sync_peers(6);

    // remove the value
    assert(o2_service_property_free("two", "attr0") == O2_SUCCESS);
    assert(o2_services_list_free() == O2_SUCCESS);

    // fail to get the value
    sync_peers(7);
    lookup();
    gp = o2_service_getprop(one, "attr0");
    if (gp) printf("**** Unexpected one's attr0 is %s\n", gp);
    assert(gp == NULL);
    gp = o2_service_getprop(two, "attr0");
    if (gp) printf("**** Unexpected two's attr0 is %s\n", gp);
    assert(gp == NULL);
    gp = o2_service_properties(one);
    assert(streql(gp, "attr1:value1;"));
    gp = o2_service_properties(two);
    assert(streql(gp, "attr2:value2;"));

    sync_peers(8);
    // add several new attr/values 2 3 4 5 6
    assert(o2_service_set_property("two", "attr1", "value1") == O2_SUCCESS);
    assert(o2_service_set_property("two", "attr2", "value2") == O2_SUCCESS);
    assert(o2_service_set_property("two", "attr3", "value3") == O2_SUCCESS);
    assert(o2_service_set_property("two", "attr4", "value4") == O2_SUCCESS);
    assert(o2_service_set_property("two", "attr5", "value5") == O2_SUCCESS);

    // get the values
    sync_peers(9);
    assert(o2_services_list_free() == O2_SUCCESS);
    lookup();
    gp = o2_service_properties(one);
    assert(streql(gp, "attr5:value5;attr4:value4;attr3:value3;"
                      "attr2:value2;attr1:value1;") ||
           streql(gp, "attr1:value1;attr2:value2;attr3:value3;"
                      "attr4:value4;attr5:value5;"));
    gp = o2_service_properties(two);
    assert(streql(gp, "attr5:value5;attr4:value4;attr3:value3;"
                      "attr2:value2;attr1:value1;") ||
           streql(gp, "attr1:value1;attr2:value2;attr3:value3;"
                      "attr4:value4;attr5:value5;"));
    
    gp = o2_service_getprop(one, "attr1");
    assert(streql(gp, "value1"));
    O2_FREE((char *) gp);
    gp = o2_service_getprop(one, "attr2");
    assert(streql(gp, "value2"));
    O2_FREE((char *) gp);
    gp = o2_service_getprop(one, "attr3");
    assert(streql(gp, "value3"));
    O2_FREE((char *) gp);
    gp = o2_service_getprop(one, "attr4");
    assert(streql(gp, "value4"));
    O2_FREE((char *) gp);
    gp = o2_service_getprop(one, "attr5");
    assert(streql(gp, "value5"));
    O2_FREE((char *) gp);

    gp = o2_service_getprop(two, "attr1");
    assert(streql(gp, "value1"));
    O2_FREE((char *) gp);
    gp = o2_service_getprop(two, "attr2");
    assert(streql(gp, "value2"));
    O2_FREE((char *) gp);
    gp = o2_service_getprop(two, "attr3");
    assert(streql(gp, "value3"));
    O2_FREE((char *) gp);
    gp = o2_service_getprop(two, "attr4");
    assert(streql(gp, "value4"));
    O2_FREE((char *) gp);
    gp = o2_service_getprop(two, "attr5");
    assert(streql(gp, "value5"));
    O2_FREE((char *) gp);

    sync_peers(10);
    // remove attrs 1 3 5
    assert(o2_services_list_free() == O2_SUCCESS);
    assert(o2_service_property_free("two", "attr1") == O2_SUCCESS);
    assert(o2_service_property_free("two", "attr3") == O2_SUCCESS);
    assert(o2_service_property_free("two", "attr5") == O2_SUCCESS);
    assert(o2_services_list_free() == O2_SUCCESS);

    // get and check full properties string
    sync_peers(11);
    lookup();
    gp = o2_service_getprop(one, "attr2");
    assert(streql(gp, "value2"));
    O2_FREE((char *) gp);
    gp = o2_service_getprop(one, "attr4");
    assert(streql(gp, "value4"));
    O2_FREE((char *) gp);

    assert(o2_service_getprop(one, "attr1") == NULL);
    assert(o2_service_getprop(one, "attr3") == NULL);
    assert(o2_service_getprop(one, "attr5") == NULL);

    gp = o2_service_getprop(two, "attr2");
    assert(streql(gp, "value2"));
    O2_FREE((char *) gp);
    gp = o2_service_getprop(two, "attr4");
    assert(streql(gp, "value4"));
    O2_FREE((char *) gp);

    assert(o2_service_getprop(two, "attr1") == NULL);
    assert(o2_service_getprop(two, "attr3") == NULL);
    assert(o2_service_getprop(two, "attr5") == NULL);


    assert(o2_services_list_free() == O2_SUCCESS);
    
    sync_peers(12);
    // check escaped chars
    assert(o2_service_set_property("two", "attr1", "\\;\\:\\\\") == O2_SUCCESS);
    assert(o2_service_set_property("two", "attr2", "\\:value2\\;") ==
           O2_SUCCESS);
    assert(o2_service_set_property("two", "attr3", "val\\\\\\\\ue3") ==
           O2_SUCCESS);
    assert(o2_service_set_property("two", "attr4", "\\\\\\\\\\;\\:value4") ==
           O2_SUCCESS);

    sync_peers(13);
    lookup();
    gp = o2_service_getprop(one, "attr1");
    assert(streql(gp, "\\;\\:\\\\"));
    O2_FREE((char *) gp);
    gp = o2_service_getprop(one, "attr2");
    assert(streql(gp, "\\:value2\\;"));
    O2_FREE((char *) gp);
    gp = o2_service_getprop(one, "attr3");
    assert(streql(gp, "val\\\\\\\\ue3"));
    O2_FREE((char *) gp);
    gp = o2_service_getprop(one, "attr4");
    assert(streql(gp, "\\\\\\\\\\;\\:value4"));
    O2_FREE((char *) gp);

    gp = o2_service_getprop(two, "attr1");
    assert(streql(gp, "\\;\\:\\\\"));
    O2_FREE((char *) gp);
    gp = o2_service_getprop(two, "attr2");
    assert(streql(gp, "\\:value2\\;"));
    O2_FREE((char *) gp);
    gp = o2_service_getprop(two, "attr3");
    assert(streql(gp, "val\\\\\\\\ue3"));
    O2_FREE((char *) gp);
    gp = o2_service_getprop(two, "attr4");
    assert(streql(gp, "\\\\\\\\\\;\\:value4"));
    O2_FREE((char *) gp);
    assert(o2_services_list_free() == O2_SUCCESS);
    sync_peers(14);

    o2_finish();
    printf("DONE\n");
    return 0;
}
