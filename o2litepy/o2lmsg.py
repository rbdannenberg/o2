class O2lMsg:
    def __init__(self, length=0, misc=0, timestamp=0.0, address=''):
        self.length = length       # int32_t equivalent in Python is int
        self.misc = misc           # int32_t equivalent in Python is int
        self.timestamp = timestamp # double equivalent in Python is float
        self.address = address     # char[4] can be represented as a string

    def __repr__(self):
        return f'O2lMsg(length={self.length}, misc={self.misc}, timestamp={self.timestamp}, address={self.address})'
