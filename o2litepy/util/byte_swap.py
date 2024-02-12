import struct


def o2lswap16(i):
    # Perform byte swap
    return ((i >> 8) & 0xff) | ((i & 0xff) << 8)


def o2lswap32(i):
    # Swap the endianness of a 32-bit integer
    return struct.unpack("<I", struct.pack(">I", i))[0]