README.txt

These are test programs for o2, benchmarking and development.

broadcastclient.c - development code; see if broadcasting works
broadcastserver.c

clockmaster.c - test of O2 clock synchronization (there are no 
clockmaster.h   provisions here to test accuracy, only if it works)

lo_benchmark_client.c - a performance test similar to o2client/o2server
lo_benchmark_server.c

o2client.c - performance test; send messages back and forth between
o2server.c   client and server. Only expected to work on localhost.

tcpclient.c - o2client/o2server will eventually drop a message if
tcpserver.c   run on an unreliable network. These programs do the
              same test using tcp rather than udp so that they should
              work on a wireless connection.

tcppollclient.c - development code exercising poll() to get messages
tcppollserver.c

midiclient.c - read keys from console and send MIDI via O2 to a server
midiserver.c   that relays the messages to MIDI using PortMIDI.

dispatchtest.c - test for simple message construction and dispatch

typestest.c - send short messages of all types except vectors and
              arrays.

coercetest.c - send short messages of all types and try all type
               coercions

arraytest.c - send arrays and vectors, receive them with all possible  
              coercions.  

longtest.c - send long messages to force special allocation


