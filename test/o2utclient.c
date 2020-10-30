//  o2utclient.c - part of performance benchmark
//
//  see o2utserver.c for details


#ifdef __GNUC__
// define usleep:
#define _XOPEN_SOURCE 500
#define _POSIX_C_SOURCE 200112L
#endif

#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"

#ifdef WIN32
#include "usleep.h" // special windows implementation of sleep/usleep
#else
#include <unistd.h>
#endif


int max_msg_count = 200000;

int msg_count = 0;


// poll O2 every ms for about dur seconds. If dur == 0, 
//     poll once and delay 1ms
static void ppause(double dur)
{
    do {
        o2_poll();
        usleep(1000); // 1ms
        dur -= 0.001;
    } while (dur > 0);
}


int main(int argc, const char *argv[])
{
    printf("Usage: o2utclient [maxmsgs] [debugflags]\n"
           "    see o2.h for flags, use a for all, - for none\n");
    if (argc >= 2) {
        max_msg_count = atoi(argv[1]);
        printf("max_msg_count set to %d\n", max_msg_count);
    }
    if (argc >= 3) {
        if (argv[1][0] != '-') {
            o2_debug_flags(argv[2]);
            printf("debug flags are: %s\n", argv[2]);
        }
    }
    if (argc > 3) {
        printf("WARNING: o2client ignoring extra command line argments\n");
    }

    o2_initialize("test");
    o2_service_new("client");
    
    while (o2_status("server") < O2_REMOTE) {
        ppause(0.002);
    }
    printf("We discovered the server.\ntime is %g.\n", o2_time_get());
    
    ppause(1.0);
    printf("Here we go! ...\ntime is %g.\n", o2_time_get());
    
    double next_time = o2_local_time();
    while (msg_count < max_msg_count) {
        if (o2_local_time() >= next_time) {
            if (msg_count % 2 == 0) { // UDP
                o2_send("!server/udp", 0, "i", msg_count);
            } else {
                o2_send_cmd("!server/tcp", 0, "i", msg_count);
            }
            msg_count++;
            next_time += 0.05;
        }
        ppause(0.0);
    }
    ppause(0.1);

    o2_send("!server/tcp", 0, "i", -1); // shutdown message

    ppause(1.0);

    o2_finish();
    printf("CLIENT DONE\n");
    return 0;
}
