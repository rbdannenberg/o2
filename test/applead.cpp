//  applead.c - change ensemble test/demo
//
//  This program works with appfollow.c. Synopsis:
//    connect to appfollow as ensemble test1, 
//    establish clock sync,
//    receive "hello" message from follow,
//    shut down and reinitialize as ensemble test2,
//    establish clock sync,
//    receive "hello" message from follow,
//    shut down

#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"

int hello_count = 0;
O2time cs_time = 1000000.0;

// this is a handler that polls for current status
// it runs about every 1s
//
void applead(O2msg_data_ptr msg, const char *types,
               O2arg_ptr *argv, int argc, const void *user_data)
{
    int ss = o2_status("server");
    int cs = o2_status("client");
    printf("applead: local time %g global time %g "
           "server status %d client status %d\n",
           o2_local_time(), o2_time_get(), ss, cs);
    // record when the client synchronizes
    if (cs == O2_REMOTE) {
        if (o2_time_get() < cs_time && hello_count > 0) {
            cs_time = o2_time_get();
            printf("applead sync time %g\n", cs_time);
        }
    }
    // stop 10s later
    if (o2_time_get() > cs_time + 10) {
        o2_stop_flag = true;
        printf("applead set stop flag true at %g\n", o2_time_get());
    }
    o2_send("!server/applead", o2_time_get() + 1, "");
}


// this is a handler to get a "hello" message from appfollow
//
void apphello(O2msg_data_ptr msg, const char *types,
              O2arg_ptr *argv, int argc, const void *user_data)
{
    printf("applead got hello message at local time %g\n", o2_local_time());
    hello_count++;
}


int rtt_sent = false;
char client_ip_port[O2_MAX_PROCNAME_LEN];

void service_info(O2msg_data_ptr msg, const char *types,
                  O2arg_ptr *argv, int argc, const void *user_data)
{
    const char *service_name = argv[0]->s;
    int new_status = argv[1]->i32;
    const char *ip_port = argv[2]->s;
    const char *properties = argv[3]->s;
    printf("service_info: service %s status %d ip_port %s properties \"%s\"\n",
           service_name, new_status, ip_port, properties);
    if (streql(service_name, "client") && (new_status == O2_REMOTE)) {
        // client has clock sync
        if (!rtt_sent) {
            char address[O2_MAX_PROCNAME_LEN];
            strcpy(client_ip_port, ip_port); // save it for rtt_reply check
            snprintf(address, O2_MAX_PROCNAME_LEN, "%s%s%s",
                     "!", ip_port, "/cs/rt");
            o2_send_cmd(address, 0.0, "s", "!server/rtt/put");
            printf("Sent message to %s\n", address);
            rtt_sent = true;
        }
    }
}


int rtt_received = false;

void rtt_reply(O2msg_data_ptr msg, const char *types,
               O2arg_ptr *argv, int argc, const void *user_data)
{
    const char *service_name = argv[0]->s;
    float mean = argv[1]->f;
    float minimum = argv[2]->f;
    printf("rtt_reply: service %s mean %g min %g\n",
           service_name, mean, minimum);
    assert(rtt_sent);
    assert(streql(service_name, client_ip_port));
    assert(mean >= 0 && mean < 1);
    assert(minimum >= 0 && minimum < 1);
    rtt_received = true;
}


int main(int argc, const char * argv[])
{
    // flush everything no matter what (for getting as much info as possible when
    // there are problems):
    setvbuf (stdout, NULL, _IONBF, BUFSIZ);
    setvbuf (stderr, NULL, _IONBF, BUFSIZ);

    printf("Usage: applead [debugflags] "
           "(see o2.h for flags, use a for (almost) all)\n");
    if (argc == 2) {
        o2_debug_flags(argv[1]);
        printf("debug flags are: %s\n", argv[1]);
    }
    if (argc > 2) {
        printf("WARNING: applead ignoring extra command line argments\n");
    }

    o2_initialize("test1");
    o2_service_new("server");
    o2_method_new("/server/applead", "", &applead, NULL, false, false);
    o2_method_new("/server/hello", "", &apphello, NULL, false, false);
    o2_method_new("/_o2/si", "siss", &service_info, NULL, false, true);
    o2_method_new("/server/rtt/put", "sff", &rtt_reply, NULL, false, true);
    // we are the reference clock
    o2_clock_set(NULL, NULL);
    o2_send("!server/applead", 0.0, ""); // start polling
    o2_run(100);
    o2_finish();

    printf("---------------- applead changing app test1 to app test2 ------------\n");

    hello_count = 0;
    cs_time = 1000000.0;
    o2_stop_flag = false;
    o2_initialize("test2");
    o2_service_new("server");
    o2_method_new("/server/applead", "", &applead, NULL, false, false);
    o2_method_new("/server/hello", "", &apphello, NULL, false, false);
    o2_method_new("/_o2/si", "siss", &service_info, NULL, false, true);
    o2_method_new("/server/rtt/put", "sff", &rtt_reply, NULL, false, true);
    // we are the reference clock
    o2_clock_set(NULL, NULL);
    o2_send("!server/applead", 0.0, ""); // start polling
    o2_run(100);
    o2_finish();
    
    o2_sleep(1000);
    if (rtt_received) {
        printf("APPLEAD DONE\n");
    } else {
        printf("APPLEAD FAILED (no rtt message)\n");
    }
    return 0;
}
