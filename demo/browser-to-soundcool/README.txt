o2/demo/browser_to_soundcool - demonstrate o2host to send OSC
    from Browser to Soundcool

Roger B. Dannenberg
August 2024

This directory contains a demo described in a blog (to be written)
and to be part of the O2 distribution.

The function is to use the handtrack.js library to recognize poses
using a webcam and send commands to control a Soundcool patch.

The poses used are Pinch and Point. The Pinch gesture can be left,
lower middle, upper middle, lower right, or upper right, and the
Point gesture can be middle or right. This gives 7 different
"commands" to toggle sounds on and off.

To avoid false positives, a gesture must be detected 4 times with
no other intervening gesture classes before it is acted upon, and
after sending a command, no other command can be sent until the
gesture class changes and 1 second has elapsed.

To send a command, O2 over websockets is used. The command is
sent via o2host, which forwards commands from O2 to OSC. Soundcool
is set up to receive OSC on ports 8001 and 8002 as follows:

--------------------------------------------------------------------------------
Configuration: handtrack             Load   Delete 
    Rename to:                       Save   New 

Ensemble name:     rbdhttest                         Polling rate: 500 
Debug flags:       rsq                               Reference Clock: Y
Networking (up/down to select): local network      
HTTP Port: 8080  Root: www                            
MQTT Host:                                  MQTT Port:      
Fwd Service sndcues              to OSC IP 127.000.000.001 Port 8001  UDP (X )
Fwd Service drumloop             to OSC IP 127.000.000.001 Port 8002  UDP (X )

················································································
New forward O2 to OSC:          New forward OSC to O2:  
New MIDI In to O2:      New MIDI Out from O2:      MIDI Refresh:  
Type ESC to start, Control-H for Help.
--------------------------------------------------------------------------------

The browser URL should be localhost:8080/hto2.htm, and the web pages are in the
www subdirectory. It is necessary to run o2host from this browser-to-soundcool
directory so that it can find www and the pages inside.

Soundcool is loaded from scconfig.soundcool. You will probably have to reload
the wav files since scconfig.soundcool has complete path names to sound files.
