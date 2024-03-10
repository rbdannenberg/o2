//  oscrecvtest.c - test o2_osc_port_new()
//
//  this test is designed to run with oscsendtest.c


#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"

// Here's what is sent
//   at NOW+2.9: [/xyz/msg1 1009 "an arbitrary string at 2.9"],
//               [/abcdefg/msg2 2009 "another arbitrary string at 2.9"]
//   at NOW+2.8: [/xyz/msg1 1008 "an arbitrary string at 2.8"],
//               [/abcdefg/msg2 2008 "another arbitrary string at 2.8"]
//   at NOW+2.7: [/xyz/msg1 1007 "an arbitrary string at 2.7"],
//               [/abcdefg/msg2 2007 "another arbitrary string at 2.7"]
//   at NOW+2.6: [/xyz/msg1 1006 "an arbitrary string at 2.6"],
//               [/abcdefg/msg2 2006 "another arbitrary string at 2.6"]
//   at NOW+2.5: [/xyz/msg1 1005 "an arbitrary string at 2.5"],
//               [/abcdefg/msg2 2005 "another arbitrary string at 2.5"]
// Then we'll send a nested bundle:
//   at NOW+3:   [/first 1111 "an arbitrary string at 3.0"],
//               [#bundle NOW+3.1
//                 [/xyz/msg1 1011 "an arbitrary string at 3.1"],
//                 [/abcdefg/msg2 2011 "another arbitrary string at 3.1"]]
//
// Putting that in delivery order, we expect:
//   at NOW+2.5: /xyz/msg1 1005 "an arbitrary string at 2.5"
//               /abcdefg/msg2 2005 "another arbitrary string at 2.5"
//   at NOW+2.6: /xyz/msg1 1006 "an arbitrary string at 2.6"
//               /abcdefg/msg2 2006 "another arbitrary string at 2.6"
//   at NOW+2.7: /xyz/msg1 1007 "an arbitrary string at 2.7"
//               /abcdefg/msg2 2007 "another arbitrary string at 2.7"
//   at NOW+2.8: /xyz/msg1 1008 "an arbitrary string at 2.8"
//               /abcdefg/msg2 2008 "another arbitrary string at 2.8"
//   at NOW+2.9: /xyz/msg1 1009 "an arbitrary string at 2.9"
//               /abcdefg/msg2 2009 "another arbitrary string at 2.9"
//   at NOW+3:   /first 1111 "an arbitrary string at 3.0"
//   at NOW+3.1: /xyz/msg1 1011 "an arbitrary string at 3.1"
//               /abcdefg/msg2 2011 "another arbitrary string at 3.1"


int ints[] = {1005, 2005, 1006, 2006, 1007, 2007, 1008, 2008, 1009, 2009,
              3001, 3002, 3003, 4001, 4002, 4003, 999};

const char *strings[] = {
    "an arbitrary string at 2.5",
    "another arbitrary string at 2.5",
    "an arbitrary string at 2.6",
    "another arbitrary string at 2.6",
    "an arbitrary string at 2.7",
    "another arbitrary string at 2.7",
    "an arbitrary string at 2.8",
    "another arbitrary string at 2.8",
    "an arbitrary string at 2.9",
    "another arbitrary string at 2.9",
    "first string at 3",
    "msg1 string at 0",
    "msg2 string at 0",
    "first string at 3.1",
    "msg1 string at 3.2",
    "msg2 string at 3.2",
    "not a valid string"};

O2time times[] = {2.5, 2.5, 2.6, 2.6, 2.7, 2.7, 2.8, 2.8, 2.9, 2.9,
                   3.0, 3.0, 3.0, 3.1, 3.2, 3.2, 999};


int msg_count = 0;
bool test_called = false;
O2time start_time = 0.0;


// test if x and y are within 30ms (Note: 10ms was too tight under
// Windows, but I'm not sure why.
int approximate(O2time x, O2time y)
{
    return (x < y + 0.03) && (y < x + 0.03);
}


void meta_handler(const char *name, O2arg_ptr *argv, int argc,
                  O2msg_data_ptr msg)
{
    if (msg_count == 0) { // assume first message is delivered at the right time
        start_time = o2_time_get() - 2.5; // timestamp was "now + 2.5"
    }
    printf("%s receieved %d, \"%s\"\n",
           name, argv[0]->i, argv[1]->s);
    printf("    elapsed %g timestamp %g o2 time %g last_time %g\n",
           o2_time_get() - start_time, msg->timestamp, o2_time_get(),
           o2_gtsched.last_time);
    assert(argv);
    assert(argc == 2);
    assert(argv[0]->i == ints[msg_count]);
    assert(strcmp(argv[1]->s, strings[msg_count]) == 0);
    assert(approximate(o2_time_get() - start_time, times[msg_count]));
    msg_count++;
}

#define ARGS O2msg_data_ptr msg, const char *types, \
             O2arg_ptr *argv, int argc, const void *user_data
void first_handler(ARGS) { meta_handler("first_handler", argv, argc, msg); }
void msg1_handler (ARGS) { meta_handler("msg1_handler",  argv, argc, msg); }
void msg2_handler (ARGS) { meta_handler("msg2_handler",  argv, argc, msg); }


void test_handler(ARGS)
{
    printf("test_handler got /oscrecv/test message\n");
    test_called = true;
}


int main(int argc, const char * argv[])
{
    printf("Usage: oscbndlrecv flags "
           "(see o2.h for flags, use a for (almost) all, also u for UDP)\n");
    int tcpflag = true;
    if (argc == 2) {
        o2_debug_flags(argv[1]);
        tcpflag = (strchr(argv[1], 'u') == NULL);
        printf("   flags found: %s\n", argv[1]);
    }
    if (argc > 2) {
        printf("WARNING: o2server ignoring extra command line argments\n");
    }

    o2_initialize("test");
    printf("tcpflag %d\n", tcpflag);
    o2_service_new("oscrecv");
    int err = o2_osc_port_new("oscrecv", 8100, tcpflag);
    assert(err == O2_SUCCESS);
    printf("created osc server port 8100\n");

    o2_clock_set(NULL, NULL);

    o2_method_new("/oscrecv/test", "", test_handler, NULL, false, true);
    o2_method_new("/oscrecv/xyz/msg1", "is", msg1_handler, NULL, false, true);
    o2_method_new("/oscrecv/abcdefg/msg2", "is", msg2_handler, 
                  NULL, false, true);
    o2_method_new("/oscrecv/first", "is", first_handler, NULL, false, true);
    while (msg_count < 16) {
        o2_poll();
        o2_sleep(1); // 1ms
    }
    assert(test_called);
    o2_osc_port_free(8100);
    o2_finish();
    o2_sleep(1000);
    printf("OSCRECV DONE\n");
    return 0;
}
