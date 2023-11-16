class MyListener:
    def remove_service(self, zeroconf, type, name):
        pass  # Handle the removal if needed

    def add_service(self, zeroconf, type, name):
        info = zeroconf.get_service_info(type, name)
        if info:
            self.handle_service(info)

    def handle_service(self, info):
        name = info.properties.get(b'name')

        version = info.properties.get(b'vers')
        tcp_port = info.port
        # Other properties can be accessed the same way

        # Here you can handle the properties, establish connections, etc.
        # This is similar to the handling done in `zc_resolve_callback` in the C code.
