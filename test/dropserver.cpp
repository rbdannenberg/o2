//  dropserver.c - test drop warning
//
//  This program works with dropclient.c. 
//
// First sync up with dropclient.c
// Wait for dropclient's dropclient service to become ready.
// 0 Send a message with timing before clock is obtained.
// 1 Send a message to a non-existent service
// 2 Send a message to path with existing service but no matching path
// 3 Send a message to a matching path but with wrong types and no coercion
// 4 Send a message to a matching path with a good type count but not coercible
// 5 Test o2_method_new on a service that's remote - should fail.
// 6 Send a message to /dropclient/nohandler and see if it gets reported.
// 7 Send a message to /dropclient/bye
// Wait a bit and exit. (dropclient should wait and exit after bye message).

#include "o2usleep.h"
#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"

int msg_count = 0;

void pollsome()
{
    for (int i = 0; i < 10; i++) {
        o2_poll();
        usleep(2000);
    }
}


#define streql(a, b) (strcmp(a, b) == 0)
const char *expected_warning = "";
int warning_count = -1;  // warnings are numbered from 0 because I added
// a test at the beginning and wanted to keep all the wired-in test numbers
// that used to start at 1

static void drop_warning(const char *warn, o2_msg_data_ptr msg)
{
    printf("drop_warning: got \"%s\"\n", warn);
    if (expected_warning[0]) {
        assert(streql(warn, expected_warning));
    }
    warning_count++;
    printf("warning_count %d\n", warning_count);
}


// this is a handler for incoming messages
//
void hi(o2_msg_data_ptr msg, const char *types,
                 O2arg_ptr *argv, int argc, const void *user_data)
{
    assert(argc == 1);
    msg_count++;
    printf("bye handler msg_count %d i %d\n", msg_count, argv[0]->i32);
}


int main(int argc, const char *argv[])
{
    int rslt;
    printf("Usage: dropserver [debugflags]\n"
           "    see o2.h for flags, use a for all, - for none\n");
    if (argc >= 2) {
        if (argv[1][0] != '-') {
            o2_debug_flags(argv[1]);
            printf("debug flags are: %s\n", argv[1]);
        }
    }
    if (argc > 2) {
        printf("WARNING: dropserver ignoring extra command line argments\n");
    }

    o2_initialize("test");
    o2_message_warnings(drop_warning);
    o2_service_new("dropserver");
    //                                                  coerce parse
    o2_method_new("/dropserver/hi",     "i", &hi, NULL, false, true);
    o2_method_new("/dropserver/coerce", "i", &hi, NULL, true, true);

    expected_warning = "dropping message because there is no "
                       "clock and a non-zero timestamp";
    rslt = o2_send_cmd("/dropserver/hi", 10.0, "i", 4);
    printf("Return 0 is %d\n", rslt);
    assert(rslt == O2_NO_CLOCK);
    assert(warning_count == 0);
    pollsome(); // call o2_poll even if not necessary

    // we are the reference clock
    o2_clock_set(NULL, NULL);
    
    // wait for client service to be discovered
    while (o2_status("dropclient") < O2_REMOTE) {
        o2_poll();
        usleep(2000); // 2ms
    }
    
    printf("We discovered the dropclient at time %g.\n", o2_local_time());
    
    expected_warning = "dropping message because "
                       "service was not found";
    rslt = o2_send_cmd("/nonservice/", 0, "i", 1);
    printf("Return 1 is %d\n", rslt);
    assert(rslt == O2_NO_SERVICE);
    assert(warning_count == 1);
    pollsome(); // call o2_poll even if not necessary

    expected_warning = "dropping message because "
                       "no handler was found";
    rslt = o2_send_cmd("/dropserver/drop", 0, "i", 2);
    printf("Return 2 is %d\n", rslt);
    assert(rslt == O2_SUCCESS);
    assert(warning_count == 2);
    pollsome(); // call o2_poll even if not necessary

    expected_warning = "dropping message because of type mismatch";
    rslt = o2_send_cmd("/dropserver/hi", 0, "f", 3.3);
    printf("Return 3 is %d\n", rslt);
    assert(rslt == O2_SUCCESS);
    assert(warning_count == 3);
    pollsome(); // call o2_poll even if not necessary

    expected_warning = "dropping message because "
    "of type coercion failure";
    rslt = o2_send_cmd("/dropserver/coerce", 0, "s", "4");
    printf("Return 4 is %d\n", rslt);
    assert(rslt == O2_SUCCESS);
    assert(warning_count == 4);
    pollsome(); // call o2_poll even if not necessary
    
    rslt = o2_method_new("/dropclient/impossible", "i",
                         &hi, NULL, false, true);
    printf("Return 5 is %d\n", rslt);
    assert(rslt = O2_NO_SERVICE);
    pollsome(); // call o2_poll even if not necessary

    expected_warning = "none";
    rslt = o2_send_cmd("/dropclient/drop", 0, "i", 6);
    printf("Return 6 is %d\n", rslt);
    assert(rslt == O2_SUCCESS);
    assert(warning_count == 4);
    pollsome(); // call o2_poll even if not necessary

    rslt = o2_send_cmd("/dropclient/bye", 0, "i", 7);
    printf("Return 7 is %d\n", rslt);
    assert(rslt == O2_SUCCESS);
    assert(warning_count == 4);
    pollsome(); // call o2_poll even if not necessary
    
    rslt = o2_send_cmd("/dropserver/coerce", 0, "f", 8.1);
    printf("Return 8 is %d\n", rslt);
    assert(rslt == O2_SUCCESS);

    // delay 0.5 second
    double now = o2_time_get();
    while (o2_time_get() < now + 0.5) {
        o2_poll();
        usleep(2000);
    }

    // test for warning on timed message drop
    expected_warning = "dropping message because "
                       "no handler was found";
    now = o2_time_get();
    rslt = o2_send_cmd("/dropserver/drop", now + 0.1, "i", 2);
    printf("Return 9 is %d\n", rslt);
    assert(rslt == O2_SUCCESS);
    assert(warning_count == 4);
    while (o2_time_get() < now + 0.2) {
        o2_poll();
        usleep(2000);
    }
    assert(warning_count == 5);

    o2_finish();
    printf("DROPSERVER DONE\n");
    return 0;
}
