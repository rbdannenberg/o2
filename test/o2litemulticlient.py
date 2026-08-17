# o2litemulticlient.py -- port of test/o2litemulticlient.c for o2lite in python
#
# Roger B. Dannenberg
# August 2026

# This o2lite process is meant to connect to o2litemultihost.cpp.
# The expected protocol is:
# - initialize o2lite
# - wait for discovery
# - wait for clock sync
# - send a message to /checkin/hello with ID and a reply address.
# - receive message at /client<N>/checkin_reply (<N> is our ID)
# - send a message to /checkin/goodbye with ID
# - exit
#
# Alternatively, we can join the wrong ensemble and fail to connect
# (controlled by a command line argument). Then we time out and exit,
# but if we *do* connect, we continue with the protocol above.
#
# The o2litemultihost will exit after the 2nd /checkin/goodbye message,
# so if the bad ensemble name is not rejected, a 3rd o2litemulticlient
# using the correct ensemble name should fail to find o2litemultihost
# and will therefore fail.

import sys
from o2litepy import o2lite

running = True
use_tcp = True
wrong_name = False


def checkin_reply(address, types, info):
    global running
    id_val = o2lite.get_int32()
    if id_val != o2lite.bridge_id:
        print(f"FAILURE: o2litemulticlient checkin_reply got id {id_val}, "
              f"expected {o2lite.bridge_id}")
        sys.exit(1)
    print("checkin_reply called")
    if use_tcp:
        o2lite.send_cmd("/checkin/goodbye", 0, "i", o2lite.bridge_id)
    else:
        o2lite.send("/checkin/goodbye", 0, "i", o2lite.bridge_id)
    running = False


def time_check():
    if o2lite.local_now > 15:
        if wrong_name:
            print("o2litemulticlient with wrong ensemble name timed out")
            print("CLIENT DONE")
            sys.exit(0)
        else:
            print("o2litemulticlient timeout FAILURE exiting now")
            sys.exit(1)


if __name__ == "__main__":
    print("Usage: o2litemulticlient.py [-d debug_flags] [-w] [udp]")
    print("    -d debug_flags -- set debug flags string (e.g. -d a for all)")
    print("    -w initializes with the wrong ensemble name")
    print("    specify udp to use udp. Anything else or nothing gives tcp")

    debug_flags = ""
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] == "-d":
            i += 1
            if i < len(args):
                debug_flags = args[i]
            else:
                print("Error: -d requires a debug_flags argument")
                sys.exit(1)
        elif args[i] == "-w":
            wrong_name = True
        elif args[i] == "udp":
            use_tcp = False
        else:
            print(f"Unknown option: {args[i]}")
        i += 1

    ensemble = "badname" if wrong_name else "test"
    o2lite.initialize(ensemble, debug_flags=debug_flags)

    while o2lite.bridge_id < 0 or o2lite.time_get() < 0:
        time_check()
        o2lite.poll()
        o2lite.sleep(0.002)

    print("main detected o2lite connected")

    service_name = f"client{o2lite.bridge_id}"
    reply_address = f"/{service_name}/checkin_reply"
    o2lite.set_services(service_name)
    o2lite.method_new(reply_address, "i", True, checkin_reply, None)

    # Delay ~100ms so our service is registered with the host before sending.
    # (Without this, a UDP reply could be dropped if the host does not yet
    # know our service address.)
    for _ in range(50):
        o2lite.poll()
        o2lite.sleep(0.002)

    if use_tcp:
        o2lite.send_cmd("/checkin/hello", 0, "is", o2lite.bridge_id, service_name)
    else:
        o2lite.send("/checkin/hello", 0, "is", o2lite.bridge_id, service_name)

    print("main detected o2lite clock sync")

    while running:
        time_check()
        o2lite.poll()
        o2lite.sleep(0.002)

    print("sent /checkin/goodbye, exit in ~500ms")

    # run for another ~500ms to ensure all messages are delivered
    # since the host may shut down, closing our connection, o2lite.poll() can
    # be pretty slow, so rather than polling 250 times for 2ms each as in the
    # original C code, we poll as fast as we can until a timeout.
    delay_until = o2lite.local_now + 0.5
    while o2lite.local_now < delay_until:
        o2lite.poll()
        o2lite.sleep(0.002)

    print("o2litemulticlient")
    print("CLIENT DONE")
