import socket
from zeroconf import ServiceBrowser, Zeroconf, ServiceStateChange


class O2LiteDiscovery:
    INVALID_SOCKET = -1

    def __init__(self, ensemble):
        self.ensemble = ensemble
        self.zc_name = ensemble
        self.zeroconf = Zeroconf()
        self.browser = None
        self.tcp_sock = None
        self.browse_timeout = 20

    def zc_shutdown(self):
        if self.browser:
            self.browser.cancel()
            self.browser = None
        if self.zeroconf:
            self.zeroconf.close()
            self.zeroconf = None
        self.zc_name = None

    def on_service_state_change(self, zeroconf, service_type, name, state_change):
        if state_change == ServiceStateChange.Added:
            print(f"(Browser) NEW: {name} of type {service_type}")
            info = zeroconf.get_service_info(service_type, name)
            if info:
                address = socket.inet_ntoa(info.address)
                port = info.port
                properties = info.properties
                self.process_service(address, port, properties)
        elif state_change == ServiceStateChange.Removed:
            print(f"(Browser) REMOVE: {name} of type {service_type}")

    def process_service(self, address, port, properties):
        name = properties.get(b'name', b'').decode('utf-8')
        version = properties.get(b'vers', b'0').decode('utf-8')
        if name and version:
            # Assuming o2l_is_valid_proc_name, o2l_address_init, and o2l_network_connect
            # are provided somewhere in your code or you need to implement them.
            internal_ip, udp_port = self.o2l_is_valid_proc_name(name, port)
            if internal_ip and udp_port:
                self.o2l_address_init(internal_ip, udp_port)
                print(f"Found a host: {name}")
                self.o2l_network_connect(internal_ip, port)

    def o2l_is_valid_proc_name(self, name, port):
        # This is a placeholder function. Fill in the logic based on the C code.
        return '127.0.0.1', 8000

    def o2l_address_init(self, ip, port):
        """
        Initialize the UDP address for communication.

        :param ip: IP address as a string.
        :param port: Port number.
        :param use_ipv6: A boolean indicating if IPv6 should be used.
        """
        self.udp_address = (ip, port)
        self.udp_socket = socket.socket(socket.AF_INET6 if use_ipv6 else socket.AF_INET, socket.SOCK_DGRAM)
        try:
            self.udp_socket.bind(self.udp_address)
        except socket.error as e:
            print(f"Failed to bind to UDP address {ip}:{port}. Error: {e}")
            self.udp_socket = None

    def o2l_network_connect(self, ip, port):
        """
        Connect to a given IP and port using TCP.

        :param ip: IP address as a string.
        :param port: Port number.
        """
        self.tcp_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            self.tcp_socket.connect((ip, port))
            print(f"Successfully connected to {ip}:{port}")
        except socket.error as e:
            print(f"Failed to connect to {ip}:{port}. Error: {e}")
            self.tcp_socket.close()
            self.tcp_socket = None

    def o2ldisc_init(self):
        if self.browser:
            print("Already running")
            return
        self.browser = ServiceBrowser(self.zeroconf, "_o2proc._tcp.local.", handlers=[self.on_service_state_change])

    def o2ldisc_events(self):
        # Fill in this function based on the C code if required.
        pass

    def o2ldisc_poll(self):
        # Placeholder for o2l_local_now, assuming it's current time in seconds
        o2l_local_now = time.time()

        # start resolving if timeout
        if self.tcp_sock == self.INVALID_SOCKET:
            if o2l_local_now > self.browse_timeout:
                print("No activity, restarting Avahi client")
                self.zc_shutdown()
                self.browse_timeout = o2l_local_now + self.BROWSE_TIMEOUT
                self.o2ldisc_init(self.ensemble)

        # Process Avahi (Zeroconf in this case) events
        # The Zeroconf library in Python is event-driven. So, there's no direct
        # equivalent of "polling" like in the C code.

        if self.zc_shutdown_request:
            self.zc_shutdown_request = False
            self.zc_shutdown()


# Example usage
discovery = O2LiteDiscovery("my_ensemble")
discovery.o2ldisc_init()
