import struct
import sys
from socket import socket


def hex_to_nibble(hex_char):
    if hex_char.isdigit():
        return int(hex_char)
    elif 'A' <= hex_char <= 'F':
        return ord(hex_char) - ord('A') + 10
    elif 'a' <= hex_char <= 'f':
        return ord(hex_char) - ord('a') + 10
    else:
        print("ERROR: bad hex character passed to hex_to_nibble()")
        return 0


def hex_to_byte(hex_str):
    return (hex_to_nibble(hex_str[0]) << 4) + hex_to_nibble(hex_str[1])


def o2_hex_to_dot(hex_string):
    def hex_to_byte(hex_substr):
        return int(hex_substr, 16)

    i1 = hex_to_byte(hex_string[:2])
    i2 = hex_to_byte(hex_string[2:4])
    i3 = hex_to_byte(hex_string[4:6])
    i4 = hex_to_byte(hex_string[6:8])

    return f"{i1}.{i2}.{i3}.{i4}"


def o2lswap16(i):
    return ((i >> 8) & 0xff) | ((i & 0xff) << 8)


def o2lswap32(i):
    return ((i >> 24) & 0xff) | ((i & 0xff0000) >> 8) | ((i & 0xff00) << 8) | ((i & 0xff) << 24)


def o2lswap64(i):
    # Convert the 64-bit integer to bytes in little-endian format
    bytes_le = struct.pack('<Q', i)  # 'Q' denotes an unsigned 64-bit integer in little-endian format

    # Unpack the bytes as a big-endian 64-bit integer
    swapped = struct.unpack('>Q', bytes_le)[0]  # '>Q' denotes an unsigned 64-bit integer in big-endian format

    return swapped


