//  lo_bndlrecv.c - test program to receive OSC bundles
//
//  this test is designed to run with bndlsendtest.c
//  this test is based on bndlrecvtest.c

#include "stdio.h"
#include "testassert.h"
#include "lo/lo.h"
#include "string.h"
#include <stdbool.h>

#include "o2base.h" // to get o2_sleep()


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

double times[] = {2.5, 2.5, 2.6, 2.6, 2.7, 2.7, 2.8, 2.8, 2.9, 2.9,
                  3.0, 3.0, 3.0, 3.1, 3.2, 3.2, 999};


int msg_count = 0;
double start_time = 0.0;


// test if x and y are within 20ms
// Note: this failed with 10ms tolerance, which was surprising
// It seemed to be jitter and latency rather than systematic
// error (too early or too late), maybe just due to printing.
int approximate(double x, double y)
{
    double diff = x - y;
    return (diff < 0.02) && (diff > -0.02);
}

#define JAN_1970 0x83aa7e80     /* 2208988800 1970 - 1900 in seconds */

// we'll use secs since 1970 for a little more precision
double timetag_to_secs(lo_timetag tt)
{
    return (tt.sec - JAN_1970) + tt.frac * 0.00000000023283064365;
}


int meta_handler(const char *name, lo_arg **argv, int argc)
{
    lo_timetag ttnow;
    lo_timetag_now(&ttnow);
    double now = timetag_to_secs(ttnow);
    if (msg_count == 0) {
        start_time = now - 2.5;
    }
    printf("%s received %d, \"%s\"\n", name, argv[0]->i, &(argv[1]->s));
    printf("    elapsed time: %g msg_count %d\n", now - start_time, msg_count);
    o2assert(argv);
    o2assert(argc == 2);
    o2assert(argv[0]->i == ints[msg_count]);
    o2assert(strcmp(&(argv[1]->s), strings[msg_count]) == 0);
    o2assert(approximate(now - start_time, times[msg_count]));
    msg_count++;
    return 0;
}


#define ARGS const char *path, const char *types, \
             lo_arg **argv, int argc, void *msg, void *user_data

int first_handler(ARGS) { return meta_handler("first_handler", argv, argc); }
int  msg1_handler(ARGS) { return meta_handler("msg1_handler",  argv, argc); }
int  msg2_handler(ARGS) { return meta_handler("msg2_handler",  argv, argc); }

bool test_called = false;

int test_handler(ARGS)
{
    printf("test_handler received message to /test\n");
    test_called = true;
    return 0;
}


int main(int argc, const char * argv[])
{
    int tcpflag = 1;
    printf("Usage: lo_bndlrecv [u] (u means use UDP)\n");
    if (argc == 2) {
        tcpflag = (strchr(argv[1], 'u') == NULL);
    }
    printf("tcpflag %d\n", tcpflag);

    lo_server server = lo_server_new_with_proto(
                               "8100", tcpflag ? LO_TCP : LO_UDP, NULL);

    lo_server_add_method(server, "/test", "", &test_handler, NULL);
    lo_server_add_method(server, "/xyz/msg1", "is", &msg1_handler, NULL);
    lo_server_add_method(server, "/abcdefg/msg2", "is", &msg2_handler, NULL);
    lo_server_add_method(server, "/first", "is", &first_handler, NULL);

    while (msg_count < 16) {
        lo_server_recv_noblock(server, 0);
        o2_sleep(10); // 10ms
    }
    o2assert(test_called);
    lo_server_free(server);
    o2_sleep(1000);
    printf("OSCRECV DONE\n");
    return 0;
}

