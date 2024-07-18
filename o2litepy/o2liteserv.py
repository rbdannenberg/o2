# o2liteserv.py -- port of test/o2liteserv.c for o2lite in python
#
# Zekai Shen and Roger B. Dannenberg
# Feb 2024
 
# This test should be run with o2/test/o2litehost.c (e.g. after
#     compiling the .c file, execute o2/Debug/o2litehost). This
#     test does the following:
# - initialize o2lite
# - wait for discovery
# - wait for clock sync
# - send a message to self over O2 with sift types
# - respond to messages from o2litehost's client services

from o2litepy import O2lite
# from util.sleep import o2l_sleep

sift_called = False
msg_count = 0
running = True
use_tcp = False


def about_equal(val1, val2, tolerance=1e-6):
    return abs(val1 - val2) < tolerance


def sift_han(address, types, info):
    global sift_called

    # print("sift_han called")
    assert info == 111
    assert o2l.get_string() == 'this is a test'
    assert o2l.get_int32() == 1234
    assert abs(o2l.get_float() - 123.4) < 0.0001
    assert about_equal(o2l.get_time(), 567.89)
    sift_called = True


def server_test(address, types, info):
    global msg_count, running, use_tcp

    got_i = o2l.get_int32()

    msg_count += 1
    o2l.send_start(client_addresses[msg_count % n_addrs], 0, "i", use_tcp)
    o2l.add_int(msg_count)
    o2l.send()

    if msg_count % 10000 == 0:
        print(f"-------------server received {msg_count} messages------------")

    if msg_count < 100:
        print(f"------------server message {msg_count} is {got_i}------------")

    if got_i == -1:
        running = False
    else:
        assert msg_count == got_i, "Message count does not match received value"


if __name__ == "__main__":
    o2l = O2lite()
    o2l.initialize("test", debug_flags="a")
    o2l.set_services("sift")
    o2l.method_new("/sift", "sift", True, sift_han, 111)
    while o2l.bridge_id < 0:
        o2l.poll()
    o2l.send_start("/sift", 0, "sift", True)
    o2l.add_string("this is a test")
    o2l.add_int(1234)
    o2l.add_float(123.4)
    o2l.add_time(567.89)
    o2l.send()

    while o2l.time_get() < 0:
        o2l.poll()
        o2l.sleep(2)

    print("o2liteserv.py detected o2lite clock sync")

    start_wait = o2l.time_get()
    print("o2liteserv.py start_wait", start_wait)
    while start_wait + 1 > o2l.time_get() and not sift_called:
        o2l.poll()
        o2l.sleep(2)
        if not sift_called: print("Error: sift not called at", o2l.time_get)

    print("o2liteserv.py received loop-back message")

    n_addrs = 20

    # Creating addresses and handlers to receive server messages
    client_addresses = []
    server_addresses = []

    for i in range(n_addrs):
        client_address = f"!client/benchmark/{i}"
        server_address = f"/server/benchmark/{i}"

        client_addresses.append(client_address)
        server_addresses.append(server_address)

        # Assuming server_test is a function defined somewhere as a handler
        o2l.method_new(server_address, "i", True, server_test, None)

    # Announcing the server services
    o2l.set_services("sift,server")

    while running:
        o2l.poll()
        o2l.sleep(2)

    assert sift_called
    print("o2liteserv\nSERVER DONE")
