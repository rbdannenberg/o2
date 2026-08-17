class O2lite_handler:
    def __init__(self, address, typespec, full, handler, info):
        # To simplify search, we strip off the initial path character,
        # which could be "/" or "!"
        self.address = address[1:]
        self.typespec = typespec.encode('utf-8') if typespec else None
        self.full = full
        self.handler = handler
        self.info = info

    def matches(self, address, types):
        if self.full and self.address != address:
            return False
        if self.typespec:
            return self.typespec == types
        return True
