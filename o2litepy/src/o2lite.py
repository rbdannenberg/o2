# o2lite.py -- Python equivalent of o2lite.h
#
# Zekai Shen and Roger B. Dannenberg
# Feb 2024

# TIME:
# O2lite.local_now is the current local time, updated every
#     O2lite.poll(), which starts from 0
# O2lite.time_get() retrieves the global time in seconds, but -1
#     until clock synchronization completes.
# O2lite.local_time() retrieves the local time (local_now is identical
#     within the polling period and should be faster).
# o2l_sys_time() is a system-dependent function that returns time
#     in seconds (for internal use to implement O2lite.local_time())
# o2l_start_time (for internal use) is the start time of the
#     reference clock (may or may not be 0)
# O2lite.global_minus_local is (for internal use) the skew between
#     global and local time as estimated by clock synchronization.
# Caution: O2lite.get_time() and O2lite.add_time() are for message
#     reading and constructing; they do not tell time
#
# DEBUGGING:
# debug flags is a string used to set some debugging options:
#   b -- print actual bytes of messages
#   s -- print O2 messages when sent
#   r -- print O2 messages when received
#   d -- print info about discovery
#   g -- general debugging info
#   a -- all debugging messages except b
# Setting any flag automatically enables "g"
#
# DISCOVERY:
# discovery will find services representing O2 hosts as quickly
# as possible. We could initiate connections with all possible
# hosts, but since O2lite only connects to one host, it is better
# to attempt connections sequentially until one is successful.
# Normally, we will connect to the first host and not bother with
# the rest.
#
# To conduct orderly, sequential connection attempts, we will
# call discovery.get_host() if no host is connected and no
# connection is in progress. This is done in our polling loop
# and is fast because the real work of discovery is happening
# in another thread (or in the case of MicroPython, checks take
# about 10-15 msec on ESP32).
#
# Once we get a candidate host, we attempt to synchronously make
# a TCP connection. The connection may eventually fail, e.g. the
# host might refuse an O2lite connection or the host might crash,
# but until then, our tcp_socket is not None, and we use this
# condition to block further connection attempts to O2 hosts. If
# the TCP connect fails or closes, the tcp_socket is set to None
# so that we will go back to get_host() and try to connect to a
# new O2 host (as soon as we find one).
#
# If every connection fails, we will exhaust the list of services
# obtained by discovery. Since Bonjour is asynchronous, there
# might be a race condition that allowed us to miss an O2 host
# starting or restarting.  Therefore, if no new hosts appear after
# some time, we should restart the whole discovery process. The
# instance variable idle_start_time records when we notice that
# there are no hosts to try and no tcp_socket representing a
# connection attempt or successful connection. After 20 sec, we
# restart discovery.


import ctypes
import math
import select
import socket
import struct
import sys
import time

# Some system dependencies
if sys.implementation.name == "micropython":
    from .upyfns import o2l_sys_time, find_my_ip_address
    from .upydiscovery import Upydiscovery as O2lite_discovery
    # micropython cannot allocate a port number and tell you what it is
    initial_udp_recv_port = 55967  # chosen randomly from unassigned range
else:
    from .py3fns import o2l_sys_time, find_my_ip_address
    from .py3discovery import Py3discovery as O2lite_discovery
    # python can bind to port 0, allocate a free port, and tell you what it is:
    initial_udp_recv_port = 0  # chosen randomly from unassigned range
from .o2lite_handler import O2lite_handler
from .byte_swap import o2lswap32

# constants and flags
O2L_VERSION = 0x020000
O2L_SUCCESS = 0
O2L_FAIL = -1
O2L_ALREADY_RUNNING = -5

O2_UDP_FLAG = 0
O2_TCP_FLAG = 1

MAX_MSG_LEN = 256
PORT_MAX = 16

O2L_CLOCKSYNC = True

INVALID_SOCKET = -1

CLOCK_SYNC_HISTORY_LEN = 5


