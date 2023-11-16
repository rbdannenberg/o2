from zeroconf import ServiceBrowser, Zeroconf
import socket
import time

class O2LiteBonjourDiscovery:
    def __init__(self, ensemble_name):
        self.zeroconf = Zeroconf()
        self.listener = MyListener()
        self.browser = ServiceBrowser(self.zeroconf, "_o2proc._tcp.local.", self.listener)
        self.ensemble_name = ensemble_name
        self.last_activity = time.time()
        self.BROWSE_TIMEOUT = 20  # this was defined as a macro in C code

    def service_discovered(self):
        # Called whenever a service is discovered
        self.last_activity = time.time()

    def poll(self):
        current_time = time.time()

        # Check if there's no activity for the BROWSE_TIMEOUT duration
        if current_time - self.last_activity > self.BROWSE_TIMEOUT:
            print("No activity, restarting ServiceBrowse")
            self.restart_discovery()
            self.last_activity = current_time

        # Other polling logic can be added here

    def restart_discovery(self):
        # Close existing discovery and restart it
        self.zeroconf.close()
        self.zeroconf = Zeroconf()
        self.browser = ServiceBrowser(self.zeroconf, "_o2proc._tcp.local.", self.listener)

    def close(self):
        self.zeroconf.close()

# Example usage
if __name__ == "__main__":
    discovery = O2LiteBonjourDiscovery("ensemble_name_here")
    try:
        while True:
            discovery.poll()
            time.sleep(1)  # You can adjust the sleep time accordingly
    except KeyboardInterrupt:
        pass
    finally:
        discovery.close()
