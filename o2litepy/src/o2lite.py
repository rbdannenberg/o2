# O2lite.py -- Python equivalent of o2lite.h
#
# Zekai Shen and Roger B. Dannenberg
# Feb 2024

# O2lite functions are all methods within the class O2lite.
# In this documentation, we assume you have a single instance
# of O2lite named o2lite. Thus, we write o2lite.time_get() to
# indicate a call to the time_get() method of O2lite. Of course,
# you can use a name other than o2lite.

# EXAMPLES
# Receiving a string from ensemble "test":
"""
from o2litepy import O2lite
o2lite = O2lite()
o2lite.initialize("test", debug_flags="a")
o2lite.set_services("example")
o2lite.method_new("/example/str", "s", True, str_handler, None)
def str_handler(address, types, info):
    print("got a string:", o2lite.get_string())
o2lite.sleep(30)  # polls for 30 seconds
"""
# Sending a float to service "sensor", ensemble "test":
"""
from o2litepy import O2lite
o2lite = O2lite()
o2lite.initialize("test", debug_flags="a")
# wait for connect and clock sync with host:
while o2lite.time_get() < 0:
    o2lite.sleep(1)

# send (reliably) now that we are connected:
o2lite.send_cmd("/test/sensor", "f", 3.14)

# poll for another second (exiting immediately or not polling
# may close the connection before message delivery completes):
o2lite.sleep(1) 
# Normally, a "main loop" will call o2lite.poll() frequently
# so you do not need to explicitly call o2lite.sleep() after
# sending.
"""
#
# TIME:
# o2lite.local_now is the current local time, updated every
#     O2lite.poll(), which starts from 0
# o2lite.time_get() retrieves the global time in seconds, but -1
#     until clock synchronization completes. (Do not confuse with
#     get_time(), which retrieves a time from an O2 message.)
# o2lite.local_time() retrieves the local time (local_now is identical
#     within the polling period and should be faster).
#
# DEBUGGING:
# debug_flags is a string used to set some debugging options:
#   b -- print actual bytes of messages
#   s -- print O2 messages when sent
#   r -- print O2 messages when received
#   d -- print info about discovery
#   g -- general debugging info
#   a -- all debugging messages except b
# Setting any flag automatically enables "g". debug_flags can be
# set directly, or debug flags can be passed as a parameter to
# initialize().
#
# INITIALIZATION
# You should create *one* instance of the O2lite class, e.g.
#     o2lite = O2lite().
# o2lite.initialize(ensemble_name, debug_flags="") must be called
#     before using any other O2lite methods.
#
# O2 TYPES
# This library supports the following data type codes and types in
# messages:
#     'i' 32-bit signed integer (Python int)
#     'f' 32-bit IEEE float (Python float)
#     's' string (Python str)
#     't' 64-bit double time-stamp (Python float)
# This limited set is intentional to minimize the size of o2lite
# implementations, but it is likely to expand with more types.
#
# SENDING MESSAGES
# Messages are sent with either send(), to send via UDP, or
# send_cmd(), to send via TCP:
# o2lite.send(address, time, type_string, data1, data2, data3, ...)
#     address is the destination address
#     time is the delivery time (double in seconds).  All messages are
#         delivered immediately and dispatched at the given timestamp.
#         Use zero for "as soon as possible."
#     type_string is a string of type codes (see above).
#     data parameters are actual values for the message.
#     The message is sent via UDP. For example,
#         o2lite.send("/host/info", "isf", 57, "hello", 3.14) will
#     send a messages to O2 address "/host/info" with integer, string
#     and float values as shown.
# o2lite.send_cmd(address, time, type_string, data1, data2, data3, ...)
#     is similar to o2lite.send() except the message is sent via TCP.
#
# RECEIVING MESSAGES
# Messages are directed to services. To receive a message you
# first state the services you offer in this process using
# o2lite.set_services(services) where services is a string of
#     *all* your service names separated by commas, e.g.
#         o2lite.set_services("self,services")
#     This call can be made even before calling initialize, and
#     it can be made again if the set of services changes.
# Next, you need to register a handler for each address or
# address prefix to be handled using:
# o2lite.method_new(path, type_string, full, handler, info) where
#     path is the complete O2 address including the service name,
#     type_string is the type string expected for the message (""
#         means no parameters and None means parameter types are
#         not checked: the handler decides what to accept),
#     full is true if the address is the full address, or false
#         to accept any message to an address that begins with
#         path (addresses are checked node-by-node, so /serv1/foo
#         is a prefix of /serv1/foo/bar, but /serv1/foo is *not*
#         a prefix of /serv1/foobar),
#     handler is a function to be called. The signature of the
#         function is handler(address, type_string, info),
#     info is additional data to pass on to the handler, e.g.
#         you can attach the same handler to addresses /serv/1,
#         /serv/2, /serv/3 and quickly distinguish them by passing
#         in info values of 1, 2, and 3, respectively, or info
#         could be an object for object-oriented handlers, e.g.
#             def handler(a, tm, ty, info): info.handler(a, tm, ty)
# Finally, you need to declare a handler. The handler will typically
# begin by extracting parameters from the message using the
# following functions:
#
# o2lite.get_blob() check for and return an O2blob
# o2lite.get_bool() check for and return a boolean
# o2lite.get_double() check for and return a 64-bit float (double)
# o2lite.get_float() check for and return a 32-bit float
# o2lite.get_int32() check for and return a 32-bit integer
# o2lite.get_int64() check for and return a 64-bit integer
# o2lite.get_string() check for and return a string
# o2lite.get_time() check for and return a time (double with type 't')
#
# Values are returned sequentially from the message and the sequence
# of requests must match the sequence of types in the message
# (available in the type_string parameter, but you can assume the
# strings match the type_string specified in method_new()).
#
#
# IMPLEMENTATION DETAILS ON DISCOVERY:
# discovery will find services representing O2 hosts as quickly
# as possible. We could initiate connections with all possible
# hosts, but since O2lite only connects to one host, it is better
# to attempt connections sequentially until one is successful.
# Normally, we will connect to the first host and not bother with
# the rest.
#
# To conduct orderly, sequential connection attempts, we
# call discovery.get_host() if no host is connected and no
# connection is in progress. This is done in our polling loop.
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
#
# ADDITIONAL INTERNAL METHODS OF INTEREST
# o2l_sys_time() is a system-dependent function that returns time
#     in seconds (for internal use to implement O2lite.local_time())
# o2l_start_time (for internal use) is the start time of the
#     reference clock (may or may not be 0)
# O2lite.global_minus_local is (for internal use) the skew between
#     global and local time as estimated by clock synchronization.
# Caution: O2lite.get_time() and O2lite.add_time() are for message
#     reading and constructing; they do not tell time


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
    from py3fns import o2l_sys_time, find_my_ip_address
    from py3discovery import Py3discovery as O2lite_discovery
    # python can bind to port 0, allocate a free port, and tell you what it is:
    initial_udp_recv_port = 0  # chosen randomly from unassigned range
