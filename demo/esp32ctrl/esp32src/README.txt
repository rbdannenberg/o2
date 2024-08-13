This directory, esp32src, contains code for the Sparkfun ESP32 Thing.
To build the program, put *all* of these files in a directory and open
the acc2midi.ini file with the Arduino IDE. If everything goes well,
you can simply upload the project and the IDE will compile, install,
and run the program.

You will need to run o2host and configure it. (See the blog
www.cs.cmu.edu/~rbd/blog/o2host/o2host-blog12aug2024.html.)

The configuration screen of o2host looks like this:
--------------------------------------------------------------------------

Configuration: acc2midi              Load   Delete 
    Rename to:                       Save   New 

Ensemble name:     rbdapp                            Polling rate: 500 
Debug flags:       d                                 Reference Clock: Y
Networking (up/down to select): local network      
HTTP Port: 8080  Root: www                            
MQTT Host:                                  MQTT Port:      
MIDI Out Service midiout              to IAC Driver Bus 1             (X )

··········································································
New forward O2 to OSC:          New forward OSC to O2:  
New MIDI In to O2:      New MIDI Out from O2:      MIDI Refresh:  
Type ESC to start, Control-H for Help.

--------------------------------------------------------------------------

You should run o2host in the parent directory so it can find www, and you
should visit localhost:8080/acc2midi.htm to load the control panel, which
should automatically connect to o2host and the ESP32.
