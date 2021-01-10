//  oscbndlsend.c - test program to send OSC bundles
//
//  this test is designed to run with either oscbndlrecv.c or lo_bndlrecv.c

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

#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"

int block_check = true;

O2message_ptr make_message(O2time time, const char *address, int i, char *s)
{
    o2_send_start();
    o2_add_int32(i);
    o2_add_string(s);
    // add the message to the bundle
    return o2_message_finish(time, address, false);
}


O2message_ptr bundle2(O2time time, O2message_ptr m1, O2message_ptr m2)
{
    o2_send_start();
    o2_add_message(m1);
    O2_FREE(m1);
    o2_add_message(m2);
    O2_FREE(m2);
    return o2_service_message_finish(time, "oscsend", "", false);
}


void send_nested(O2time now, O2time touter, O2time tinner, int base)
{
    char s[128];

    // make first message
    sprintf(s, "first string at %g", touter);
    O2message_ptr out1 = make_message(now + touter, "/oscsend/first",
                                       base + 1, s);
    // make first inner message
    sprintf(s, "msg1 string at %g", tinner);
    O2message_ptr in1 = make_message(now + tinner, "/oscsend/xyz/msg1",
                                      base + 2, s);
    // make second inner message
    // use timestamp of 0, should deliver at max(touter, tinner) because
    // of containing bundle
    sprintf(s, "msg2 string at %g", tinner);
    O2message_ptr in2 = make_message(0.0, "/oscsend/abcdefg/msg2",
                                      base + 3, s);

    // make inner bundle
    O2message_ptr inner = bundle2(now + tinner, in1, in2);
    // make outer bundle
    O2message_ptr outer = bundle2(now + touter, out1, inner);

    printf("send_nested o2_can_send to oscsend: %d\n", o2_can_send("oscsend"));
    while (block_check && o2_can_send("oscsend") != O2_SUCCESS) {
        o2_poll();
    }

    // send it
    o2_message_send(outer);
}


int main(int argc, const char * argv[])
{
    printf("Usage: oscbndlrecv flags (see o2.h for flags, "
           "use a for all, also u for UDP, M for master, and\n"
           "! to send without checking o2_can_send)\n");
    int tcpflag = true;
    int master = false;
    if (argc == 2) {
        o2_debug_flags(argv[1]);
        tcpflag = (strchr(argv[1], 'u') == NULL);
        master = (strchr(argv[1], 'M') != NULL);
        block_check = (strchr(argv[1], '!') == NULL);
        printf("debugflags %s, tcp %d, master %d\n", argv[1], tcpflag, master);
    }
    if (argc > 2) {
        printf("WARNING: o2server ignoring extra command line argments\n");
    }
    printf("tcpflag %d master %d\n", tcpflag, master);

    o2_initialize("test");

    // you can make this run without an O2 server by passing "master"
    if (master)
        o2_clock_set(NULL, NULL);
    
    if (master) o2_sleep(2000); // wait for liblo server to come up if we are master

    char s[128];

    printf("Waiting for clock sync\n");
    while (!o2_clock_is_synchronized) {
        o2_sleep(2);
        o2_poll();
    }

    int err = o2_osc_delegate("oscsend", "localhost", 8100, tcpflag);
    assert(err == O2_SUCCESS);
    printf("connected to port 8100\n");
    
    O2time now = o2_time_get();
    
    printf("Sending simple message\n");
    // note: You can send messages with o2_send() or os_send_cmd() -- the
    // actual choice of UDP or TCP is controlled by whether the OSC is
    // a UDP server or a TCP server. HOWEVER, if OSC is using TCP and you
    // call o2_send(), O2 is free to drop the message if there is a previous
    // message waiting to be sent or if the TCP connection is not yet
    // accepted by the server. On the other hand, if OSC is using UDP and
    // you call o2_send_cmd(), it will behave just like calling o2_send()
    // if the O2 message forwarding is from this local process. So in
    // general, it's best to have a direct connection to the OSC server
    // (don't forward messages through another process without a good
    // reason), and use o2_send_cmd().
    o2_send_cmd("/oscsend/test", 0.0, NULL);

    printf("Sending messages\n");
    for (int i = 9; i >= 5; i--) {
        // make the bundle
        o2_send_start();

        // make first message
        sprintf(s, "an arbitrary string at 2.%d", i);
        O2message_ptr msg1 = make_message(0.0, "/oscsend/xyz/msg1", 
                                           1000 + i, s);

        // make second message
        sprintf(s, "another arbitrary string at 2.%d", i);
        O2message_ptr msg2 = make_message(0.0, "/oscsend/abcdefg/msg2", 
                                           2000 + i, s);

        // add the messages to the bundle
        o2_send_start();
        o2_add_message(msg1);
        O2_FREE(msg1);
        o2_add_message(msg2);
        O2_FREE(msg2);
        O2message_ptr msg = o2_service_message_finish(now + 2 + i * 0.1,
                                                       "oscsend", "", true);
        printf("Sending bundle with %d \"...2.%d\" and %d \"...2.%d\"\n",
               1000 + i, i, 2000 + i, i);

        printf("o2_can_send to oscsend: %d\n", o2_can_send("oscsend"));
        while (block_check && o2_can_send("oscsend") != O2_SUCCESS) {
            o2_poll();
        }
        // send it
        o2_message_send(msg);
    }

    // now send nested bundles
    // this tests timestamps on inner bundles, trying both 0 and a time:
    //    [@3.0 /first [@0 /msg1 /msg2]] -- should deliver all at 3.0
    //    [@3.1 /first [@3.2 /msg1 /msg2]] -- should dliever msg1, msg2 at 3.2

    printf("send_nested(now, 3.0, 0.0, 3000);\n");
    send_nested(now, 3.0, 0.0, 3000);
    printf("send_nested(now, 3.1, 3.2, 4000);\n");
    send_nested(now, 3.1, 3.2, 4000);

    printf("after sending\n");
    for (int i = 0; i < 500; i++) {
        o2_poll();
        o2_sleep(2); // if you exit() after send(), data might be lost
    }
    printf("removing oscsend\n");
    o2_service_free("oscsend");
    printf("calling o2_finish()\n");
    o2_finish();
    printf("sleep(1)\n");
    o2_sleep(1000); // clean up sockets
    printf("OSCSEND DONE\n");
    return 0;
}
