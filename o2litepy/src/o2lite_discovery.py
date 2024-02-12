import threading

from zeroconf import ServiceBrowser, Zeroconf, ServiceInfo
import socket
import time

from util.ip_util import is_hex, hex_to_byte


def validate_and_extract_udp_port(name, tcp_port):
    if len(name) != 28 or name[0] != '@' or not is_hex(name[1:9]) or name[9] != ':' or not is_hex(name[10:18]) or name[
        18] != ':':
        return None
    udp_port_hex = name[24:28]
    if not is_hex(udp_port_hex):
        return None
    udp_port = hex_to_byte(udp_port_hex)
    return udp_port


class O2LiteDiscovery:
    def __init__(self, ensemble, o2l):
        self.ensemble = ensemble
        self.o2l = o2l

        self.discovery_stop_flag = None
        self.discovery_thread = None
        self.last_activity_time = None
        self.browser = None
        self.zeroconf = Zeroconf()
        self.services = {}
        self.discovered_services = []  # List to store discovered services
        self.browse_timeout = 2  # seconds

    def start_browsing(self):
        self.browser = ServiceBrowser(self.zeroconf, "_o2proc._tcp.local.", self)
        self.last_activity_time = time.time()

    def add_service(self, zeroconf, service_type, name):
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
        tcp_port = info.port

        txt_records = info.properties
        name_bytes = txt_records.get(b'name')
        if name_bytes:
            name = name_bytes.decode('utf-8')
            if validate_and_extract_udp_port(name, tcp_port):
                udp_port = validate_and_extract_udp_port(name, tcp_port)
                service_discovered = {"ip": server_ip, "tcp_port": tcp_port, "udp_port": udp_port}
                # self.discovered_services.append(service_discovered)
                self.o2l.update_most_recent_host(service_discovered)

    def update_service(self, zeroconf, type, name):
        # Optional: Implement logic here if you need to handle service updates
        pass

    def check_timeout(self):
        if time.time() - self.last_activity_time > self.browse_timeout:
            self.restart_browsing()

    def restart_browsing(self):
        # Restart browsing services
        self.browser.cancel()
        self.start_browsing()

    def close(self):
        self.zeroconf.close()

    def disc_poll(self):
        pass

    def run_discovery(self):
        self.start_browsing()
        time.sleep(self.browse_timeout)
        self.browser.cancel()
        return self.discovered_services

    def run_discovery_continuous(self):
        def discovery_task():
            while not self.discovery_stop_flag:
                self.start_browsing()
                time.sleep(self.browse_timeout)

        self.discovery_thread = threading.Thread(target=discovery_task)
        self.discovery_thread.start()

    def stop_discovery(self):
        self.discovery_stop_flag = True
        self.discovery_thread.join()
        self.close()

