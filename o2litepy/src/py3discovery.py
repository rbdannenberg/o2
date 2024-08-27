# py3discovery -- discovery object implementation for Python3
#
# Zekai Shen and Roger B. Dannenberg
# March 2024

from o2lite_disc import O2lite_disc, validate_and_extract_udp_port
from zeroconf import ServiceBrowser, Zeroconf, ServiceInfo
import socket
import time
from threading import Lock


class Py3discovery (O2lite_disc):
    def __init__(self, ensemble, debug_flags):
        super().__init__(ensemble, debug_flags)

        self.zeroconf = Zeroconf()
        self.dslock = Lock()

    def add_service(self, zeroconf, service_type, name):
        """Callback from Zeroconf"""
        # New service found, resolve it
        info = zeroconf.get_service_info(service_type, name)
        if info:
            self.handle_new_service(info)

    def remove_service(self, zeroconf, service_type, name):
        # Service removed
        print(f"Service removed: {name}")

    def handle_new_service(self, info):
        add = info.addresses
        server_ip = socket.inet_ntoa(info.addresses[0])
        print("handle_new_service", info)
        tcp_port = info.port

        txt_records = info.properties
        name_bytes = txt_records.get(b'name')
        if name_bytes:
            name = name_bytes.decode('utf-8')
            udp_port = validate_and_extract_udp_port(name)
            if udp_port:
                service_discovered = {"ip": server_ip, "tcp_port": tcp_port,
                                      "udp_port": udp_port}
                print("service_discovered", service_discovered)
                with self.dslock:
                    self.discovered_services.append(service_discovered)

    def pop_a_service(self):
        """Assume discovered_services is not empty, 
            remove and return first element
        """
        with self.dslock:
            service = self.discovered_services.pop(0)
            print("get_host returns (and pops)", service)
        return service

    def update_service(self, zeroconf, type, name):
        # Optional: Implement logic here if you need to handle service updates
        pass

    def close(self):
        self.zeroconf.close()

    def run_discovery(self):
        print("run_discovery start_browsing")
        browser = ServiceBrowser(self.zeroconf, "_o2proc._tcp.local.", self)
        time.sleep(self.browse_timeout)
        browser.cancel()
        print("run_discovery browser.cancel")
        return self.discovered_services
