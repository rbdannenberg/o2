README.txt

-- This directory has test programs for o2, benchmarking and development. --


Contents
--------
- Top-level Regression Testing
- Tests run by regression_tests.py
- MQTT Tests 
- Some additional OSC tests
- Performance benchmarks


Top-level Regression Testing
----------------------------

regression_tests.py - run all regression tests that run locally.

checkports.py - see which O2 ports are in use, which are free.

portstatus.py - obsolete


regression_tests.sh  
regression_run_two.sh  
regression_run_two_d.sh  
portfreetest.sh - all obsolete  
 

Tests run by regression_tests.py
--------------------------------

applead.c   - tests whether O2 can reinitialize and change the 
appfollow.c   ensemble name. (Ensemble used to be named application).

arraytest.c - send arrays and vectors, receive them with all possible  
              coercions. Prints DONE near the end if every test 
              passes; otherwise, it will be terminated by a failed 
              assert(). 


bridgeapi.c - tests of the bridge API that forwards messages to another
	      transport mechanism.

broadcastclient.c - development code; see if broadcasting works
broadcastserver.c

bundletest.c - test delivery of message bundles (locally.

clockmirror.c - test of O2 clock synchronization (there are no 
clockref.c      provisions here to test accuracy, only if it works).
                To test, run both processes on the same host or on 
                the same local network. They should discover each 
                other, run the clock sync protocol, and print 
                messages indicating success.

coercetest.c - send short messages of all types and try all type
               coercions.  Prints DONE near the end if every test 
               passes; otherwise, it will be terminated by a failed 
               assert(). 

dispatchtest.c - test O2 dispatching messages to handlers (locally)

dropclient.c - test for warning messages when messages are dropped.
dropserver.c   Tests o2_message_warnings() call.

hubclient.c - a careful test of using hub() instead of discovery.
hubserver.c

infotest1.c - test the O2 mechanism for sending service info messages
	      to /_o2/si (uses clockmirror process).

infotest2.c   - test the O2 mechanism for sending service info messages
clockmirror.c   to /_o2/si using remote process.

lo_benchmark_client.c - a performance test similar to o2client/o2server
lo_benchmark_server.c   but using liblo (you will have to get liblo
                        and build these yourself if you want to run them.

longtest.c - test for longer messages that require allocation (actually,
	     the current O2 implementation does not keep a cache of
	     small pre-allocated messages, but at least this tests
	     different message sizes).

midiclient.c - read keys from console and send MIDI via O2 to a server  
midiserver.c   that relays the messages to MIDI using PortMIDI. This  
               was used for an O2 demo. Requires portmidi library.  
 
nonblockrecv.c - test that non-blocking send and receive are working,
nonblocksend.c   including detecting/reporting cases that *would* block,
		 and unblocking after blocking occurs.

o2block.c   - another test for blocking and non-blocking sends in O2  
o2unblock.c  
 
o2client.c - performance test; send messages back and forth between
o2server.c   client and server. Only expected to work on localhost.
             To test, run both processes on the same host. After about
             10s, they should start sending messages back and forth, 
             printing how many messages have been sent. They run only
             a short time unless you pass a message count to o2client:
               o2client 10000000

o2litehost.c   - Tests o2lite implementation and hosting.
o2liteserver.c

o2litemsg.c - Simple test of message create and dispatch for o2lite.
	      All local, so sending message within o2lite.

oscanytest.c  - Test sending and receiving *any* OSC messages.  
oscsendtest.c  
 
oscbndlrecv.c - test OSC bundle send and receive.
oscbndlsend.c

oscrecvtest.c - Test sending and receiving OSC messages. More careful
oscsendtest.c   and exact than oscanytest.c

patterntest.c - test finding handlers when addresses contain patterns.

proprecv.c - test for propagating service properties, which is usable
propsend.c   as publish/subscribe.

shmemserv.c - tests shared memory bridge by talking to o2client.
o2client.c

statusclient.c - Test discovery and finding status of remote service.   
statusserver.c  
 
stuniptest.c - see if O2 can get a public IP address using a STUN server

tappub.c - test taps across two processes.
tapsub.c

taptest.c - local test for tapping a server

tcpclient.c - o2client/o2server will eventually drop a message if
tcpserver.c   run on an unreliable network. These programs do the
              same test as o2client/o2server but use tcp rather than
              udp so that they should work on a wireless connection as
              well as on a single host or over local (wired) ethernet.

tcppollclient.c - development code exercising poll() to get messages
tcppollserver.c

typestest.c - send short messages of all types except vectors and
              arrays. Prints DONE near the end if every test passes; 
              otherwise, it will be terminated by a failed assert(). 


MQTT Tests
----------

srp/mqttsender.srp     - Demo/test of getting from one computer to another
srp/mqttsubscriber.srp   via MQTT. Implemented in Serpent. See mqttserver.c.

The ability to run tests that require two hosts is needed; for now,
you must run these tests manually. Currently the only such test pair
is:

mqttserver.c - Make a connection and exchange messages using MQTT.
mqttclient.c   This test passes without MQTT if you run both tests
	       on the same local area network, even on two machines.
	       If you have two machines able to broadcast UDP to each
	       other, then connect one to the Internet via VPN so that
	       the machines can no longer broadcast discovery messages
	       to each other over UDP.

 
Other Multi-host Tests
----------------------

o2litedisc.ino - ESP32 code to test discovery using o2lite

o2liteserv.ino - ESP32 code to test offering a service over o2lite


Some additional OSC tests 
-------------------------

srp/oscrecvtest.srp - not sure if these are compatible with anything, 
srp/oscsendtest.srp   but might help with general low-level OSC testing. 


Performance benchmarks
----------------------

o2utclient.c - Send a bunch of messages over TCP or UDP
o2usserver.c

