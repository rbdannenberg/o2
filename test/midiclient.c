// midiclient.c - an example program, send to midiserver
//
// This program works with midiserver.c.

#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "cmtio.h"

#ifdef WIN32
#include <windows.h> 
#else
#include <unistd.h>
#endif

#pragma comment(lib,"o2_static.lib")

#define N_ADDRS 20

char *server_addresses[N_ADDRS];
int msg_count = 0;


int main(int argc, const char * argv[])
{
    o2_debug = 2;
    
    /* establish non-blocking input so we can "type" some notes */
    IOsetup(0); // inputfd: 0 means stdin

    o2_initialize("miditest");
    
    while (o2_status("midi") < O2_LOCAL) {
        o2_poll();
        usleep(2000); // 2ms
    }
    printf("We discovered the midi service.\ntime is %g.\n", o2_get_time());
    
    printf("Here we go! ...\ntime is %g.\n", o2_get_time());
    
    while (1) {
        o2_poll();
        int input = IOgetchar();
        if (input > 0) {
            char *keys = "qwertyuiopasdfghjklzxcvbnm";
            char *keyloc = strchr(keys, input);
            if (keyloc) {
                input = (keyloc - keys) + 48; // index of found key to pitch
                double now = o2_get_time();
                o2_send_cmd("/midi/midi", 0.0, "iii", 0x90, input, 127);
                o2_send_cmd("/midi/midi", now + 1.0, "iii", 0x90, input, 0);
                printf("sent key number %d at %g\n", input, now);
            }
        }
        usleep(2000); // 2ms
    }

    o2_finish();
    return 0;
}
