# child class for o2lite project
from o2lite import O2liteObject


class O2liteEsp32(O2liteObject):
    def __init__(self):
        super().__init__()

    # diff between ifdef and if?
    def button_poll(self):
        pass

    def connect_to_wifi(self, hostname, ssid, pwd):
        pass

    def print_line(self):
        pass

