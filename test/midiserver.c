//  midiserver.c - example program, receive O2, send MIDI
//
//  This program works with midiclient.c. 
//

#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "portmidi.h"
#include "porttime.h" // maybe not needed

#define TIME_PROC ((int32_t (*)(void *)) Pt_Time)
#define TIME_INFO NULL

PmStream *midi_out = NULL; // MIDI output stream

#ifdef WIN32
#include <windows.h> 
#else
#include <unistd.h>
#endif


// this is a handler for incoming messages. It simply builds a
// MIDI message and sends it using portmidi
//
void midi_handler(o2_msg_data_ptr msg, const char *types,
                  o2_arg ** argv, int argc, void *user_data)
{
    int status = argv[0]->i32;
    int data1 = argv[1]->i32;
    int data2 = argv[2]->i32;
    Pm_WriteShort(midi_out, 0, Pm_Message(status, data1, data2));
    printf("Pm_WriteShort(%2x %2x %2x at %g\n", status, data1, data2, o2_time_get());
}


int main(int argc, const char * argv[])
{
    o2_debug_flags("3");
    
    // start portmidi
    Pt_Start(1, 0, 0);

    int dev_num = Pm_GetDefaultOutputDeviceID();
    printf("Using default PortMidi ouput device number %d\n", dev_num);

    Pm_OpenOutput(&midi_out, dev_num, NULL, 0, TIME_PROC, TIME_INFO, 0);

    o2_initialize("miditest"); // ideally, this application name should be
    // passed from the command line so we provide service to any application

    // we are the master clock
    o2_clock_set(NULL, NULL);

    o2_service_new("midi");
    
    // add our handler for incoming messages to each server address
    o2_method_new("/midi/midi", "iii", &midi_handler, NULL, TRUE, TRUE);
    
    printf("Here we go! ...\ntime is %g.\n", o2_time_get());
    
    while (TRUE) {
        o2_poll();
        usleep(1000); // 1ms
    }

    o2_finish();
    Pm_Close(midi_out);
    Pm_Terminate();
    return 0;
}