def o2l_clock_finish():
    if sys.platform == "win32":
        # Restore timer resolution using ctypes
        winmm = ctypes.WinDLL("winmm.dll")
        winmm.timeEndPeriod(1)


def roundup(value):
    return value + (4 - value % 4) if value % 4 != 0 else value


def o2l_address_init(ip, port_num, use_tcp):
    ai_family = socket.AF_INET
    ai_socktype = socket.SOCK_STREAM if use_tcp else socket.SOCK_DGRAM
    ai_protocol = socket.IPPROTO_TCP if use_tcp else socket.IPPROTO_UDP

    # Get address information
    addr_info = socket.getaddrinfo(ip, port_num, ai_family, ai_socktype,
                                   ai_protocol)

    # Return the first result
    return addr_info[0][4]


class O2lite:
    def __init__(self):
        # basic info
        self.udp_send_address = None
        self.socket_list = []
        self.internal_ip = None
        self.udp_recv_port = initial_udp_recv_port
        self.clock_sync_id = 0
        # discovery
        self.bridge_id = -1  # "no bridge"
        self.handlers = []
        self.services = None
        self.idle_start_time = 1e7
        self.error = False
        # message
        self.udpinbuf = None  # gets bytearray from udp recv
        self.outbuf = bytearray(256)
        self.out_msg_address = ""
        self.parse_msg = None  # incoming message to parse
        self.parse_address = None  # extracted address from parse_msg
        self.parse_cnt = 0  # how many bytes retrieved
        self.max_parse_cnt = 0  # how many bytes can be retrieved
        self.parse_types = None  # the type string (without the ',')
        self.parse_type_index = 0  # index of next type character
        self.parse_error = False  # was there an error parsing message?
        self.out_msg_cnt = 0  # how many bytes written to outbuf
        self.num_msg = 0
        # socket
        self.udp_recv_sock = None
        self.udp_send_sock = None
        self.tcp_socket = None
        self.udp_socket = None
        # clock sync
        self.local_time = o2l_sys_time  # system-dependent implementation
        self.local_now = -1
        self.clock_initialized = False
        self.clock_synchronized = False
        self.ping_reply_count = 0
        self.global_minus_local = 0
        self.start_sync_time = None
        self.time_for_clock_ping = 1e7
        self.clock_ping_send_time = None
        self.rtts = [0.0] * CLOCK_SYNC_HISTORY_LEN
        self.ref_minus_local = [0.0] * CLOCK_SYNC_HISTORY_LEN
        self.debug_flags = ""


    def initialize(self, ensemble_name, debug_flags=""):
        self.ensemble_name = ensemble_name

        # expand "a" to "srd" (all debug flags enabled)
        if 'a' in debug_flags:
            debug_flags += "srd"
        # if any flag is present, set 'g' flag for 'g'eneral messages
        if not 'g' in debug_flags and debug_flags != "":
            debug_flags += 'g'
        if 'b' in debug_flags:   # if we are printing message bytes,
            debug_flags += "sr"  # make sure send/receive are in there too
        self.debug_flags = debug_flags

        self.discovery = O2lite_discovery(ensemble_name, debug_flags)
        # Initialize clock (if not disabled)
        if O2L_CLOCKSYNC:
            self.clock_initialize()

        self.method_new("!_o2/id", "i", True, self.id_handler, None)

        # Create UDP send socket
        try:
            self.udp_send_sock = socket.socket(socket.AF_INET,
                                               socket.SOCK_DGRAM, 0)
        except socket.error as e:
            print("O2lite: allocating udp send socket:", e)
            return O2L_FAIL

        self.udp_recv_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM,
                                           socket.IPPROTO_UDP)
        # I think this cannot fail, or perhaps raises an error:
        # if self.udp_recv_sock == socket.error:
        #     print("O2lite: udp socket creation error")
        #     return O2L_FAIL

        if self.get_recv_udp_port(self.udp_recv_sock) != 0:
            print("O2lite: could not allocate udp_recv_port")
            return O2L_FAIL
        elif "g" in self.debug_flags:
            print("O2lite: UDP server port", self.udp_recv_port)

        self.internal_ip = find_my_ip_address()

        # running discovery
        self.discovery.run_discovery()


    def send_cmd(self, *args):
        self.send(*args, tcp=True)
        

    def send(self, *args, tcp=False):
        if len(args) > 0:  # this is like the all-in-one o2_send(...)
            type_string = ""
            if len(args) > 2:  # there is a type string
                type_string = args[2]
            self.send_start(args[0], args[1], type_string, tcp)
            i = 3
            for type_char in type_string:
                if type_char == 'i':
                    self.add_int(args[i])
                elif type_char == 'f':
                    self.add_float(args[i])
                elif type_char == 's':
                    self.add_string(args[i])
                elif type_char == 't':
                    self.add_time(args[i])
                i += 1
            # now we fall through to the send() code
        if self.error or self.tcp_socket is None:
            return

        self.add_length()

        misc = struct.unpack('I', self.outbuf[4:8])[0]

        # prepare for debug printing (may not be used):
        start = 0
        via = "TCP"

        if misc & o2lswap32(O2_TCP_FLAG):
            bytes_sent = self.tcp_socket.send(self.outbuf[:self.out_msg_cnt])
        else:
            bytes_sent = self.udp_send_sock.sendto(
                    self.outbuf[4:self.out_msg_cnt], self.udp_send_address)
            start = 4
            via = "UDP"

        # debug printing requested?
        if 's' in self.debug_flags:
            print("O2lite: sending", bytes_sent, "bytes via", via, end="")
            if 'b' in self.debug_flags:  # print actual bytes
                print("(", self.outbuf[start : self.out_msg_cnt], ")", end="")
            print(" to", self.out_msg_address)


        self.num_msg += 1


    def send_start(self, address, time, types, tcp):
        if tcp:
            flag = O2_TCP_FLAG
        else:
            flag = O2_UDP_FLAG

        self.parse_error = False
        self.out_msg_cnt = 4
        self.add_int(flag)
        self.add_time(time)
        self.add_string(address)
        self.outbuf[self.out_msg_cnt] = ord(',')
        self.out_msg_cnt += 1
        self.add_string(types)
        self.out_msg_address = address  # save for possible debug output


    def poll(self):
        self.local_now = self.local_time()

        if O2L_CLOCKSYNC:
            if self.time_for_clock_ping < self.local_now:
                self.clock_ping()

        self.network_poll()


    def add_string(self, s):
        for char in s:
            # Check for buffer overflow including space for null terminator
            if self.out_msg_cnt + 2 > MAX_MSG_LEN:
                return
            self.outbuf[self.out_msg_cnt] = ord(char)
            self.out_msg_cnt += 1

        # Add null terminator
        if self.out_msg_cnt < MAX_MSG_LEN:
            self.outbuf[self.out_msg_cnt] = 0
            self.out_msg_cnt += 1

        # Pad to 4-byte boundary
        while self.out_msg_cnt % 4 != 0:
            if self.out_msg_cnt < MAX_MSG_LEN:
                self.outbuf[self.out_msg_cnt] = 0
                self.out_msg_cnt += 1
            else:
                break


    def add_int(self, i):
        if self.out_msg_cnt + 4 > MAX_MSG_LEN:  # sizeof(int32_t) is 4 bytes
            return

        # Pack the integer with the endian swap and add it to the buffer
        self.outbuf[self.out_msg_cnt:self.out_msg_cnt + 4] = \
                struct.pack("<I", o2lswap32(i))
        self.out_msg_cnt += 4


    def add_length(self):
        msg_length_excluding_length_field = 4
        self.outbuf[0:4] = struct.pack("<I", o2lswap32(self.out_msg_cnt -
                                   msg_length_excluding_length_field))


    def add_time(self, time):
        if self.out_msg_cnt + 8 > MAX_MSG_LEN:
            return
        packed_time = struct.pack(">d", time)  # Pack as big-endian double
        self.outbuf[self.out_msg_cnt:self.out_msg_cnt + 8] = packed_time
        self.out_msg_cnt += 8


    def add_float(self, x):
        if self.out_msg_cnt + 4 > MAX_MSG_LEN:  # sizeof(float) is 4 bytes
            return
        packed_float = struct.pack(">f", x)  # Pack as big-endian float

        self.outbuf[self.out_msg_cnt:self.out_msg_cnt + 4] = packed_float
        self.out_msg_cnt += 4


    def ping_reply_handler(self, msg, types, data, info):
        id_in_data = self.get_int32()

        if id_in_data != self.clock_sync_id:
            return

        rtt = self.local_now - self.clock_ping_send_time
        ref_time = self.get_time() + rtt * 0.5

        if self.parse_error:
            return

        i = self.ping_reply_count % CLOCK_SYNC_HISTORY_LEN
        self.rtts[i] = rtt
        self.ref_minus_local[i] = ref_time - self.local_now

        if self.ping_reply_count >= CLOCK_SYNC_HISTORY_LEN:
            # find minimum round trip time
            min_rtt = self.rtts[0]
            best_i = 0
            for i in range(1, CLOCK_SYNC_HISTORY_LEN):
                if self.rtts[i] < min_rtt:
                    min_rtt = self.rtts[i]
                    best_i = i
            new_gml = self.ref_minus_local[best_i]

            if not self.clock_synchronized:
                if "g" in self.debug_flags:
                    print("O2lite: clock synchronized.")
                # print("ref_minus_local", self.ref_minus_local,
                #       "local_now", self.local_now, "new_gml", new_gml)
                self.clock_synchronized = True
                self.send_start("!_o2/o2lite/cs/cs", 0, "", True)
                self.send()  # notify O2 via tcp
                self.global_minus_local = new_gml
            else:
                bump = 0.0
                upper = new_gml + min_rtt
                lower = new_gml - min_rtt

                if self.global_minus_local < lower:
                    self.global_minus_local = lower
                elif self.global_minus_local > upper:
                    self.global_minus_local = upper
                elif self.global_minus_local < new_gml - 0.002:
                    bump = 0.002  # increase by 2ms if too low by > 2ms
                elif self.global_minus_local > new_gml + 0.002:
                    bump = -0.002  # decrease by 2ms if too high by > 2ms
                else:
                    bump = new_gml - self.global_minus_local

                self.global_minus_local += bump

        self.ping_reply_count += 1


    def add_socket(self, s):
        """
        Add the given socket to the read set if it is a valid socket.

        :param read_set: A list of sockets to monitor for readability.
        :param s: The socket to be added to the read set.
        """
        if s != INVALID_SOCKET:  # Assuming INVALID_SOCKET is a defined constant
            if s not in self.socket_list:
                self.socket_list.append(s)


    def id_handler(self, msg, types, data, info):
        self.bridge_id = self.get_int32()
        if "d" in self.debug_flags:
            print("O2lite: got id =", self.bridge_id)
        # We're connected now, send services if any
        self.send_services()
        if O2L_CLOCKSYNC:
            # Sends are synchronous. Since we just sent a bunch of messages,
            # take 50ms to service any other real-time tasks before this:
            self.time_for_clock_ping = self.local_now + 0.05
            # When does syncing start?:
            self.start_sync_time = self.time_for_clock_ping


    def clock_initialize(self):
        if self.clock_initialized:
            o2l_clock_finish()

        if O2L_CLOCKSYNC:
            self.method_new("!_o2/cs/put", "it", True,
                            self.ping_reply_handler, None)

        self.global_minus_local = 0
        self.clock_synchronized = False
        self.ping_reply_count = 0


    def clock_ping(self):
        if self.bridge_id <  0:  # make sure we still have a connection
            self.time_for_clock_ping += 1e7  # no more pings until connected
            return
        self.clock_ping_send_time = self.local_now
        self.clock_sync_id += 1
        self.send_start("!_o2/o2lite/cs/get", 0, "iis", False)
        self.add_int(self.bridge_id)
        self.add_int(self.clock_sync_id)
        self.add_string("!_o2/cs/put")
        self.send()
        self.time_for_clock_ping = self.clock_ping_send_time + 0.1
        if self.clock_ping_send_time - self.start_sync_time > 1:
            self.time_for_clock_ping += 0.4
        if self.clock_ping_send_time - self.start_sync_time > 5:
            self.time_for_clock_ping += 9.5


    def sleep(self, delay):
        """wait for delay sec, always polls at least once"""
        self.poll()
        wakeup = self.local_now + delay
        while self.local_now < wakeup:
            time.sleep(0.001)
            self.poll()


    def time_get(self):
        if self.clock_synchronized:
            return self.local_time() + self.global_minus_local
        else:
            return -1


    def get_recv_udp_port(self, sock):
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

        server_addr = ("", self.udp_recv_port)

        try:
            sock.bind(server_addr)
        except socket.error as e:
            print(f"O2lite: socket error: {e}")
            return O2L_FAIL

        if self.udp_recv_port == 0:  # Python3 only
            # If port was 0, find the port that was allocated
            udp_port = sock.getsockname()[1]
            self.udp_recv_port = udp_port
        # print(f"Bind UDP receive socket to port: {self.udp_recv_port}")
        return O2L_SUCCESS


    def send_services(self):
        if "d" in self.debug_flags:
            print("O2lite: send_services:", self.services,
                  "bridge_id", self.bridge_id)

        if self.bridge_id < 0:
            return

        s = self.services

        while s:  # Loop until the end of the services string
            comma_index = s.find(',')
            if comma_index == -1:
                service_name = s
                s = ''
            else:
                service_name = s[:comma_index]
                s = s[comma_index + 1:]

            if len(service_name) > 31:  # Check if the service name is too long
                print("o2lite error, service name too long:", service_name)
                return

            # print("send_services: sending", service_name, "to go:", s)

            # Process the service name
            # print("Service:", service_name)
            self.send_start("!_o2/o2lite/sv", 0, "siisi", True)
            self.add_string(service_name)
            self.add_int(1)
            self.add_int(1)
            self.add_string("")
            self.add_int(0)
            self.send()


    def set_services(self, services):
        self.services = services
        self.send_services()


    def msg_dispatch(self, msg):
        # get the address
        address_start = 12
        address_end = msg.find(b'\x00', address_start)
        # O2lite_handler.address is a string starting after the leading
        # "/" or "!", so make address compatible:
        address = msg[address_start + 1 : address_end].decode('utf-8')

        # debugging output requested?
        if 'r' in self.debug_flags:
            print(f"O2lite: received {len(msg)} bytes for /{address} ", end="")
            if 'b' in self.debug_flags:  # print actual bytes
                print("(", msg, ")", end="")
            print()

        # get the typespec
        typespec_start = msg.find(b',', address_end)
        # typespec_start = address_end + 1
        typespec_end = msg.find(b'\x00', typespec_start)
        typespec = msg[typespec_start + 1 : typespec_end]

        data_start = (typespec_end + 4) & ~3  # get the data after zero pad

        for h in self.handlers:
            if h.full:
                if h.address != address or \
                   (h.typespec is not None and h.typespec != typespec):
                    continue
            else:
                if not all(a == b for a, b in zip(h.address, addresss)):
                    continue
                if (address[len(h.address)] not in ['\0', '/']) or \
                   (h.typespec is not None and h.typespec != typespec):
                    continue

            # Setup for parsing
            self.parse_msg = msg
            # address is missing the initial character, so restore it:
            self.parse_address = chr(msg[address_start]) + address
            self.parse_cnt = data_start
            self.parse_error = False
            self.parse_types = typespec.decode('utf-8')
            self.parse_types_index = 0


            h.handler(msg, address, self.parse_types, h.info)
            return

        print(f"O2lite: no match, dropping msg to {address}.")


    def read_from_tcp(self):
        tcp_len_got = 0
        tcp_msg_got = 4
        tcp_in_msg_length = 0

        # Receive the length of the message. For simplicity, assume we
        # always get the first 4 bytes of the message in one recv() call:
        msg_length = self.tcp_socket.recv(4)
        if not msg_length:  # i.e. error occurred
            if "d" in self.debug_flags:
                print("O2lite: read_from_tcp got nothing, closing tcp.")
            return self.tcp_close()  # Error or connection closed
        tcp_in_msg_length = int.from_bytes(msg_length, 'big')

        capacity = MAX_MSG_LEN - 4  # 4 bytes for the length field

        if tcp_in_msg_length > capacity:
            # Discard the message if it's too long
            while tcp_msg_got < tcp_in_msg_length:
                togo = min(tcp_in_msg_length - tcp_msg_got, capacity)
                data = self.tcp_socket.recv(togo)
                if not data:
                    if "d" in self.debug_flags:
                        print("O2lite: read_from_tcp no data, closing tcp.")
                    return self.tcp_close()  # Error or connection closed
                tcp_msg_got += len(data)
            return  # no message could be received

        # Receive the rest of the message, allow multiple recv's:
        tcpinbuf = b''
        while len(tcpinbuf) < tcp_in_msg_length:
            data = self.tcp_socket.recv(tcp_in_msg_length - len(tcpinbuf))
            if not data:  # i.e. an error occurred
                return None  # Error or connection closed
            tcpinbuf += data
        if 'b' in self.debug_flags:
            print("O2lite: got", len(tcpinbuf), "bytes (", tcpinbuf,
                  ") via TCP.")
        self.msg_dispatch(tcpinbuf)


    def read_from_udp(self):
        try:
            data, addr = self.udp_recv_sock.recvfrom(MAX_MSG_LEN)
            if data:
                # Since UDP does not guarantee delivery, no further
                # action on error or no data:
                if 'b' in self.debug_flags:
                    print("O2lite: got", len(data), "bytes (", data,
                          ") via UDP.")
                self.msg_dispatch(data)
                return "O2L_SUCCESS"
            else:
                # No data received
                return "O2L_FAIL"
        except Exception as e:
            # Handle exceptions, such as a socket error
            print("o2lite recvfrom_udp error:", e)
            return "O2L_FAIL"


    def tcp_close(self):
        self.tcp_socket.close()
        self.tcp_socket = None
        self.bridge_id = -1  # no connection, so bridge_id invalid
        return None
        

    def method_new(self, path, typespec, full, handler, info):
        self.handlers.append(O2lite_handler(path, typespec, full,
                                            handler, info))


    def network_poll(self):
        if self.tcp_socket is None:
            host = self.discovery.get_host()
            if host:
                self.network_connect(host["ip"], host["tcp_port"])
                self.udp_send_address = o2l_address_init(
                        host["ip"], host["udp_port"], False)
            elif self.idle_start_time == 1e7:  # start timeout:
                self.idle_start_time = self.local_now
            elif self.local_now > self.idle_start_time + 20:  # timed out:
                self.discovery.restart()
                self.idle_start_time = 1e7

        self.socket_list = []
        if self.tcp_socket is not None:
            self.add_socket(self.tcp_socket)
        if self.udp_recv_sock is not None:
            self.add_socket(self.udp_recv_sock)

        if len(self.socket_list) == 0:
            return

        readable, _, _ = select.select(self.socket_list, [], [], 0)

        if self.tcp_socket in readable:
            # print("O2lite: network_poll got TCP msg")
            self.read_from_tcp()

        if self.udp_recv_sock in readable:
            # print("O2lite: network_poll got UDP msg")
            self.read_from_udp()


    def network_connect(self, ip, port):
        if 'g' in self.debug_flags:
            print("O2lite: connecting to host, ip", ip, "port", port)

        # Initialize server address for TCP
        server_addr = o2l_address_init(ip, port, True)

        # Create a TCP socket
        self.tcp_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

        # Attempt to connect
        try:
            self.tcp_socket.connect(server_addr)
            # set TCP_NODELAY flag. If TCP_NODELAY is defined we use
            # it. But MicroPython does not define TCP_NODELAY.
            if hasattr(socket, 'TCP_NODELAY'):
                self.tcp_socket.setsockopt(socket.IPPROTO_TCP, tcp_nodelay,
                                           socket.TCP_NODELAY)
            if "g" in self.debug_flags:
                print(f"O2lite: connected to {ip} on port {port}.")
        except OSError as e:
            print(f"O2lite: connection failed: {e}.")
            self.tcp_close()

        self.send_start("!_o2/o2lite/con", 0, "si", True)
        # print(f"O2lite: sending !_o2/o2lite/con si {self.internal_ip}",
        #       self.udp_recv_port)
        self.add_string(self.internal_ip)
        self.add_int(self.udp_recv_port)
        self.send()


    def get_error(self):
        return self.error

    
