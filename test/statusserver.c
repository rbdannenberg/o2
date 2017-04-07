// statusserver.c - O2 status/discovery test, client side
//
// This program works with statusclient.c. It checks for 
// discovery of statusclient's service, sends it a message
// to exit, then checks that the status of the service
// reverts to "does not exist".

#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"

#ifdef WIN32
#include "usleep.h" // special windows implementation of sleep/usleep
#else
#include <unistd.h>
#endif

int running = TRUE;


int main(int argc, const char * argv[])
{
    printf("Usage: tcpserver\n");
    if (argc > 1) {
        printf("WARNING: o2server ignoring extra command line argments\n");
    }
    o2_initialize("test");
    
    // we are the master clock
    o2_clock_set(NULL, NULL);
    
    // wait for client service to be discovered
    while (o2_status("client") < O2_LOCAL) {
        o2_poll();
        usleep(2000); // 2ms
    }
    
    printf("We discovered the client at time %g.\n", o2_time_get());
    
    // delay 1 second
    double now = o2_time_get();
    while (o2_time_get() < now + 1) {
        o2_poll();
        usleep(2000);
    }
    
    double start_time = o2_time_get();
    printf("Here we go! ...\ntime is %g.\n", start_time);
    o2_send_cmd("!client/stop", 0.0, "");
    // allow 3s for client to shut down and detect it
    while (running && o2_time_get() < start_time + 3 &&
           o2_status("client") >= 0) {
        o2_poll();
    }
    if (o2_status("client") < 0) {
        printf("SERVER DONE\n");
    } else {
        printf("FAIL: client service status is %d\n", o2_status("client"));
    }
    o2_finish();
    sleep(1); // clean up sockets
    return 0;
}
