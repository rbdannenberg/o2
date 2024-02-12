import netifaces as ni


def find_my_ip_address():
    internal_ip_hex = '00000000'  # Default to an uninitialized state

    # Attempt to find a non-localhost, AF_INET address
    for interface in ni.interfaces():
        addresses = ni.ifaddresses(interface)
        if ni.AF_INET in addresses:
            for link in addresses[ni.AF_INET]:
                ip_address = link['addr']
                if ip_address != '127.0.0.1':
                    # Convert the IP address to its hex representation
                    internal_ip_hex = ''.join([f'{int(x):02x}' for x in ip_address.split('.')])
                    return internal_ip_hex  # Stop searching after finding the first valid IP

    # If no non-localhost address is found, return localhost in hex
    return '7f000001' if internal_ip_hex == '00000000' else internal_ip_hex


def hex_to_ip(hex_str):
    # Ensure hex_str is 8 characters
    assert len(hex_str) == 8, "Hex string must be 8 characters long."

    # Convert each 2-character hex segment to decimal
    return '.'.join(str(int(hex_str[i:i + 2], 16)) for i in range(0, 8, 2))


def is_hex(s):
    try:
        int(s, 16)
        return True
    except ValueError:
        return False


def hex_to_byte(hex_str):
    return int(hex_str, 16)