######## Message Parsing ###########

    def _check_error(self, byte_count, typecode):
        if self.parse_cnt + byte_count > len(self.parse_msg):
            print("O2lite: parse error reading message to",
                  f"{self.parse_address}, message too short.")
            raise ValueError("Parse error")
        if self.parse_types_index < len(self.parse_types) and \
           typecode != self.parse_types[self.parse_types_index]:
            print("O2lite: parse error reading message to",
                  f"{self.parse_address}, expected type {typecode}",
                  "but got type", \
                  "EOS" if self.parse_types_index >= len(self.parse_types) \
                        else self.parse_types[self.parse_types_index])
                  
            raise ValueError("Parse error")
        self.parse_types_index += 1

        
    def _read_data(self, byte_count, format_string, typecode):
        # print("_read_data:", byte_count, "bytes, typecode", typecode,
        #       "offset", self.parse_cnt, "into", self.parse_msg)
        self._check_error(byte_count, typecode)
        value = struct.unpack(format_string,
                    self.parse_msg[self.parse_cnt :
                                   self.parse_cnt + byte_count])
        self.parse_cnt += byte_count
        # print("_read_data", format_string, value[0])
        return value[0]

    
    def get_int32(self):
        return self._read_data(4, '>i', 'i')  # Read 4 bytes as big-endian int32

    def get_time(self):
        return self._read_data(8, '>d', 't')  # Read 8 as big-endian double

    def get_float(self):
        return self._read_data(4, '>f', 'f')  # Read 4 bytes as big-endian float

    def get_string(self):
        """Get a string parameter from a message."""
        # Note that we cannot directly use _check_error() because we
        # do not know the string length (it is arbitrary and depends
        # on the message).  Therefore, we have to scan the message
        # here to determine the length.  But first, we should check
        # for type code 's'. Since we do both of these here, we only
        # call _check_error when we encounter an error, using
        # _check_error merely as an error reporting and
        # exception-raising function -- we know it will report an
        # error when we call it.
        if self.parse_types[self.parse_types_index] != 's':
            self._check_error(4, 's')  # force type error reporting
        self.parse_types_index += 1
        start = self.parse_cnt
        end = self.parse_cnt + 1
        while end < len(self.parse_msg) and self.parse_msg[end] != 0:
            end += 1
        if end >= len(self.parse_msg):
            self._check_error(end - start, 's')  # force length error reporting
        try:
            extracted_string = self.parse_msg[start : end].decode('utf-8')
        except UnicodeDecodeError:
            raise ValueError("Error decoding string")

        # Align the parse count to 4 bytes
        self.parse_cnt = (end + 4) & ~3

        return extracted_string
