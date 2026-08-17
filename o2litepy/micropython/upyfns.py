# upyfns -- MicroPython implementations of some O2lite functions
#
# Roger B. Dannenberg
# Feb 2024

import time
import network
import globals

o2l_start_time = time.ticks_ms()

def o2l_sys_time():
    return time.ticks_diff(time.ticks_ms(), o2l_start_time) * 0.001


def find_my_ip_address():
    """Get the IP address in the form ddd.ddd.ddd.ddd in globals
        and convert to hexadecimal
    """
    return ''.join([f"{int(x):02x}"
                    for x in globals.internal_ip_address.split('.')])
