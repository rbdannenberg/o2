# ESP32 Control Demo Software for O2 and O2lite

**Roger B. Dannenberg**
**Aug 2024**

This is part of demonstration of using a Browser as a control
panel for an ESP32 microcontroller program. The web page here
connects through O2lite to an instance of **o2host** which relays
control to the ESP32, also connected to **o2host** over O2lite.
There is a blog online with more details.

## The Control Interface
The microcontroller uses 2 accelerometers for roll and pitch.

There are 4 sliders:
- Roll Input Range - roll values are clipped to +/- this number.
- Minimum Pitch - minimum roll value is mapped to this pitch (a MIDI 
  key number, middle C = C4 = 60). 
- Maximum Pitch - maximum roll value is mapped to this pitch. 
- Threshold - notes are stopped when pitch (rotation along y axis) 
  exceeds this threshold. 
- Duration - the Inter-Onset-Interval (IOI) of notes sent. 

There is also a selection of scale types and a button to blink an LED
on the microcontroller.

