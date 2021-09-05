# o2
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

O2 is currently undergoing major changes to support asynchronous
sends. Please look for a released version -- these work and have been
used fairly extensively. Do not try to use the most recent (head)
version from the repository.

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

