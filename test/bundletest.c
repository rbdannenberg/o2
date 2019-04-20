//  bundletest.c -- dispatch messages between local services
//

#include <stdio.h>
#include "o2.h"
#include "o2_dynamic.h"
#include "assert.h"
// why do we need this? If we need this we also need thread_local from o2_internal.h
//          #include "o2_message.h"


#define N_ADDRS 20

int expected = 0;  // expected encodes the expected order of invoking services
// e.g. 2121 means (right to left) service1, service2, service1, service2

void service_one(o2_msg_data_ptr data, const char *types,
                 o2_arg_ptr *argv, int argc, void *user_data)
{
    assert(argc == 1);
    assert(argv[0]->i == 1234);
    printf("service_one called\n");
    assert(expected % 10 == 1);
    expected /= 10;
}

void service_two(o2_msg_data_ptr data, const char *types,
                 o2_arg_ptr *argv, int argc, void *user_data)
{
    assert(argc == 1);
    assert(argv[0]->i == 2345);
    printf("service_two called\n");
    assert(expected % 10 == 2);
    expected /= 10;
}


int main(int argc, const char * argv[])
{
    o2_initialize("test");
    o2_service_new("one");
    o2_method_new("/one/i", "i", &service_one, NULL, TRUE, TRUE);  
    o2_service_new("two");
    o2_method_new("/two/i", "i", &service_two, NULL, TRUE, TRUE);

    // make a bundle, starting with two messages
    o2_send_start();
    o2_add_int32(1234);
    o2_message_ptr one = o2_message_finish(0.0, "/one/i", TRUE);

    o2_send_start();
    o2_add_int32(2345);
    o2_message_ptr two = o2_message_finish(0.0, "/two/i", TRUE);

    expected = 21;
    o2_send_start();
    o2_add_message(one);
    o2_add_message(two);
    o2_send_finish(0.0, "#one", TRUE);
    // because delivery of messages in bundle is nested delivery,
    // the nested messages are queued up. Delivery is strictly
    // sequential. Therefore, we have to call o2_poll() to finish
    // delivery. It's unspecified how many times you need to call
    // o2_poll(), but once or twice should be enough. 100 to be sure!
    for (int i = 0; i < 100 && expected != 0; i++) o2_poll();
    assert(expected == 0);
    
    expected = 21;
    o2_send_start();
    o2_add_message(one);
    o2_add_message(two);
    o2_send_finish(0.0, "#two", TRUE);
    for (int i = 0; i < 100 && expected != 0; i++) o2_poll();
    assert(expected == 0);
    
    // make a nested bundle ((12)(12))
    o2_send_start();
    o2_add_message(one);
    o2_add_message(two);
    o2_message_ptr bdl = o2_message_finish(0.0, "#one", TRUE);

    expected = 2121;
    o2_send_start();
    o2_add_message(bdl);
    o2_add_message(bdl);
    o2_send_finish(0.0, "#two", TRUE);
    for (int i = 0; i < 100 && expected != 0; i++) o2_poll();
    assert(expected == 0);
    
    o2_finish();
    printf("DONE\n");
    return 0;
}