class O2Lite:
    # Constants
    O2L_MAX_ARGS = 16
    O2L_ENSEMBLE_LEN = 64
    O2L_SERVICES_LEN = 100
    O2L_IP_LEN = 16

    def __init__(self):
        self.parse_msg = None
        self.parse_cnt = None
        self.parse_error = None

        self.max_parse_cnt = 100

        self.remote_ip_port = ""  # Placeholder for the IP and port of the bridged O2 process
        self.bridge_id = -1  # Identifier for this bridged process

        O2_NO_ZEROCONF = False  # or True, depending on the configuration
        O2_NO_O2DISCOVERY = False  # or True, depending on the configuration

        self.o2n_internal_ip = ""
        self.o2l_services = None
        self.o2l_ensemble = None
        self.verbose = False  # Verbose for debugging
        self.platform = sys.platform

        if not (O2_NO_ZEROCONF or O2_NO_O2DISCOVERY):
            raise ValueError("O2lite supports either ZeroConf or built-in discovery, but not both. "
                             "One of O2_NO_ZEROCONF or O2_NO_O2DISCOVERY must be set to True.")

        if self.platform == "win32":
            self.start_time = 0
        elif self.platform in ["darwin", "linux", "esp32"]:
            self.start_time = 0
        else:
            raise EnvironmentError("Unsupported environment. Expecting win32, darwin, linux, or esp32.")

    def o2l_dispatch(self, msg):
        pass

    def find_my_ip_address(self):
        hostname = socket.gethostname()
        IPAddr = socket.gethostbyname(hostname)
        print("Your Computer IP Address is:" + IPAddr)

    def o2l_get_timestamp(self):
        t = int(self.parse_msg['timestamp'])
        t = o2lswap64(t)
        return float(t)

    def o2l_get_error(self):
        return self.parse_error

    def check_error(self, typ):
        if parse_cnt + struct.calcsize(typ) > max_parse_cnt:
            print(f"o2lite: parse error reading message to {parse_msg.address}")
            parse_error = True
            return 0

    def cur_data_addr(self, typ):
        offset = parse_cnt
        return struct.unpack(typ, self.parse_msg[offset:offset + struct.calcsize(typ)])

    def cur_data(self, var_name, typ):
        self.check_error(typ)
        var = self.cur_data_addr(typ)[0]
        globals()[var_name] = var
        parse_cnt += struct.calcsize(typ)

    # Functions to get data types from incoming message
    def o2l_get_time(self):
        # Assuming `parse_msg` is a bytes-like object available in the global scope
        t_format = 'q'  # 'q' is for int64_t in little-endian format
        t_size = struct.calcsize(t_format)

        # Equivalent of CURDATA(t, int64_t);
        t_bytes = self.parse_msg[parse_cnt:parse_cnt + t_size]
        t = struct.unpack(t_format, t_bytes)[0]
        parse_cnt += t_size

        # Assuming o2lswap64 swaps byte order (endianness)
        t_swapped = struct.unpack('>q', struct.pack('<q', t))[0]  # Swap from little-endian to big-endian

        # Convert the swapped int64_t back to bytes and interpret as double
        return struct.unpack('d', struct.pack('>q', t_swapped))[0]

    def get_float(self):
        # Assuming `parse_msg` is a bytes-like object available in the global scope
        x_format = 'i'  # 'i' is for int32_t in little-endian format
        x_size = struct.calcsize(x_format)

        # Equivalent of CURDATA(x, int32_t);
        x_bytes = self.parse_msg[parse_cnt:parse_cnt + x_size]
        x = struct.unpack(x_format, x_bytes)[0]
        parse_cnt += x_size

        # Assuming o2lswap32 swaps byte order (endianness)
        x_swapped = struct.unpack('>i', struct.pack('<i', x))[0]  # Swap from little-endian to big-endian

        # Convert the swapped int32_t back to bytes and interpret as float
        return struct.unpack('f', struct.pack('>i', x_swapped))[0]

    def get_int32(self):
        # Assuming `parse_msg` is a bytes-like object available in the global scope
        i_format = 'i'  # 'i' is for int32_t in little-endian format
        i_size = struct.calcsize(i_format)

        # Equivalent of CURDATA(i, int32_t);
        i_bytes = self.parse_msg[parse_cnt:parse_cnt + i_size]
        i = struct.unpack(i_format, i_bytes)[0]
        parse_cnt += i_size

        # Assuming o2lswap32 swaps byte order (endianness)
        return struct.unpack('>i', struct.pack('<i', i))[0]

    def get_string(self):
        # Assuming `parse_msg` is a bytes-like object available in the global scope
        start_idx = self.parse_cnt

        # Find the end of the string (null terminator in C)
        end_idx = self.parse_msg.find(b'\0', start_idx)

        if end_idx == -1:
            # Handle error: end of string not found
            # This is the equivalent of CHECKERROR(char *)
            print(
                "o2lite: parse error reading string from message")
            return None

        s = self.parse_msg[start_idx:end_idx].decode('utf-8')
        len_s = len(s)
        self.parse_cnt += (len_s + 4) & ~3
        return s

    def o2l_add_string(self, s):
        for char in s:
            # still need to write char and EOS, so need space for 2 chars:
            if out_msg_cnt + 2 > MAX_MSG_LEN:
                self.parse_error = True
                return
            outbuf[out_msg_cnt] = char
            out_msg_cnt += 1

        # write EOS (end-of-string), which is represented as a null character in C
        outbuf.append('\0')

        # fill to word boundary (assuming 4-byte boundary since `out_msg_cnt & 0x3` checks the last two bits)
        while out_msg_cnt & 0x3:
            outbuf.append('\0')
            out_msg_cnt += 1

    def o2l_add_float(self, x):
        # Convert the float value to its binary representation
        binary_float = struct.pack('f', x)

        # Convert binary representation of float to int32 and swap
        int32_value = struct.unpack('i', binary_float)[0]
        swapped_value = o2lswap32(int32_value)

        # Check for buffer overflow
        if out_msg_cnt + 4 > MAX_MSG_LEN:  # sizeof(float) is typically 4 bytes
            parse_error = True
            return

        # Append swapped_value to the buffer
        outbuf[out_msg_cnt:out_msg_cnt + 4] = struct.pack('i', swapped_value)
        out_msg_cnt += 4

    def o2l_add_int32(self, i):
        # Swap the byte order
        swapped_value = o2lswap32(i)

        # Check for buffer overflow
        if out_msg_cnt + 4 > MAX_MSG_LEN:  # sizeof(int32_t) is typically 4 bytes
            parse_error = True
            return

        # Append swapped_value to the buffer
        outbuf[out_msg_cnt:out_msg_cnt + 4] = struct.pack('i', swapped_value)
        out_msg_cnt += 4

    def o2l_add_time(self, time):
        # Check for buffer overflow
        if out_msg_cnt + 8 > MAX_MSG_LEN:  # sizeof(double) is typically 8 bytes
            parse_error = True
            return

        # Convert the double to its int64 representation and swap its byte order
        t = struct.unpack('Q', struct.pack('d', time))[0]
        swapped_value = o2lswap64(t)

        # Append swapped_value to the buffer
        outbuf[out_msg_cnt:out_msg_cnt + 8] = struct.pack('Q', swapped_value)
        out_msg_cnt += 8

    def o2l_send_start(self, address, time, types, tcp):
        # global parse_error, out_msg_cnt, out_msg, outbuf

        parse_error = False
        out_msg_cnt = len(out_msg['length'])

        self.o2l_add_int32(O2_TCP_FLAG if tcp else O2_UDP_FLAG)
        self.o2l_add_time(time)
        self.o2l_add_string(address)
        outbuf[out_msg_cnt] = ','  # type strings have a leading ','
        out_msg_cnt += 1
        self.o2l_add_string(types)

    def o2l_send(self):
        if self.parse_error or tcp_sock == INVALID_SOCKET:
            return

        out_msg.length = o2lswap32(out_msg_cnt - len(out_msg.length))

        if out_msg.misc & o2lswap32(O2_TCP_FLAG):
            tcp_sock.send(outbuf[:out_msg_cnt])
        else:
            message_to_send = outbuf[len(out_msg.length):out_msg_cnt]
            bytes_sent = udp_send_sock.sendto(message_to_send, udp_server_sa)
            if bytes_sent < 0:
                print("Error attempting to send udp message")
                print(f"Address: {out_msg.address}, socket: {udp_send_sock}")

    def o2l_send_services(self):
        global o2l_services, o2l_bridge_id
        s = o2l_services
        if o2l_bridge_id < 0:
            return
        while s:
            name = ""
            while s and s[0] != ',':
                if len(name) > 31:  # name too long
                    print("service name too long")
                    return
                name += s[0]
                s = s[1:]
            if not name:
                continue  # skip comma
            self.o2l_send_start("!_o2/o2lite/sv", 0, "siisi", True)
            self.o2l_add_string(name)
            self.o2l_add_int32(1)  # exists
            self.o2l_add_int32(1)  # this is a service
            self.o2l_add_string("")  # no properties
            self.o2l_add_int32(0)  # send_mode is ignored for services
            self.o2l_send()
            if s and s[0] == ',':
                s = s[1:]

    # Service Announcements
    def o2l_set_services(self, services):
        o2l_services = services
        self.o2l_send_services()

    # Endianness utility functions
    @staticmethod
    def is_big_endian():
        return sys.byteorder == "big"

    @staticmethod
    def is_little_endian():
        return not O2Lite.is_big_endian()

    @staticmethod
    def o2lswap16(i):
        # Logic for byte swapping for 16-bit integer
        return ((i >> 8) & 0xff) | ((i & 0xff) << 8) if O2Lite.is_little_endian() else i

    @staticmethod
    def o2lswap32(i):
        # Logic for byte swapping for 32-bit integer
        if O2Lite.is_little_endian():
            return ((i >> 24) & 0xff) | ((i & 0xff0000) >> 8) | \
                ((i & 0xff00) << 8) | ((i & 0xff) << 24)
        else:
            return i

    @staticmethod
    def o2lswap64(i):
        # Logic for byte swapping for 64-bit integer
        if O2Lite.is_little_endian():
            return ((O2Lite.o2lswap32(i) << 32) | O2Lite.o2lswap32(i >> 32))
        else:
            return i

    # Discovery API functions (placeholders)
    @staticmethod
    def o2ldisc_init(ensemble):
        pass

    @staticmethod
    def o2ldisc_poll():
        pass

    @staticmethod
    def o2ldisc_events(readset):
        pass

    # Other utility functions and validations
    @staticmethod
    def is_valid_proc_name(name, port, internal_ip, udp_port):
        pass

    @staticmethod
    def parse_version(vers, vers_len):
        pass

    @staticmethod
    def address_init(ip, port_num, tcp=True):
        pass

    @staticmethod
    def network_connect(ip, port):
        pass

    @staticmethod
    def add_socket(s):
        pass

    @staticmethod
    def bind_recv_socket(sock, port):
        pass
