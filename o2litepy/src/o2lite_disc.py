# py3discovery -- discovery object implementation for Python3
#
# Zekai Shen and Roger B. Dannenberg
# March 2024

import socket
import time

from .ip_util import is_hex, hex_to_byte


def validate_and_extract_udp_port(name):
    if len(name) != 28 or name[0] != '@' or not is_hex(name[1:9]) or \
       name[9] != ':' or not is_hex(name[10:18]) or name[18] != ':':
        return None
    udp_port_hex = name[24:28]
    if not is_hex(udp_port_hex):
        return None
    udp_port = hex_to_byte(udp_port_hex)
    return udp_port


class O2lite_disc:
    def __init__(self, ensemble, debug_flags):
        self.services = {}
        self.discovered_services = []  # List to store discovered services
        self.browse_timeout = 2  # seconds
        self.debug_flags = debug_flags

    def get_host(self):
        """Callable from another (main) thread. Pops and returns a
            discovered service to the caller like {ip: ip, tcp_port: port}
        """
        if len(self.discovered_services) == 0:
            self.restart()
        if len(self.discovered_services) == 0:
            return None
        return self.pop_a_service()

        with self.dslock:
            service = self.discovered_services.pop(0)
            if "d" in self.debug_flags:
                print("O2lite_disc: get_host returns (and pops)", service)

        return service

    def restart(self):
        """restart discovery"""
        if "d" in self.debug_flags:
            print("O2lite_disc: (re)starting discovery")
        self.run_discovery()
        return
