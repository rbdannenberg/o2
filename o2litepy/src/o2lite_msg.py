class O2lMsg:
    def __init__(self, data):
        self.data = data

    def addr_pointer(self):
        # Find the index of the first null-terminated string '\x00' (address)
        address_start = 12
        address_end = self.data.find(b'\x00', address_start)
        return self.data[address_start:address_end]

    def typespec_pointer(self):
        # Find the index of the next null-terminated string '\x00' (type spec)
        address_end = self.data.find(b'\x00', 12)
        typespec_start = address_end + 1
        typespec_end = self.data.find(b'\x00', typespec_start)
        return self.data[typespec_start + 1:typespec_end] #skip ,

    def data_pointer(self):
        # Find the index of the next null-terminated string '\x00' (type spec)
        typespec_end = self.data.find(b'\x00', 12)
        data_start = typespec_end + 1
        return self.data[data_start:]
