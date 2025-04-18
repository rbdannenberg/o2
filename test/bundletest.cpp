//  bundletest.c -- dispatch messages between local services
//

#include <stdio.h>
#include "o2.h"
#include "testassert.h"


#define N_ADDRS 20

int expected = 0;  // expected encodes the expected order of invoking services
// e.g. 2121 means (right to left) service1, service2, service1, service2


void service_one(O2msg_data_ptr data, const char *types,
                 O2arg_ptr *argv, int argc, const void *user_data)
{
    o2assert(argc == 1);
    o2assert(argv[0]->i == 1234);
    printf("service_one called\n");
    o2assert(expected % 10 == 1);
    expected /= 10;
}


void service_two(O2msg_data_ptr data, const char *types,
                 O2arg_ptr *argv, int argc, const void *user_data)
{
    o2assert(argc == 1);
    o2assert(argv[0]->i == 2345);
    printf("service_two called\n");
    o2assert(expected % 10 == 2);
    expected /= 10;
}


int main(int argc, const char * argv[])
{
    printf("Usage: bundletest [debugflags] "
           "(see o2.h for flags, use a for (almost) all)\n");
    if (argc == 2) {
        o2_debug_flags(argv[1]);
        printf("debug flags are: %s\n", argv[1]);
    }
    if (argc > 2) {
        printf("WARNING: bundletest ignoring extra command line argments\n");
    }

    o2_initialize("test");
    o2_service_new("one");
    o2_method_new("/one/i", "i", &service_one, NULL, true, true);
    o2_service_new("two");
    o2_method_new("/two/i", "i", &service_two, NULL, true, true);

    // make a bundle, starting with two messages
    o2_send_start();
    o2_add_int32(1234);
    O2message_ptr one = o2_message_finish(0.0, "/one/i", true);

    o2_send_start();
    o2_add_int32(2345);
    O2message_ptr two = o2_message_finish(0.0, "/two/i", true);

    expected = 21;
    o2_send_start();

    o2_add_message(one);
    o2_add_message(two);
    o2_send_finish(0.0, "#one", true);
    // because delivery of messages in bundle is nested delivery,
    // the nested messages are queued up. Delivery is strictly
    // sequential. Therefore, we have to call o2_poll() to finish
    // delivery. It's unspecified how many times you need to call
    // o2_poll(), but once or twice should be enough. 100 to be sure!
    for (int i = 0; i < 100 && expected != 0; i++) o2_poll();
    o2assert(expected == 0);
    
    expected = 21;
    o2_send_start();
    o2_add_message(one);
    o2_add_message(two);
    o2_send_finish(0.0, "#two", true);
    for (int i = 0; i < 100 && expected != 0; i++) o2_poll();
    o2assert(expected == 0);
    
    // make a nested bundle ((12)(12))
    o2_send_start();
    o2_add_message(one);
    o2_add_message(two);
    O2message_ptr bdl = o2_message_finish(0.0, "#one", true);
    O2_FREE(one);
    O2_FREE(two);

    expected = 2121;
    o2_send_start();
    o2_add_message(bdl);
    o2_add_message(bdl);
    o2_send_finish(0.0, "#two", true);
    O2_FREE(bdl);
    for (int i = 0; i < 100 && expected != 0; i++) o2_poll();
    o2assert(expected == 0);
    
    o2_finish();
    printf("DONE\n");
    return 0;
}
