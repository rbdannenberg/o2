// o2litemultihost.cpp - o2 process for more o2lite tests.
//
// Roger B. Dannenberg
// Aug 2026
//
// Goal: Test connecting to multiple o2lite processes.
//
// Waits for two o2lite processes to connect and exchange messages and
// exit. After the 2nd o2lite process exists, this process will exit.
// 
// The messages protocol is:
// 1. this process offers service "checkin"
// 2. o2lite process registers a service named "client<N>" where <N>
//    is the ID.
// 3. o2lite process sends ID and "client<N>" to "/checkin/hello"
// 4. this process sends ID to "/client<N>/checkin_reply"
// 5. o2lite process sends ID to "/checkin/goodbye"
// 6. o2lite process exits
// 7. this process exits after the 2nd "bye" recieved


#include "o2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "testassert.h"

int use_tcp = true;
bool running = true;
int exit_count = 0;

void checkin_hello(O2msg_data_ptr data, const char *types,
                   O2arg_ptr *argv, int argc, const void *user_data)
{
    int id = argv[0]->i32;
    char *reply_service = argv[1]->s;
    
    char reply_address[128];

    snprintf(reply_address, 128, "/%s/checkin_reply", reply_service);

    if (use_tcp) o2_send_cmd(reply_address, 0, "i", id);
    else o2_send(reply_address, 0, "i", id);

    printf("checkin_hello replied with id %d to address %s\n",
           id, reply_address);
}


void checkin_goodbye(O2msg_data_ptr data, const char *types,
                     O2arg_ptr *argv, int argc, const void *user_data)
{
    int id = argv[0]->i32;
    exit_count++;
    printf("checkin_goodbye got id %d; exit_count is now %d\n", id, exit_count);
    if (exit_count >= 2) {
        running = false;
    }
}


void time_check()
{
    if (o2_local_time() > 60) {
        o2_finish();
        printf("o2litehost timeout FAILURE exiting now\n");
        exit(1);
    }
}


int main(int argc, const char *argv[])
{
    printf("Usage: o2litemultihost [debugflags] [udp]\n"
           "    see o2.h for flags, use a for (almost) all, - for none\n"
           "    specify udp to use udp. Anything else or nothing gives tcp\n");
    if (argc >= 2) {
        o2_debug_flags(argv[1]);
        printf("debug flags are: %s\n", argv[2]);
    }
    if (argc >= 3) {
        if (strcmp(argv[2], "udp") == 0) {
            use_tcp = false;
        }
        printf("Using %s\n", use_tcp ? "TCP" : "UDP");
    }
    if (argc > 3) {
        printf("WARNING: o2litemultihost ignoring extra command "
               "line argments\n");
    }

    o2_initialize("test");
#ifndef O2_NO_BRIDGES
    o2lite_initialize(); // enable o2lite - this test is used with o2liteserv
#endif
    o2_clock_set(NULL, NULL); // become the master clock
    o2_service_new("checkin");
    o2_method_new("/checkin/hello", "is", &checkin_hello, NULL, false, true);
    o2_method_new("/checkin/goodbye", "i", &checkin_goodbye, NULL, false, true);

    int stat = 0;
    
    printf("Here we go! ...\ntime is %g.\n", o2_time_get());
    
    while (running) {
        time_check();
        o2_poll();
        o2_sleep(2); // 2ms (you could delete this line for benchmarking)
    }
    // run for another 1/2 second to make sure all messages delivered
    for (int i = 0; i < 250; i++) {
        o2_poll();
        o2_sleep(2);
    }

    o2_finish();
    printf("HOST DONE\n");
    return 0;
}
