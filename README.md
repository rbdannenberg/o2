# O2
O2 is a communication protocol and implementation
for music systems that aims to replace Open Sound Control
(OSC). Many computer musicians routinely deal with
problems of interconnection in local area networks, unreliable
message delivery, and clock synchronization. O2
solves these problems, offering *named services*, automatic
network *address discovery*, *clock synchronization*, and
a *reliable message delivery option*, as well as *interoperability*
with existing OSC libraries and applications.
Aside from these new features, O2 owes much of its design
to OSC and is mostly compatible with and similar to
OSC. O2 addresses the problems of inter-process communication
with a minimum of complexity.

O2 now supports asynchronous sends, WebSockets, a simplified subset
protocol O2lite, Bonjour/Avahi for discovery and service property lists.

## More on O2

[An informal demo/overview](https://www.cs.cmu.edu/~rbd/blog/nime-blog22may2022.html).

[Bibliography](https://www.cs.cmu.edu/~rbd/bib-o2.html).

[Teaser video](https://youtu.be/ELVsGEBS9Go).

## Building O2

O2 uses CMake. There are a lot of configuration options, so I recommend
the CMake GUI (MacOS or Windows) or `ccmake` (Linux: in the o2 directory
just run `ccmake .` and type `h` for help) to build either a project
for Xcode or Visual Studio, or a Makefile for Linux (then on Linux, 
simply type `make`). 

The default for O2 discovery is Bonjour/Avahi, so on Linux you need the
`avahi-client-dev` package to be installed.

## Using o2host

**o2host** is a simple command-line application that can serve as a host 
to o2lite processes (including browser-based applications using o2lite
over websockets through `o2ws.js` (in `o2/test/www/o2ws.js`), and can
also serve as a bridge between MIDI, OSC, and O2.

See `doc/o2host.txt` for more details on building and running **o2host**.

## Documentation

[O2 web pages with documentation](https://rbdannenberg.github.io/o2/)

src/o2.h -- the O2 API and most Doxygen sources are here.

src/o2.cpp -- contains implementation details.

src/o2lite.h -- the O2lite API. O2lite connects to an O2 host to
obtain O2 services with a very small implementation built using 
the O2 bridge protocol. o2lite.h is a particular implementation
of the O2lite protocol for microcontrollers (e.g. ESP32).

src/o2lite.c -- contains implementation details for the
"microcontroller with WIFI and Bonjour" implementation of O2lite.

doc/* -- most of these files contain early design notes. The "real"
API is in src/o2.h, which is translated to docs/* by Doxygen. The
final description of the implementation is described in src/o2.cpp.
It is hard to keep the documentation consistent with the code, so I
have kept original notes in doc/* for reference. Anything wrong or
missing from src/o2.cpp should be considered a mistake, so doc/* 
should mostly be unnecessary.

