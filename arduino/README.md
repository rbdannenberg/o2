# Notes on connecting Arduino to O2 ensembles

**Roger B. Dannenberg**

Jan 2022

A full implementation of O2 is not available, but O2lite works and has been
tested on a Sparkfun ESP32 Thing connecting over WiFi.

`o2/src/o2lite.c` has notes and installation tips. This document repeats
those notes and adds a few extras.

O2lite test programs can be found in `test/*` and work under macOS,
Linux, and Windows. O2lite has been tested on ESP32 Thing board 
from Spark Fun, Inc. (hopefully more in the future), using the
Arduino development environment. Rather than making an installable
library, I kept the code more accessible and debuggable by simply
including it in a project. To compile with O2lite, you should:
- create a src subdirectory in your Arduino project. This is
  easily done *outside* the Arduino IDE, once you find where it
  stores projects.
- copy the following to your ESP_project folder:
    - `o2/o2lite.c`
    - `o2/o2lite.h`
    - `o2/o2base.h`
    - `o2/hostipimpl.h`
    - `o2/hostip.h`
    - `o2/o2liteesp32.cpp`
    - note that `o2/hostip.c` should *not* be used. `hostip.h` is implemented in `o2lite.h`, which includes `o2/hostipimpl.h`. The file `o2/hostip.c` is only for the O2 library.
- add an `#include` to code where you make calls to o2lite:\
```#include "o2lite.h"```

You will need at least the .c and .cpp files to appear in tabs
in the Arduino IDE. This tells the IDE to compile and link them.
The Arduino IDE should take care of the rest.. It will compile
and link your project with `o2lite.c` and `o2liteesp32.cpp`.

For convenience, you can use `o2litesetup.bat` (on Windows) in this directory to copy o2lite files from the `src` directory to the Arduino project directory of your choice. Files are copied to the path provided as an argument on the command line.


