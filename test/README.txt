README.txt

These are test programs for o2, benchmarking and development.

broadcastclient.c - development code; see if broadcasting works
broadcastserver.c

clockmaster.c - test of O2 clock synchronization (there are no 
clockslave.c    provisions here to test accuracy, only if it works).
                To test, run both processes on the same host or on 
                the same local network. They should discover each 
                other, run the clock sync protocol, and print 
                messages indicating success.

lo_benchmark_client.c - a performance test similar to o2client/o2server
lo_benchmark_server.c   but using liblo (you will have to get liblo
                        and build these yourself if you want to run them.

o2client.c - performance test; send messages back and forth between
o2server.c   client and server. Only expected to work on localhost.
             To test, run both processes on the same host. After about
             10s, they should start sending messages back and forth, 
             printing how many messages have been sent. They run only
             a short time unless you pass a message count to o2client:
               o2client 10000000

tcpclient.c - o2client/o2server will eventually drop a message if
tcpserver.c   run on an unreliable network. These programs do the
              same test as o2client/o2server but use tcp rather than
              udp so that they should work on a wireless connection as
              well as on a single host or over local (wired) ethernet.

tcppollclient.c - development code exercising poll() to get messages
tcppollserver.c

midiclient.c - read keys from console and send MIDI via O2 to a server
midiserver.c   that relays the messages to MIDI using PortMIDI. This
               was used for an O2 demo. Requires portmidi library.

dispatchtest.c - test for simple message construction and dispatch. 
                 Runs forever, so kill it by hand.

typestest.c - send short messages of all types except vectors and
              arrays. Prints DONE near the end if every test passes; 
              otherwise, it will be terminated by a failed assert(). 

coercetest.c - send short messages of all types and try all type
               coercions.  Prints DONE near the end if every test 
               passes; otherwise, it will be terminated by a failed 
               assert(). 


arraytest.c - send arrays and vectors, receive them with all possible  
              coercions. Prints DONE near the end if every test 
              passes; otherwise, it will be terminated by a failed 
              assert(). 


longtest.c - send long messages to force special memory allocation
             for big messages. Prints DONE near the end if every test 
             passes; otherwise, it will be terminated by a failed 
             assert(). 


