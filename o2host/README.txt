o2host is a small utility application to forward messages
and serve as a host for o2lite, including browser-based apps
using o2ws.js, a Javascript implementation of O2lite over web
sockets.

o2host can send and receive both MIDI and OSC messages.

MIDI messages are converted to O2 messages which are sent to
/<service>/midi with type string "i" (int32) and with the
status byte in the low-order byte, the first data byte in
the next byte (i.e. shifted left 8 bits) and the second
data byte (if any) in the next byte (shifted left 16 bits).

O2 messages targeted to MIDI can use either type "i" or "m"
and are sent to /<service>/midi.

OSC messages are appended to the service name, e.g. to send
an OSC message to address /xyz/abc, send an O2 message to
/<service>/xyz/abc, and configure O2 host to forward
<service> messages to OSC.

For more documentation, run o2host, use the tab key to navigate
to the help field and type X.

To build o2host, you should compile the O2 library using
../CMakeLists.txt, and install PortMidi from github
portmidi/portmidi. Relative to this directory, o2 should be
.. (the parent), portmidi should be in ../../portmidi, and
therefore ../../portmidi/pm_common/portmidi.h should exist.
