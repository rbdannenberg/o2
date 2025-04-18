// mqttself.c - another test for using MQTT
//
// This test models a problem that crashed Pd using O2 externals.
// It initializes O2 using MQTT, setting up a service am2 and
// a method for it.
// Then, after some time passes to allow for connection to MQTT,
// another service am1 is created. (This is where Pd crashed.)
// A message is sent to am2 and then a message is sent to am1.
// The process then shuts down, calling o2_finish().
//
// Be sure to test with/without F flag to force messages to go
// through MQTT (not sure if that works for messages to the same
// process though - probably not). 

#include "testassert.h"
// needed for usleep
#include "stdlib.h"
#include "o2usleep.h"
#include "o2.h"
#include "stdio.h"

bool got_am1 = false;
bool got_am2 = false;


// o2assertion-like test that works in release mode
void must_succeed(int err_code, const char *msg)
{
    if (err_code != O2_SUCCESS) {
        printf("Failed with code %d (%s)\n", err_code, msg);
        exit(1);
    }
}


void must_be_true(bool x, const char *msg)
{
    must_succeed(x ? O2_SUCCESS : O2_FAIL, msg);
}


// approximate delay while calling o2_poll()
//
void delay(float seconds)
{
    for (float t = 0.0; t < seconds; t += 0.002) {
        o2_poll();
        usleep(2000); // 2ms
    }
}

void am1_receive(O2msg_data_ptr data, const char *types,
              O2arg_ptr *argv, int argc, const void *user_data)
{
    must_be_true(!got_am1, "before am1_receive");  // allow only one message
    float f = argv[0]->f;
    printf("am1_receive: got %g\n", f);
    // f should be 123.4567
    must_be_true(123.456 < f && f < 123.457, "am1_receive value");
    got_am1 = true;
}


void am2_receive(O2msg_data_ptr data, const char *types,
              O2arg_ptr *argv, int argc, const void *user_data)
{
    must_be_true(!got_am2, "before am2_receive");  // allow only one message
    float f = argv[0]->f;
    printf("am2_receive: got %g\n", f);
    // f should be 234.5678
    must_be_true(234.567 < f && f < 234.568, "am2_receive value");  
    got_am2 = true;
}


int main(int argc, const char *argv[])
{
    printf("Usage: mqttself [debugflags]\n"
           "    see o2.h for flags, use a for (almost) all\n");
    if (argc >= 2) {
        if (argv[1][0] != '-') {
            o2_debug_flags(argv[1]);
            printf("debug flags are: %s\n", argv[1]);
        }
    }
    if (argc > 2) {
        printf("WARNING: mqttclient ignoring extra command line argments\n");
    }

    must_succeed(o2_initialize("test"), "o2_initialize");
    must_succeed(o2_mqtt_enable(NULL, 0), "o2_mqtt_enable");
    printf("Creating am2 service.\n");
    must_succeed(o2_service_new("am2"), "o2_service_new am2");
    must_succeed(o2_method_new("/am2/freq", "f", &am2_receive, 
                               NULL, false, true), "o2_method_new am2");
    
    printf("Delay while we connect to MQTT broker\n");
    delay(3.0);  // wait for MQTT connection

    printf("Creating am1 service.\n");
    must_succeed(o2_service_new("am1"), "o2_service_new am1");
    must_succeed(o2_method_new("/am1/freq", "f", &am1_receive,
                               NULL, false, true), "o2_method_new am1");

    printf("Sending to am2\n");
    o2_send_cmd("!am2/freq", 0, "f", 234.5678);
    
    while (!got_am2) delay(0.1);

    printf("Sending to am1\n");
    o2_send_cmd("!am1/freq", 0, "f", 123.4567);
    
    while (!got_am1) delay(0.1);
    
    o2_finish();
    printf("DONE\n");
    return 0;
}
