# py3fns -- Python3 implementations of some O2lite functions
#
# Roger B. Dannenberg
# Feb 2024

import time
import netifaces as ni

o2l_start_time = time.monotonic()
def o2l_sys_time():
    return time.monotonic() - o2l_start_time


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
                    internal_ip_hex = ''.join(
                            [f'{int(x):02x}' for x in ip_address.split('.')])
                    # Stop searching after finding the first valid IP
                    return internal_ip_hex  

    # If no non-localhost address is found, return localhost in hex
    return '7f000001' if internal_ip_hex == '00000000' else internal_ip_hex
