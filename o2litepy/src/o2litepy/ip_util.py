def is_hex(s):
    try:
        int(s, 16)
        return True
    except ValueError:
        return False


def hex_to_byte(hex_str):
    return int(hex_str, 16)
