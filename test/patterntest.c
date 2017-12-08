// patterntest.c -- test patterned messages
//

// What does this test?
// 1. sending patterned messages

#include <stdio.h>
#include "o2.h"
#include "assert.h"
#include "string.h"
#define NEGATE '!'
#ifdef WIN32
#define snprintf _snprintf
#endif

int got_the_message = FALSE;

// 3. sending typestring ?* (an array with 3 integers)
//
void service_aiii(o2_msg_data_ptr data, const char *types,
                 o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_extract_start(data);
#ifndef NDEBUG
     o2_arg_ptr arg =
#endif
    o2_get_next('i');

#ifndef NDEBUG
    arg =
#endif
    o2_get_next('i');

#ifndef NDEBUG
    arg =
#endif
    o2_get_next('i');

    got_the_message = TRUE;
	printf("%d",got_the_message);
}

void send_the_message()
{
    for (int i = 0; i < 1000000; i++) {
        if (got_the_message) break;
        o2_poll();
    }

    got_the_message = FALSE;
}

int main(int argc, const char * argv[])
{
    o2_set_discovery_period(1.0);
    o2_initialize("test");
    o2_service_new("one");
    o2_method_new("/one/service_a?*/0", "iii", &service_aiii,NULL, FALSE, FALSE);
	  o2_method_new("/one/service_a?/", "iii", &service_aiii,NULL, FALSE, FALSE);
	  o2_method_new("/one/service_a?[*]/", "i[ii]", &service_aiii,NULL, FALSE, FALSE);
    o2_method_new("/one/service_a?[!*]/", "i[!ii]", &service_aiii,NULL, FALSE, FALSE);
    o2_method_new("/one/service_a*]/", "iii]", &service_aiii,NULL, FALSE, FALSE);
    o2_method_new("/one/service_a[*/", "[iii", &service_aiii,NULL, FALSE, FALSE);
    o2_method_new("/one/service_a[%c-%c]/", "[a-z]", &service_aiii,NULL, FALSE, FALSE);
    o2_method_new("!one/service_a[%c-%c]/", "[a-z]", &service_aiii,NULL, FALSE, FALSE);
    o2_method_new("/one/service_a{%c}/", "{a}", &service_aiii,NULL, FALSE, FALSE);
    o2_method_new("/one/service_a{%c,%c}/", "{a,a}", &service_aiii,NULL, FALSE, FALSE);
    o2_send_start();

    o2_add_start_array();
    o2_add_int32(456);
    o2_add_int32(234);
	  o2_add_int32(567);
    o2_add_end_array();
    o2_send_finish(0, "/one/service_a?*/0", TRUE);
  	o2_send_finish(0, "/one/service_a?/", TRUE);
    o2_send_finish(0, "/one/service_a?[!*]/", TRUE);
    o2_send_finish(0, "/one/service_a*]/", TRUE);
    o2_send_finish(0, "/one/service_a[*/", TRUE);
    o2_send_finish(0, "/one/service_a[%c-%c]/", TRUE);
    o2_send_finish(0, "!one/service_a[%c-%c]/", TRUE);
    o2_send_finish(0, "/one/service_a{%c}/", TRUE);
    o2_send_finish(0, "/one/service_a{%c,%c}/", TRUE);
    send_the_message();

  	o2_add_int32(123);
  	o2_add_start_array();
    o2_add_int32(456);
    o2_add_int32(234);
	  o2_add_end_array();
    int i = o2_send_finish(0, "/one/service_a?[*]/", TRUE);
    o2_error_to_string(i);
    send_the_message();
    printf("DONE sending 123, 234, 567\n");
    printf("DONE\n");
    o2_finish();
    return 0;
}
