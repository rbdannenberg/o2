class O2LiteHandler:
    def __init__(self, address, typespec, full, handler, info):
        self.address = address
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
