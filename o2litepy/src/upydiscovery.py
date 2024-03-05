# upydiscovery -- MicroPython discovery class
#
# Roger B. Dannenberg
# Feb 2024

from .o2lite_disc import O2lite_disc, validate_and_extract_udp_port
import network
import uasyncio
from mdns_client import Client
from mdns_client.service_discovery.txt_discovery import TXTServiceDiscovery
import globals  # includes internal_ip_address


class Upydiscovery (O2lite_disc):
    def __init__(self, ensemble, debug_flags):
        super().__init__(ensemble, debug_flags)

        self.loop = uasyncio.get_event_loop()
        client = Client(globals.internal_ip_address)
        self.discovery = TXTServiceDiscovery(client)


    async def discover_once(self):
        responses = await self.discovery.query_once("_o2proc", "_tcp")
        for resp in responses:
            if 'name' in resp.txt_records:
                name = resp.txt_records['name']
                if isinstance(name, list):
                    name = name[0]
                udp_port = validate_and_extract_udp_port(name)
                if udp_port:
                    # response provides the IP as a Set, so we are going
                    # to hope that the Set either has one element or the
                    # first element we pop is the one we want. We could
                    # also get the IP from name, but then we have to
                    # decide whether we want the public IP or internal IP.
                    # A really careful implementation would pop resp.ips
                    # until finding an IP that matches either the public
                    # or internal IP in name, but we'll start with simple:
                    service_discovered = {"ip": resp.ips.pop(),
                                          "tcp_port": resp.port,
                                          "udp_port": udp_port}
                    if "d" in self.debug_flags:
                        print("Upydiscovery: service_discovered",
                              service_discovered)
                    self.discovered_services.append(service_discovered)


    def pop_a_service(self):
        """Assume discovered_services is not empty, 
            remove and return first element
        """
        return self.discovered_services.pop(0)

            
    def run_discovery(self):
        """Run discovery -- this implementation is a one-shot discovery
            that uses the default 2 sec timeout from mdns_client. Since
            the caller expects discovery to continue, we "fool" it by
            running again in get_host() when there are no more hosts
            available.
        """
        self.loop.run_until_complete(self.discover_once())
