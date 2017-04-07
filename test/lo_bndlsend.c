//  lo_bndlsend.c - test to send OSC bundles
//
//  this test is designed to run with oscbndlrecv.c

// We'll send 5 bundles:
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

#include "stdio.h"
#include "assert.h"
#include "lo/lo.h"
#include "string.h"

#ifdef WIN32
#include "usleep.h" // special windows implementation of sleep/usleep
#else
#include <unistd.h>
#endif

lo_address client;

#define TWO32 4294967296.0

void timetag_add(lo_timetag *timetag, lo_timetag x, double y)
{
    double secs = x.sec + (x.frac / TWO32);
    secs += y;
    timetag->sec = (uint32_t) secs;
    timetag->frac = (uint32_t) ((secs - timetag->sec) * TWO32);
}


lo_message make_message(int i, char *s)
{
    lo_message msg = lo_message_new();
    lo_message_add_int32(msg, i);
    lo_message_add_string(msg, s);
    return msg;
}
    

void send_nested(lo_timetag now, double touter, double tinner, int base)
{
    lo_timetag timetag;
    char s[128];
    // send nested bundle
    timetag_add(&timetag, now, touter);
    lo_bundle outer = lo_bundle_new(timetag);

    // make first message
    sprintf(s, "first string at %g", touter);
    lo_message out1 = make_message(base + 1, s);
    // add the message to the bundle
    lo_bundle_add_message(outer, "/first", out1);

    // make inner bundle
    timetag_add(&timetag, now, tinner);
    lo_bundle inner = lo_bundle_new(timetag);

    // make first inner message
    sprintf(s, "msg1 string at %g", tinner);
    lo_message in1 = make_message(base + 2, s);
    // add the message to the bundle
    lo_bundle_add_message(inner, "/xyz/msg1", in1);

    // make second inner message
    sprintf(s, "msg2 string at %g", tinner);
    lo_message in2 = make_message(base + 3, s);
    // add the message to the bundle
    lo_bundle_add_message(inner,  "/abcdefg/msg2", in2);

    // add the inner bundle
    lo_bundle_add_bundle(outer, inner);

    // send it
    lo_send_bundle(client, outer);
}


int main(int argc, const char * argv[])
{
    int tcpflag = 1;
    printf("Usage: lo_bndlsend [u] (u means use UDP)\n");
    if (argc == 2) {
        tcpflag = (strchr(argv[1], 'u') == NULL);
    }
    printf("tcpflag %d\n", tcpflag);
    sleep(2); // allow some time for server to start

    client = lo_address_new_with_proto(tcpflag ? LO_TCP : LO_UDP,
                                       "localhost", "8100");
    printf("client: %p\n", client);
    char s[128];
    
    lo_timetag now;
    lo_timetag_now(&now);
    lo_timetag timetag;
    
    for (int i = 9; i >= 5; i--) {
        // make the bundle
        timetag_add(&timetag, now, 2 + i * 0.1);
        lo_bundle bndl = lo_bundle_new(timetag);

        // make first message
        sprintf(s, "an arbitrary string at 2.%d", i);
        lo_message msg1 = make_message(1000 + i, s);
        // add the message to the bundle
        lo_bundle_add_message(bndl, "/xyz/msg1", msg1);

        // make second message
        sprintf(s, "another arbitrary string at 2.%d", i);
        lo_message msg2 = make_message(2000 + i, s);
        // add the message to the bundle
        lo_bundle_add_message(bndl, "/abcdefg/msg2", msg2);

        // send it
        lo_send_bundle(client, bndl);
    }

    send_nested(now, 3.0, 0.0, 3000);
    send_nested(now, 3.1, 3.2, 4000);
    sleep(1); // make sure messages go out
    lo_address_free(client);
    sleep(1); // time to clean up socketsa
    printf("OSCSEND DONE\n");
    return 0;
}
