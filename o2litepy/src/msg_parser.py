import struct


class MessageParser:
    def __init__(self, message):
        self.message = message
        self.parse_cnt = 0

    def _check_error(self, byte_count):
        if self.parse_cnt + byte_count > len(self.message):
            print(f"Parse error: trying to read beyond the message length at {self.parse_cnt}")
            raise ValueError("Parse error")

    def _read_data(self, byte_count, format_string):
        self._check_error(byte_count)
        value = struct.unpack(format_string, self.message[self.parse_cnt:self.parse_cnt + byte_count])
        self.parse_cnt += byte_count
        return value[0]

    def o2l_get_int32(self):
        return self._read_data(4, '>i')  # Read 4 bytes as big-endian int32

    def o2l_get_time(self):
        return self._read_data(8, '>d')  # Read 8 bytes as big-endian double

    def o2l_get_float(self):
        return self._read_data(4, '>f')  # Read 4 bytes as big-endian float

    def o2l_get_string(self):
        start = self.parse_cnt
        try:
            # Search for the null terminator
            while self.message[self.parse_cnt] != 0:
                self.parse_cnt += 1
                self._check_error(1)
            string_length = self.parse_cnt - start
            # Extract the string
            extracted_string = self.message[start:self.parse_cnt].decode('utf-8')
        except IndexError:
            raise ValueError("String parsing exceeded message length")
        except UnicodeDecodeError:
            raise ValueError("Error decoding string")

        # Align the parse count to 4 bytes
        self.parse_cnt = start + ((string_length + 4) & ~3)
        return extracted_string