from o2lite_handler import O2lite_handler
from byte_swap import o2lswap32

# constants and flags
O2L_VERSION = 0x020000
O2L_SUCCESS = 0
O2L_FAIL = -1
O2L_ALREADY_RUNNING = -5

O2_UDP_FLAG = 0
O2_TCP_FLAG = 1

MAX_MSG_LEN = 4096
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


class O2blob:
    def __init__(self, size=0, data=bytearray(0)):
        self.size = size
        self.data = data


class O2lite:
    def __init__(self):
        # basic info
        self._udp_send_address = None
        self._socket_list = []
        self.internal_ip = None
        self._udp_recv_port = initial_udp_recv_port
        self._clock_sync_id = 0
        # discovery
        self.bridge_id = -1  # "no bridge"
        self.handlers = []
        self.services = None
        self._idle_start_time = 1e7
        self.error = False
        # state for constructing messages
        self._outbuf = bytearray(MAX_MSG_LEN)
        self._out_msg_address = ""
        self.msg_timestamp = 0

        # state for unpacking parameters from messages
        self._parse_msg = None  # incoming message to parse
        self._parse_address = None  # extracted address from parse_msg
        self._parse_cnt = 0  # how many bytes retrieved
        self.max_parse_cnt = 0  # how many bytes can be retrieved
        self._parse_types = None  # the type bytes, e.g. b'if'
                # (without the ','); changed from bytes to string,
                # e.g. "if", before handler is called
        self.parse_error = False  # was there an error parsing message?
        self._parse_type_index = 0  # index of next type character

        self._out_msg_cnt = 0  # how many bytes written to outbuf
        self.num_msg = 0
        # socket
        self._udp_recv_sock = None
        self._udp_send_sock = None
        self._tcp_socket = None
        # clock sync
        self.local_time = o2l_sys_time  # system-dependent implementation
        self.local_now = -1
        self._clock_initialized = False
        self.clock_synchronized = False
        self.ping_reply_count = 0
        self.global_minus_local = 0
        self.start_sync_time = None
        self._time_for_clock_ping = 1e7
        self._clock_ping_send_time = None
        self.rtts = [0.0] * CLOCK_SYNC_HISTORY_LEN
        self.ref_minus_local = [0.0] * CLOCK_SYNC_HISTORY_LEN
        self.debug_flags = ""


    def initialize(self, ensemble_name, debug_flags=""):
        self.ensemble_name = ensemble_name

        # expand "a" to "srd" (all debug flags but "b" enabled)
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
            self._clock_initialize()

        self.method_new("!_o2/id", "i", True, self._id_handler, None)

        # Create UDP send socket
        try:
            self._udp_send_sock = socket.socket(socket.AF_INET,
                                               socket.SOCK_DGRAM, 0)
        except socket.error as e:
            print("O2lite: allocating udp send socket:", e)
            return O2L_FAIL

        self._udp_recv_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM,
                                           socket.IPPROTO_UDP)
        # I think this cannot fail, or perhaps raises an error:
        # if self._udp_recv_sock == socket.error:
        #     print("O2lite: udp socket creation error")
        #     return O2L_FAIL

        if self._get_recv_udp_port(self._udp_recv_sock) != 0:
            print("O2lite: could not allocate udp_recv_port")
            return O2L_FAIL
        elif "g" in self.debug_flags:
            print("O2lite: UDP server port", self._udp_recv_port)

        self.internal_ip = find_my_ip_address()

        # running discovery
        self.discovery.run_discovery()


    def send_cmd(self, addr, timestamp, *args):
        self.send(addr, timestamp, *args, tcp=True)
        

    def send(self, addr, timestamp, *args, tcp=False):
        type_string = ""
        if len(args) > 1:  # there is a type string
            type_string = args[0]
        self._send_start(addr, timestamp, type_string, tcp)
        i = 1
        for type_char in type_string:
            if type_char == 'i':
                self._add_int(args[i])
            elif type_char == 'f':
                self._add_float(args[i])
            elif type_char == 's':
                self._add_string(args[i])
            elif type_char == 'd':
                self._add_double(args[i])
            elif type_char == 't':
                self._add_time(args[i])
            elif type_char == 'h':
                self._add_int64(args[i])
            elif type_char == 'b':
                self._add_blob(args[i])
            elif type_char == 'B':
                self._add_bool(args[i])
            else:
                raise Exception("O2 type character " + type_char + \
                                " not recognized")
            i += 1
        # now we fall through to the send() code
        if self.error or self._tcp_socket is None:
            return

        self._add_length()

        if tcp:
            bytes_sent = self._tcp_socket.send(
                             self._outbuf[ : self._out_msg_cnt])
        elif self._udp_send_address:
            bytes_sent = self._udp_send_sock.sendto(
                    self._outbuf[4 : self._out_msg_cnt], self._udp_send_address)
        else:
            print("Error: cannot send, no udp_send_address.")

        # debug printing requested?
        if 's' in self.debug_flags:
            self.msg_print(self._outbuf[4 : self._out_msg_cnt], "sending")

        self.num_msg += 1


    def _send_start(self, address, time, types, tcp):
        if tcp:
            flag = O2_TCP_FLAG
        else:
            flag = O2_UDP_FLAG

        self.parse_error = False
        self._out_msg_cnt = 4
        self._add_int(flag)
        self._add_time(time)
        self._add_string(address)
        self._outbuf[self._out_msg_cnt] = ord(',')
        self._out_msg_cnt += 1
        self._add_string(types)
        self._out_msg_address = address  # save for possible debug output


    def _add_length(self):
        msg_length_excluding_length_field = 4
        self._outbuf[0 : 4] = struct.pack(">I",
               self._out_msg_cnt - msg_length_excluding_length_field)


    def poll(self):
        self.local_now = self.local_time()

        if O2L_CLOCKSYNC:
            if self._time_for_clock_ping < self.local_now:
                self._clock_ping()

        self.network_poll()


    def _add_blob(self, x):
        n = x.size
        if self._out_msg_cnt + n + 4 > MAX_MSG_LEN:
            return
        self._add_int(n)
        self._outbuf[self._out_msg_cnt : self._out_msg_cnt + n] = x.data
        self._out_msg_cnt += n
        

    def _add_bool(self, b):
        return self._add_int32(1 if b else 0)


    def _add_double(self, d):
        if self._out_msg_cnt + 8 > MAX_MSG_LEN:
            return
        packed_time = struct.pack(">d", d)  # Pack as big-endian double
        self._outbuf[self._out_msg_cnt : self._out_msg_cnt + 8] = packed_time
        self._out_msg_cnt += 8


    def _add_float(self, x):
        if self._out_msg_cnt + 4 > MAX_MSG_LEN:  # sizeof(float) is 4 bytes
            return
        packed_float = struct.pack(">f", x)  # Pack as big-endian float

        self._outbuf[self._out_msg_cnt : self._out_msg_cnt + 4] = packed_float
        self._out_msg_cnt += 4


    def _add_int(self, i):
        return self._add_int32(i)


    def _add_int32(self, i):
        if self._out_msg_cnt + 4 > MAX_MSG_LEN:  # sizeof(int32_t) is 4 bytes
            return

        # Pack the integer with the endian swap and add it to the buffer
        self._outbuf[self._out_msg_cnt : self._out_msg_cnt + 4] = \
              struct.pack(">I", i)
        self._out_msg_cnt += 4


    def _add_int64(self, i):
        if self._out_msg_cnt + 8 > MAX_MSG_LEN:  # sizeof(int32_t) is 4 bytes
            return

        # Pack the integer with the endian swap and add it to the buffer
        self._outbuf[self._out_msg_cnt : self._out_msg_cnt + 8] = \
              struct.pack(">q", i)
        self._out_msg_cnt += 8


    def _add_string(self, s):
        for char in s:
            # Check for buffer overflow including space for null terminator
            if self._out_msg_cnt + 2 > MAX_MSG_LEN:
                return
            self._outbuf[self._out_msg_cnt] = ord(char)
            self._out_msg_cnt += 1

        # Add null terminator
        if self._out_msg_cnt < MAX_MSG_LEN:
            self._outbuf[self._out_msg_cnt] = 0
            self._out_msg_cnt += 1

        # Pad to 4-byte boundary
        while self._out_msg_cnt % 4 != 0:
            if self._out_msg_cnt < MAX_MSG_LEN:
                self._outbuf[self._out_msg_cnt] = 0
                self._out_msg_cnt += 1
            else:
                break


    def _add_time(self, time):
        self._add_double(time)


    def ping_reply_handler(self, address, types, info):
        id_in_data = self.get_int32()

        if id_in_data != self._clock_sync_id:
            return

        rtt = self.local_now - self._clock_ping_send_time
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
                self.send_cmd("!_o2/o2lite/cs/cs", 0)
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


    def _add_socket(self, s):
        """
        Add the given socket to the read set if it is a valid socket.

        :param read_set: A list of sockets to monitor for readability.
        :param s: The socket to be added to the read set.
        """
        if s != INVALID_SOCKET:  # Assuming INVALID_SOCKET is a defined constant
            if s not in self._socket_list:
                self._socket_list.append(s)


    def _id_handler(self, address, types, info):
        self.bridge_id = self.get_int32()
        if "d" in self.debug_flags:
            print("O2lite: got id =", self.bridge_id)
        # We're connected now, send services if any
        self._send_services()
        if O2L_CLOCKSYNC:
            # Sends are synchronous. Since we just sent a bunch of messages,
            # take 50ms to service any other real-time tasks before this:
            self._time_for_clock_ping = self.local_now + 0.05
            # When does syncing start?:
            self.start_sync_time = self._time_for_clock_ping


    def _clock_initialize(self):
        if self._clock_initialized:
            o2l_clock_finish()

        if O2L_CLOCKSYNC:
            self.method_new("!_o2/cs/put", "it", True,
                            self.ping_reply_handler, None)

        self.global_minus_local = 0
        self.clock_synchronized = False
        self.ping_reply_count = 0


    def _clock_ping(self):
        if self.bridge_id <  0:  # make sure we still have a connection
            self._time_for_clock_ping += 1e7  # no more pings until connected
            return
        self._clock_ping_send_time = self.local_now
        self._clock_sync_id += 1
        self.send("!_o2/o2lite/cs/get", 0, "iis", self.bridge_id,
                  self._clock_sync_id, "!_o2/cs/put")
        self._time_for_clock_ping = self._clock_ping_send_time + 0.1
        if self._clock_ping_send_time - self.start_sync_time > 1:
            self._time_for_clock_ping += 0.4
        if self._clock_ping_send_time - self.start_sync_time > 5:
            self._time_for_clock_ping += 9.5


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


    def _get_recv_udp_port(self, sock):
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

        server_addr = ("", self._udp_recv_port)

        try:
            sock.bind(server_addr)
        except socket.error as e:
            print(f"O2lite: socket error: {e}")
            return O2L_FAIL

        if self._udp_recv_port == 0:  # Python3 only
            # If port was 0, find the port that was allocated
            udp_port = sock.getsockname()[1]
            self._udp_recv_port = udp_port
        # print(f"Bind UDP receive socket to port: {self._udp_recv_port}")
        return O2L_SUCCESS


    def _send_services(self):
        if "d" in self.debug_flags:
            print("O2lite: _send_services:", self.services,
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

            # Process the service name
            self.send_cmd("!_o2/o2lite/sv", 0, "siisi", \
                          service_name, 1, 1, "", 0)


    def set_services(self, services):
        self.services = services
        self._send_services()


    def msg_address(self, msg):
        """get the address"""
        address_start = 12
        address_end = msg.find(b'\x00', address_start)
        return msg[address_start : address_end].decode('utf-8')


    def msg_typespec(self, msg, address_len, decode = True):
        start = msg.find(b',', 12 + address_len)
        end = msg.find(b'\x00', start)
        typespec = msg[start + 1 : end]
        if decode:
            typespec = typespec.decode('utf-8')
        return (typespec, end)  # skip ','


    def _msg_start_parse(self, msg):
        # get the timestamp
        self.msg_timestamp = struct.unpack('>d', msg[4 : 12])[0]
        
        # O2lite_handler.address is a string starting after the leading
        # "/" or "!", so make address compatible:
        self._parse_address = self.msg_address(msg)
        (self._parse_types, typespec_end) = \
                  self.msg_typespec(msg, len(self._parse_address), False)
        self._parse_cnt = (typespec_end + 4) & ~3  # get the data after zero pad

        # Setup for parsing
        self._parse_msg = msg
        self.parse_error = False
        self._parse_types_index = 0
        

    def msg_print(self, msg, direction):
        """
        Print a message. Note that we cannot use our message parsing
        methods because we could, in the middle of parsing a message,
        call send() with message printing enabled and thus overwrite the
        message parsing state.
        """
        flags = struct.unpack('I', self._outbuf[4:8])[0]
        tcp = flags & o2lswap32(O2_TCP_FLAG)
        address = self.msg_address(msg)
        timestamp = struct.unpack('>d', msg[4 : 12])[0]
        (typespec, typespec_end) = self.msg_typespec(msg, len(address), True)
        cnt = (typespec_end + 4) & ~3  # get the data after zero pad

        print(f"O2lite: {direction} {len(msg)} bytes for {address}",
              f'@ {timestamp} by {"TCP" if tcp else "UDP"}',
              f'"{typespec}"', end="")
        for type_code in typespec:
            if type_code == 'i':
                print("", struct.unpack(">i", msg[cnt : cnt + 4])[0], end="")
            elif type_code == 'f':
                print("", struct.unpack(">f", msg[cnt : cnt + 4])[0], end="")
            elif type_code == 's':
                end = cnt
                while end < len(msg) and msg[end] != 0:
                    end += 1
                print("", msg[cnt : end].decode('utf-8'), end="")
                cnt = end & ~3  # now cnt is 4 bytes before end of string
            elif type_code == 't' or type_code == 'd':
                print("", struct.unpack(">d", msg[cnt : cnt + 8])[0], end="")
                cnt += 4
            elif type_code == 'h':
                print("", struct.unpack(">q", msg[cnt : cnt + 8])[0], end="")
                cnt += 4
            elif type_code == 'B':
                print("", True if struct.unpack(">i", msg[cnt : cnt + 4])[0] \
                               else False, end="")
            elif type_code == 'b':
                size = struct.unpack(">i", msg[cnt : cnt + 4])[0]
                # print("\n    msg_print blob size", msg[cnt : cnt + 4], \
                #       "==", size, "\n        ", end="")
                print(f' ({size} byte blob)', end="")
                cnt += size
            else:
                print("(unknown type", repr(type_code) + ") ...", end="")
                break
            cnt += 4
        if 'b' in self.debug_flags:  # print actual bytes
            print("\n    ", msg, end="")
        print()


    def _msg_dispatch(self, msg):
        # debugging output requested?
        if 'r' in self.debug_flags:
            self.msg_print(msg, "received")

        self._msg_start_parse(msg)
        # remove the first character which can be either / or ! to be
        # compatible with stored handler addresses:
        address = self._parse_address[1 : ]
        typespec = self._parse_types

        for h in self.handlers:
            if h.full:
                if h.address != address or \
                   (h.typespec is not None and h.typespec != typespec):
                    continue
            else:
                # address must begin with exact match to h.address:
                if not address.startswith(h.address):
                    continue
                # and whole fields are matched (/a/b does not match /a/bcd):
                if (address[len(h.address)] not in ['\0', '/']) or \
                   (h.typespec is not None and h.typespec != typespec):
                    continue
            # _parse_types is byte array, but handlers and typechecking
            # expect strings:
            self._parse_types = self._parse_types.decode('utf-8')
            h.handler(address, self._parse_types, h.info)
            return

        print(f"O2lite: no match, dropping msg to {self._parse_address}.")


    def read_from_tcp(self):
        tcp_len_got = 0
        tcp_msg_got = 4
        tcp_in_msg_length = 0

        # Receive the length of the message. For simplicity, assume we
        # always get the first 4 bytes of the message in one recv() call:
        try:
            msg_length = self._tcp_socket.recv(4)
        except ConnectionResetError:
            msg_length = None
        
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
                try:
                    data = self._tcp_socket.recv(togo)
                except ConnectionResetError:
                    data = None
                if not data:
                    if "d" in self.debug_flags:
                        print("O2lite: read_from_tcp no data, closing tcp.")
                    return self.tcp_close()  # Error or connection closed
                tcp_msg_got += len(data)
            return  # no message could be received

        # Receive the rest of the message, allow multiple recv's:
        tcpinbuf = b''
        while len(tcpinbuf) < tcp_in_msg_length:
            data = self._tcp_socket.recv(tcp_in_msg_length - len(tcpinbuf))
            if not data:  # i.e. an error occurred
                return None  # Error or connection closed
            tcpinbuf += data
        if 'b' in self.debug_flags:
            print("O2lite: got", len(tcpinbuf), "bytes (", tcpinbuf,
                  ") via TCP.")
        self._msg_dispatch(tcpinbuf)


    def read_from_udp(self):
        try:
            data, addr = self._udp_recv_sock.recvfrom(MAX_MSG_LEN)
            if data:
                # Since UDP does not guarantee delivery, no further
                # action on error or no data:
                if 'b' in self.debug_flags:
                    print("O2lite: got", len(data), "bytes (", data,
                          ") via UDP.")
                self._msg_dispatch(data)
                return "O2L_SUCCESS"
            else:
                # No data received
                return "O2L_FAIL"
        except Exception as e:
            # Handle exceptions, such as a socket error
            print("o2lite recvfrom_udp error:", e)
            return "O2L_FAIL"


    def tcp_close(self):
        self._tcp_socket.close()
        self._tcp_socket = None
        self.bridge_id = -1  # no connection, so bridge_id invalid
        return None
        

    def method_new(self, path, typespec, full, handler, info):
        self.handlers.append(O2lite_handler(path, typespec, full,
                                            handler, info))


    def network_poll(self):
        if self._tcp_socket is None:
            host = self.discovery.get_host()
            if host:
                print("network_poll found host:", host)
                self._network_connect(host["ip"], host["tcp_port"])
                self._udp_send_address = o2l_address_init(
                        host["ip"], host["udp_port"], False)
                print("udp_send_address", self._udp_send_address)
            elif self._idle_start_time == 1e7:  # start timeout:
                self._idle_start_time = self.local_now
            elif self.local_now > self._idle_start_time + 20:  # timed out:
                self.discovery.restart()
                self._idle_start_time = 1e7

        self._socket_list = []
        if self._tcp_socket is not None:
            self._add_socket(self._tcp_socket)
        if self._udp_recv_sock is not None:
            self._add_socket(self._udp_recv_sock)

        if len(self._socket_list) == 0:
            return

        readable, _, _ = select.select(self._socket_list, [], [], 0)

        if self._tcp_socket in readable:
            # print("O2lite: network_poll got TCP msg")
            self.read_from_tcp()

        if self._udp_recv_sock in readable:
            # print("O2lite: network_poll got UDP msg")
            self.read_from_udp()


    def _network_connect(self, ip, port):
        if 'g' in self.debug_flags:
            print("O2lite: connecting to host, ip", ip, "port", port)

        # Initialize server address for TCP
        server_addr = o2l_address_init(ip, port, True)

        # Create a TCP socket
        self._tcp_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

        # Attempt to connect
        try:
            self._tcp_socket.connect(server_addr)
            # set TCP_NODELAY flag. If TCP_NODELAY is defined we use
            # it. But MicroPython does not define TCP_NODELAY.
            if hasattr(socket, 'TCP_NODELAY'):
                self._tcp_socket.setsockopt(socket.IPPROTO_TCP,
                                           socket.TCP_NODELAY, 1)
            if "g" in self.debug_flags:
                print(f"O2lite: connected to {ip} on port {port}.")
        except OSError as e:
            print(f"O2lite: connection failed: {e}.")
            self.tcp_close()

        self.send_cmd("!_o2/o2lite/con", 0, "si", self.internal_ip,
                      self._udp_recv_port)
        # print(f"O2lite: sending !_o2/o2lite/con si {self.internal_ip}",
        #       self._udp_recv_port)


    def get_error(self):
        return self.error

    
######## Message Parsing ###########

    def _check_error(self, byte_count, typecode):
        if self._parse_cnt + byte_count > len(self._parse_msg):
            print("O2lite: parse error reading message to",
                  f"{self._parse_address}, message too short.")
            raise ValueError("Parse error")
        if self._parse_types_index < len(self._parse_types) and \
           typecode != self._parse_types[self._parse_types_index]:
            print("O2lite: parse error reading message to",
                  f"{self._parse_address}, expected type {typecode}",
                  "but got type", \
                  "EOS" if self._parse_types_index >= len(self._parse_types) \
                        else self._parse_types[self._parse_types_index])
                  
            raise ValueError("Parse error")
        self._parse_types_index += 1

        
    def _read_data(self, byte_count, format_string, typecode):
        # print("_read_data:", byte_count, "bytes, typecode", typecode,
        #       "offset", self._parse_cnt, "into", self._parse_msg)
        self._check_error(byte_count, typecode)
        value = struct.unpack(format_string,
                    self._parse_msg[self._parse_cnt :
                                    self._parse_cnt + byte_count])
        self._parse_cnt += byte_count
        # print("_read_data", format_string, value[0])
        return value[0]

    
    def get_blob(self):
        """Get an O2blob from a message."""
        if self._parse_types[self._parse_types_index] != 'b':
            self._check_error(4, 'b')  # force type error reporting
        value = struct.unpack('>i', self._parse_msg[self._parse_cnt :
                                                   self._parse_cnt + 4])
        size = value[0]
        self._parse_cnt += 4
        self._parse_types_index += 1
        start = self._parse_cnt
        end = self._parse_cnt + size
        if end > len(self._parse_msg):
            self.check_error(end - start, 'b')
        blob = O2blob(size, self._parse_msg[start : end])
        self._parse_cnt = (end + 4) & 3
        return blob

    def get_bool(self):
        return True if self._read_data(4, '>i', 'B')  else False

    def get_double(self):
        return self._read_data(8, '>d', 'd')  # Read 8 as big-endian double

    def get_float(self):
        return self._read_data(4, '>f', 'f')  # Read 4 bytes as big-endian float

    def get_int(self):
        return self.get_int32()

    def get_int32(self):
        return self._read_data(4, '>i', 'i')  # Read 4 bytes as big-endian int32

    def get_int64(self):
        return self._read_data(8, '>q', 'h')  # Read 8 bytes as big-endian int64

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
        if self._parse_types[self._parse_types_index] != 's':
            self._check_error(4, 's')  # force type error reporting
        self._parse_types_index += 1
        start = self._parse_cnt
        end = start
        while end < len(self._parse_msg) and self._parse_msg[end] != 0:
            end += 1
        if end >= len(self._parse_msg):  # force length error reporting
            self._check_error(end + 1 - start, 's') 
        try:
            extracted_string = self._parse_msg[start : end].decode('utf-8')
        except UnicodeDecodeError:
            raise ValueError("Error decoding string")

        # Align the parse count to 4 bytes
        self._parse_cnt = (end + 4) & ~3

        return extracted_string

    def get_time(self):
        return self._read_data(8, '>d', 't')  # Read 8 as big-endian double

