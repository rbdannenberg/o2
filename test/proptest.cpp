//  proptest.c -- test properties test on local services
//
// Plan:
//    create a couple of services
//    set an attr/value
//    get the properties from service 1
//    get empty properties from service 2
//    get the value from service 1
//    fail to get the value from service 2
//    search for services with attr and exact value
//    search for services with attr and value pattern with :
//    search for services with attr and value pattern with ;
//    search for services with attr and value pattern within
//    change value
//    get the changed value
//    remove the value
//    fail to get the value
//    add several new attr/values 2 3 4 5 6
//    remove attrs 3 5
//    get and check full properties string


#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "o2.h"


#define N_ADDRS 20

#define MAX_MESSAGES 50000

int s = 0;
int w = 1;

void service_one(o2_msg_data_ptr data, const char *types,
                 O2arg_ptr *argv, int argc, void *user_data)
{
    char p[100];
    sprintf(p, "/two/benchmark/%d", s % N_ADDRS);
    if (s < MAX_MESSAGES) {
        o2_send(p, 0, "i", s);
    }
    if (s % 10000 == 0) {
        printf("Service one received %d messages\n", s);
    }
    s++;
}

void service_two(o2_msg_data_ptr data, const char *types,
                 O2arg_ptr *argv, int argc, void *user_data)
{
    char p[100];
    sprintf(p, "/one/benchmark/%d", w % N_ADDRS);
    if (w < MAX_MESSAGES) {
        o2_send(p, 0, "i", w);
    }
    if (w % 10000 == 0) {
        printf("Service two received %d messages\n", w);
    }
    w++;
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


int main(int argc, const char * argv[])
{
    o2_initialize("test");
    // o2_debug_flags("a");
    o2_service_new("one");
    o2_service_new("two");
    lookup();

    assert(o2_service_type(one) == O2_LOCAL);
    const char *pip;
    const char *iip;
    char procname[O2_MAX_PROCNAME_LEN];
    int port;
    O2err err = o2_get_addresses(&pip, &iip, &port);
    assert(err == O2_SUCCESS);
    sprintf(procname, "%s:%s:%04x", pip, iip, port);
    assert(streql(o2_service_process(one), "_o2"));
    assert(o2_service_tapper(one) == NULL);

    assert(streql(o2_service_properties(one), ""));
    assert(streql(o2_service_properties(two), ""));
    // set an attr/value
    assert(o2_service_set_property("bad", "attr1", "value1") == O2_FAIL);
    assert(o2_service_set_property("one", "attr0", "value0") == O2_SUCCESS);
    assert(o2_services_list_free() == O2_SUCCESS);
    // get the properties from service 1
    lookup();
    assert(o2_services_list() == O2_SUCCESS);
    assert(streql(o2_service_properties(one), "attr0:value0;"));

    // get empty properties from service 2
    assert(streql(o2_service_properties(two), ""));
    // get the value from service 1
    const char *gp = o2_service_getprop(one, "attr0");
    assert(streql(gp, "value0"));
    O2_FREE((char *) gp);
    // fail to get the value from service 2
    gp = o2_service_getprop(two, "attr0");
    assert(gp == NULL);

    // search for services with attr and value pattern within
    assert(o2_service_search(0, "attr0", "val") == one);
    // search for services with attr and value pattern with :
    o2_service_set_property("two", "attr1", "twovalue1two"); // will match value1
    assert(o2_services_list_free() == O2_SUCCESS);
    lookup();
    assert(o2_service_search(0, "attr1", ":value1") == -1);
    assert(o2_service_search(0, "attr1", ":twovalue") == two);
    // search for services with attr and value pattern with ;
    assert(o2_service_search(0, "attr1", "value1two;") == two);
    assert(o2_service_search(0, "attr1", "value1;") == -1);
    // search for services with attr and exact value
    assert(o2_service_search(0, "attr1", ":twovalue1two;") == two);
    assert(o2_service_search(0, "attr1", ":value1two;") == -1);

    // change value
    assert(o2_service_set_property("one", "attr1", "newvalue1") == O2_SUCCESS);
    assert(o2_services_list_free() == O2_SUCCESS);
    // get the changed value
    lookup();
    gp = o2_service_getprop(one, "attr1");
    assert(streql(gp, "newvalue1"));
    O2_FREE((char *) gp);

    // remove the value
    assert(o2_service_property_free("one", "attr1") == O2_SUCCESS);
    assert(o2_services_list_free() == O2_SUCCESS);
    // fail to get the value
    lookup();
    gp = o2_service_getprop(one, "attr1");
    assert(gp == NULL);
    gp = o2_service_properties(one);
    assert(streql(gp, "attr0:value0;"));
    // add several new attr/values 2 3 4 5 6
    assert(o2_service_set_property("one", "attr1", "value1") == O2_SUCCESS);
    assert(o2_service_set_property("one", "attr2", "value2") == O2_SUCCESS);
    assert(o2_service_set_property("one", "attr3", "value3") == O2_SUCCESS);
    assert(o2_service_set_property("one", "attr4", "value4") == O2_SUCCESS);
    assert(o2_service_set_property("one", "attr5", "value5") == O2_SUCCESS);

    // get the values
    assert(o2_services_list_free() == O2_SUCCESS);
    lookup();
    gp = o2_service_properties(one);
    assert(streql(gp,
        "attr5:value5;attr4:value4;attr3:value3;attr2:value2;attr1:value1;attr0:value0;"));
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

    // remove attrs 1 3 5
    assert(o2_services_list_free() == O2_SUCCESS);
    assert(o2_service_property_free("one", "attr1") == O2_SUCCESS);
    assert(o2_service_property_free("one", "attr3") == O2_SUCCESS);
    assert(o2_service_property_free("one", "attr5") == O2_SUCCESS);
    assert(o2_services_list_free() == O2_SUCCESS);

    // get and check full properties string
    lookup();
    gp = o2_service_getprop(one, "attr2");
    assert(streql(gp, "value2"));
    O2_FREE((char *) gp);
    gp = o2_service_getprop(one, "attr4");
    assert(streql(gp, "value4"));
    O2_FREE((char *) gp);
    gp = o2_service_getprop(one, "attr1");
    assert(o2_service_getprop(one, "attr1") == NULL);
    gp = o2_service_getprop(one, "attr3");
    assert(o2_service_getprop(one, "attr3") == NULL);
    gp = o2_service_getprop(one, "attr5");
    assert(o2_service_getprop(one, "attr5") == NULL);
    assert(o2_services_list_free() == O2_SUCCESS);
    
    // check escaped chars
    assert(o2_service_set_property("one", "attr1", "\\;\\:\\\\") == O2_SUCCESS);
    assert(o2_service_set_property("one", "attr2", "\\:value2\\;") == O2_SUCCESS);
    assert(o2_service_set_property("one", "attr3", "val\\\\\\\\ue3") ==
           O2_SUCCESS);
    assert(o2_service_set_property("one", "attr4", "\\\\\\\\\\;\\:value4") ==
           O2_SUCCESS);
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
    assert(o2_services_list_free() == O2_SUCCESS);

    o2_finish();
    printf("DONE\n");
    return 0;
}
