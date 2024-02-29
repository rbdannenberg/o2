# o2lite.py -- Python equivalent of o2lite.h
#
# Zekai Shen and Roger B. Dannenberg
# Feb 2024

import ctypes
import math
import select
import socket
import struct
import sys
import time
from threading import Lock

from src.msg_parser import MessageParser
from src.o2lite_discovery import O2LiteDiscovery
from src.o2lite_handler import O2LiteHandler
from util.byte_swap import o2lswap32
from util.ip_util import find_my_ip_address, hex_to_ip

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


class O2Lite:
    def __init__(self):
        # basic info
        self.udp_send_address = None
        self.socket_list = []
        self.o2n_internal_ip = None
        self.udp_recv_port = 0
        self.clock_sync_id = 0
        # discovery
        self.most_recent_host_lock = Lock()
        self.most_recent_host = None
        # use 0 instead of -1 in python
        self.o2l_bridge_id = 0
        self.handlers = []
        self.services = None
        self.error = False
        # message
        self.tcpinbuf = bytearray(MAX_MSG_LEN)
        self.udpinbuf = bytearray(MAX_MSG_LEN)
        self.outbuf = bytearray(256)
        self.tcp_len_got = 0
        self.tcp_msg_got = 0
        self.udp_in_msg = self.udpinbuf
        self.tcp_in_msg = self.tcpinbuf
        self.out_msg = self.outbuf
        self.parse_msg = None  # incoming message to parse
        self.parse_cnt = 0  # how many bytes retrieved
        self.max_parse_cnt = 0  # how many bytes can be retrieved
        self.parse_error = False  # was there an error parsing message?
        self.out_msg_cnt = 0  # how many bytes written to outbuf
        self.num_msg = 0
        # socket
        self.udp_recv_sock = None
        self.udp_send_sock = None
        self.tcp_socket = None
        self.udp_socket = None
        # clock sync
        self.o2l_local_now = -1
        self.clock_initialized = False
        self.clock_synchronized = False
        self.ping_reply_count = 0
        self.global_minus_local = 0
        self.start_time = 0
        self.start_sync_time = None
        self.time_for_clock_ping = 1e7
        self.clock_ping_send_time = None
        self.rtts = [0.0] * CLOCK_SYNC_HISTORY_LEN
        self.ref_minus_local = [0.0] * CLOCK_SYNC_HISTORY_LEN


    def initialize(self, ensemble_name):
        self.ensemble_name = ensemble_name
        self.discovery = O2LiteDiscovery(ensemble_name, self)
        # Initialize clock (if not disabled)
        if O2L_CLOCKSYNC:
            self.clock_initialize()

        self.method_new("!_o2/id", "i", True, self.id_handler, None)

        # Create UDP send socket
        try:
            self.udp_send_sock = socket.socket(socket.AF_INET,
                                               socket.SOCK_DGRAM, 0)
        except socket.error as e:
            print("allocating udp send socket:", e)
            return O2L_FAIL

        self.udp_recv_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM,
                                           socket.IPPROTO_UDP)
        if self.udp_recv_sock == socket.error:
            print("o2lite: udp socket creation error")
            return O2L_FAIL

        if self.get_recv_udp_port(self.udp_recv_sock) != 0:
            print("o2lite: could not allocate udp_recv_port")
            return O2L_FAIL
        else:
            print("o2lite: UDP server port", self.udp_recv_port)

        self.o2n_internal_ip = find_my_ip_address()

        # running discovery
        self.discovery.run_discovery_continuous()


    def send_cmd(self, *args):
        self.send(*args, tcp=True)
        

    def send(self, *args, tcp=False):
        if len(args) > 0:  # this is like the all-in-one o2_send(...)
            type_string = ""
            if len(args) > 2:  # there is a type string
                type_string = args[2]
            self.send_start(args[0], args[1], type_string, tcp)
            for type_char, arg in zip(type_string, args):
                if type_char == 'i':
                    o2l.add_int(arg)
                elif type_char == 'f':
                    o2l.add_float(arg)
                elif type_char == 's':
                    o2l.add_string(arg)
                elif type_char == 't':
                    o2l.add_time(arg)
            # now we fall through to the send() code
        if self.error or self.tcp_socket is None:
            return

        self.add_length()

        misc = struct.unpack('I', self.outbuf[4:8])[0]

        if misc & o2lswap32(O2_TCP_FLAG):
            bytes_sent = self.tcp_socket.send(self.outbuf[:self.out_msg_cnt])
            print("send through tcp socket")
            print("sending # bytes: ", bytes_sent)
            print("content: ", self.outbuf[:self.out_msg_cnt])
        else:
            bytes_sent = self.udp_send_sock.sendto(
                    self.outbuf[4:self.out_msg_cnt], self.udp_send_address)
            print("send through udp socket")
            print("sending # bytes: ", bytes_sent)
            print("content: ", self.outbuf[4:self.out_msg_cnt])

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


    def poll(self):
        self.o2l_local_now = self.local_time()

        if O2L_CLOCKSYNC:
            if self.time_for_clock_ping < self.o2l_local_now:
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
        parser = MessageParser(data)

        id_in_data = parser.o2l_get_int32()

        if id_in_data != self.clock_sync_id:
            return

        rtt = self.o2l_local_now - self.clock_ping_send_time
        ref_time = parser.o2l_get_time() + rtt * 0.5

        if self.parse_error:
            return

        i = self.ping_reply_count % CLOCK_SYNC_HISTORY_LEN
        self.rtts[i] = rtt
        self.ref_minus_local[i] = ref_time - self.o2l_local_now

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
                print("o2lite: clock synchronized")
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
        parser = MessageParser(data)

        self.o2l_bridge_id = parser.o2l_get_int32()
        print("o2lite: got id =", self.o2l_bridge_id)
        # We're connected now, send services if any
        self.send_services()
        if O2L_CLOCKSYNC:
            # Sends are synchronous. Since we just sent a bunch of messages,
            # take 50ms to service any other real-time tasks before this:
            self.time_for_clock_ping = self.o2l_local_now + 0.05
            # When does syncing start?:
            self.start_sync_time = self.time_for_clock_ping


    def clock_initialize(self):
        if self.clock_initialized:
            o2l_clock_finish()

        if O2L_CLOCKSYNC:
            self.method_new("!_o2/cs/put", "it", True,
                            self.ping_reply_handler, None)

        # initialize start time
        self.start_time = time.perf_counter()

        self.global_minus_local = 0
        self.clock_synchronized = False
        self.ping_reply_count = 0

        print("o2lite: initializing clock")


    def clock_ping(self):
        self.clock_ping_send_time = self.o2l_local_now
        self.clock_sync_id += 1
        self.send_start("!_o2/o2lite/cs/get", 0, "iis", False)
        self.add_int(self.o2l_bridge_id)
        self.add_int(self.clock_sync_id)
        self.add_string("!_o2/cs/put")
        self.send()
        self.time_for_clock_ping = self.clock_ping_send_time + 0.1
        if self.clock_ping_send_time - self.start_sync_time > 1:
            self.time_for_clock_ping += 0.4
        if self.clock_ping_send_time - self.start_sync_time > 5:
            self.time_for_clock_ping += 9.5


    def sleep(self, n):
        time.sleep(n / 1000)


    def time_get(self):
        if self.clock_synchronized:
            return self.local_time() + self.global_minus_local
        else:
            return -1


    def local_time(self):
        """
        Get the current local time in seconds, with the precision of 
        fractional seconds, relative to 'start_time'.
        """
        current_time = time.perf_counter()  # Get current high-resolution time
        elapsed_time = current_time - self.start_time  # Calculate elapsed secs
        return elapsed_time


    def get_recv_udp_port(self, sock):
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

        server_addr = ("", self.udp_recv_port)

        try:
            sock.bind(server_addr)
        except socket.error as e:
            print(f"Socket error: {e}")
            return O2L_FAIL

        if self.udp_recv_port == 0:
            # If port was 0, find the port that was allocated
            udp_port = sock.getsockname()[1]
            self.udp_recv_port = udp_port
            print(f"Allocated port: {self.udp_recv_port}")
        return O2L_SUCCESS


    def send_services(self):
        if self.o2l_bridge_id < 0:
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
                print("service name too long")
                return

            # Process the service name
            print("Service:", service_name)
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
        address = msg[address_start:address_end]

        # get the typespec
        typespec_start = msg.find(b',', address_end)
        # typespec_start = address_end + 1
        typespec_end = msg.find(b'\x00', typespec_start)
        typespec = msg[typespec_start + 1:typespec_end]

        data_start = (math.floor(typespec_end / 4) + 1) * 4  # get the data
        data = msg[data_start:]

        for h in self.handlers:
            if h.full:
                if h.address[1:] != address[1:].decode('utf-8') or \
                        (h.typespec is not None and h.typespec != typespec):
                    continue
            else:
                if not all(a == b for a, b in zip(h.address[1:], address[1:])):
                    continue
                if (address[1:][len(h.address[1:])] not in ['\0', '/']) or \
                        (h.typespec is not None and h.typespec != typespec[1:]):
                    continue

            # Setup for parsing
            self.parse_msg = msg
            # +2 for null terminators:
            self.parse_cnt = len(address) + len(typespec) + 2
            self.parse_error = False
            # self.max_parse_cnt = len(msg.length) + 4

            h.handler(msg=msg, types=typespec, data=data, info=h.info)
            return

        print(f"o2l_dispatch dropping msg to {address}")


    def read_from_tcp(self):
        tcp_len_got = 0
        tcp_msg_got = 0
        tcp_in_msg_length = 0

        # Receive the length of the message
        while tcp_len_got < 4:
            msg_length = self.tcp_socket.recv(4 - tcp_len_got)
            if not msg_length:
                return None  # Error or connection closed

            tcp_in_msg_length += int.from_bytes(msg_length, byteorder='big')
            tcp_len_got += len(msg_length)

        if tcp_len_got == 4:
            # tcp_in_msg_length = o2lswap32(tcp_in_msg_length)
            capacity = MAX_MSG_LEN - 4  # 4 bytes for the length field

            if tcp_in_msg_length > capacity:
                # Discard the message if it's too long
                while tcp_msg_got < tcp_in_msg_length:
                    togo = min(tcp_in_msg_length - tcp_msg_got, capacity)
                    data = self.tcp_socket.recv(togo)
                    if not data:
                        return None  # Error or connection closed
                    tcp_msg_got += len(data)
                self.cleanup_tcp_msg()
                return None

        # Receive the rest of the message
        while tcp_msg_got < tcp_in_msg_length:
            self.tcpinbuf = self.tcp_socket.recv(tcp_in_msg_length -
                                                 tcp_msg_got)
            if not msg_length:
                return None  # Error or connection closed
            tcp_msg_got += len(self.tcpinbuf)
            # Append data to the message (handle accordingly)
            print("got message: ", self.tcpinbuf)

            self.msg_dispatch(self.tcpinbuf)
            self.cleanup_tcp_msg()


    def read_from_udp(self):
        try:
            data, addr = self.udp_recv_sock.recvfrom(MAX_MSG_LEN)
            if data:
                # Since UDP does not guarantee delivery, no further
                # action on error or no data:
                print("got message: ", data)
                self.msg_dispatch(data)
                return "O2L_SUCCESS"
            else:
                # No data received
                return "O2L_FAIL"
        except Exception as e:
            # Handle exceptions, such as a socket error
            print("recvfrom in udp_recv_handler error:", e)
            return "O2L_FAIL"


    def cleanup_tcp_msg(self):
        self.tcp_len_got = 0
        self.tcp_msg_got = 0


    def method_new(self, path, typespec, full, handler, info):
        self.handlers.append(O2LiteHandler(path, typespec, full, handler, info))


    def update_most_recent_host(self, host):
        with self.most_recent_host_lock:
            self.most_recent_host = host


    def network_poll(self):
        with self.most_recent_host_lock:
            if self.tcp_socket is None and self.most_recent_host is not None:
                # TODO: internal ip or socket ip
                # internal_ip = hex_to_ip(self.o2n_internal_ip)
                self.network_connect(self.most_recent_host["ip"], self.most_recent_host["tcp_port"])
                self.udp_send_address = o2l_address_init(self.most_recent_host["ip"],
                                                         self.most_recent_host["udp_port"],
                                                         False)
                self.most_recent_host = None

        self.socket_list = []
        if self.tcp_socket is not None:
            self.add_socket(self.tcp_socket)
        if self.udp_recv_sock is not None:
            self.add_socket(self.udp_recv_sock)

        if len(self.socket_list) == 0:
            return

        readable, _, _ = select.select(self.socket_list, [], [], 0)

        if self.tcp_socket in readable:
            print("o2lite: network_poll got TCP msg")
            self.read_from_tcp()

        if self.udp_recv_sock in readable:
            print("o2lite: network_poll got UDP msg")
            self.read_from_udp()


    def network_connect(self, ip, port):
        # Initialize server address for TCP
        server_addr = o2l_address_init(ip, port, True)

        # Create a TCP socket
        self.tcp_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

        # Attempt to connect
        try:
            self.tcp_socket.connect(server_addr)
            # set TCP_NODELAY flag
            self.tcp_socket.setsockopt(socket.IPPROTO_TCP,
                                       socket.TCP_NODELAY, 1)
            print(f"Connected to {ip} on port {port}")
        except socket.error as e:
            print(f"Connection failed: {e}")
            self.tcp_socket.close()

        self.send_start("!_o2/o2lite/con", 0, "si", True)
        print("o2lite: sending !_o2/o2lite/con")
        self.add_string(self.o2n_internal_ip)
        self.add_int(self.udp_recv_port)
        self.send()


    def get_error(self):
        return self.error
