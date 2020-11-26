//  mqttclient.c - test for using MQTT as a bridge
//
//  see mqttserver.c for details


// needed for usleep
#include "o2usleep.h"
#include "o2.h"
#include "stdio.h"
#include "assert.h"

int MAX_MSG_COUNT = 10;

int msg_count = 0;
bool running = true;

void client_recv_reply(o2_msg_data_ptr data, const char *types,
                       o2_arg_ptr *argv, int argc, const void *user_data)
{
    msg_count++;
    int i = argv[0]->i32;
    printf("msg_count %d i %d\n", msg_count, i);
    assert(msg_count + 1000 == i);

    // server will shut down when it gets a goodbye message
    if (msg_count >= MAX_MSG_COUNT) {
        o2_send_cmd("!server/goodbye", 0, "i", msg_count + 1);
        running = false;
    } else {
        printf("client received msg %d\n", msg_count);
        o2_send_cmd("!server/server", 0, "i", msg_count + 1);
    }
}


int main(int argc, const char *argv[])
{
    printf("Usage: mqttclient [debugflags]\n"
           "    see o2.h for flags, use a for all\n");
    if (argc >= 2) {
        if (argv[1][0] != '-') {
            o2_debug_flags(argv[1]);
            printf("debug flags are: %s\n", argv[1]);
        }
    }
    if (argc > 2) {
        printf("WARNING: mqttclient ignoring extra command line argments\n");
    }

    o2_initialize("test");
    o2_mqtt_enable(NULL, 0);
    o2_service_new("client");
    o2_method_new("/client/client", "i", &client_recv_reply, NULL, false, true);
    
    while (o2_status("server") < O2_REMOTE_NOTIME) {
        o2_poll();
        usleep(2000); // 2ms
    }
    printf("We discovered the server at local time %g.\n", o2_local_time());

    while (o2_status("server") < O2_REMOTE) {
        o2_poll();
        usleep(2000); // 2ms
    }
    printf("Clock sync with server, time is %g.\n", o2_time_get());
    
    double now = o2_time_get();
    while (o2_time_get() < now + 1) {
        o2_poll();
        usleep(2000);
    }
    
    double mean, min;
    o2_roundtrip(&mean, &min);
    printf("Clock round-trip mean %g, min %g\n", mean, min);

    printf("Here we go! ...\ntime is %g.\n", o2_time_get());
    
    o2_send_cmd("!server/server", 0, "i", 1);
    
    while (running) {
        o2_poll();
        usleep(2000); // 2ms // as fast as possible
    }

    // run some more to make sure messages get sent
    now = o2_time_get();
    while (o2_time_get() < now + 1) {
        o2_poll();
        usleep(2000);
    }
    
    o2_finish();
    printf("CLIENT DONE\n");
    return 0;
}
