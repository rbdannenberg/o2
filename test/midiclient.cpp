// midiclient.c - an example program, send to midiserver
//
// This program works with midiserver.c.

#undef NDEBUG
#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "cmtio.h"

#include "o2base.h" // to get o2_sleep

#define N_ADDRS 20

char *server_addresses[N_ADDRS];
int msg_count = 0;


int main(int argc, const char * argv[])
{
    o2_debug_flags("*");
    
    /* establish non-blocking input so we can "type" some notes */
    IOsetup(0); // inputfd: 0 means stdin

    o2_initialize("miditest");
    
    while (o2_status("midi") < O2_LOCAL) {
        o2_poll();
        o2_sleep(2); // 2ms
    }
    printf("We discovered the midi service.\ntime is %g.\n", o2_time_get());
    
    printf("Here we go! ...\ntime is %g.\n", o2_time_get());
    
    while (1) {
        o2_poll();
        int input = IOgetchar();
        if (input > 0) {
            char *keys = "qwertyuiopasdfghjklzxcvbnm";
            char *keyloc = strchr(keys, input);
            if (keyloc) {
                input = (int) ((keyloc - keys) + 48); // index of found key to pitch
                double now = o2_time_get();
                o2_send_cmd("/midi/midi", 0.0, "iii", 0x90, input, 127);
                o2_send_cmd("/midi/midi", now + 1.0, "iii", 0x90, input, 0);
                printf("sent key number %d at %g\n", input, now);
            }
        }
        o2_sleep(2); // 2ms
    }
    return 0;
}
