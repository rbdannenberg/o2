# o2liteserv.py -- port of test/o2liteserv.c for o2lite in python
#
# Zekai Shen and Roger B. Dannenberg
# Feb 2024
 
# This test:
# - initialize o2lite
# - wait for discovery
# - wait for clock sync
# - send a message to self over O2 with sift types
# - respond to messages from o2litehost's client services

from functools import partial

from src.msg_parser import MessageParser
from src.o2lite import O2Lite
# from util.sleep import o2l_sleep

sift_called = False
msg_count = 0
running = True
use_tcp = False


def about_equal(val1, val2, tolerance=1e-6):
    return abs(val1 - val2) < tolerance


def sift_han(msg, types, data, info):
    global sift_called
    parser = MessageParser(data)

    print("sift_han called")
    assert info == 111
    assert parser.o2l_get_string() == 'this is a test'
    assert parser.o2l_get_int32() == 1234
    assert abs(parser.o2l_get_float() - 123.4) < 0.0001
    assert about_equal(parser.o2l_get_time(), 567.89)
    sift_called = True


def server_test(o2l, msg, types, data, info):
    global msg_count, running, use_tcp

    parser = MessageParser(data)

    got_i = parser.o2l_get_int32()

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
    o2l = O2Lite()
    o2l.initialize("test")
    o2l.set_services("sift")
    o2l.method_new("/sift", "sift", True, sift_han, 111)
    while o2l.o2l_bridge_id == 0:
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

    print("main detected o2lite clock sync")

    start_wait = o2l.time_get()
    while start_wait + 1 > o2l.time_get() and not sift_called:
        o2l.poll()
        o2l.sleep(2)

    print("main received loop-back message")

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
        server_test_partial = partial(server_test, o2l=o2l)
        o2l.method_new(server_address, "i", True, server_test_partial, None)

    # Announcing the server services
    o2l.set_services("sift,server")

    while running:
        o2l.poll()
        o2l.sleep(2)

    print("owliteserv\nSERVER DONE")
