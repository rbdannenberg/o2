README.txt - for o2litepy

o2litepy by Zekai Shen and Roger B. Dannenberg
Mar 2024

FOR PYTHON

See ../README.md.

FOR MICROPYTHON

For general documentation of the o2litepy module, see ../README.md.

My development process is described here:
    https://www.cs.cmu.edu/~rbd/blog/upyesp/upesp-blog4mar2024.html

To install on MicroPython, I suggest using the following directly in
MicroPython:
    import mip
    mip.install("github:rbdannenberg/o2/o2litepy")

You also need the package globals, but this is just an empty file,
so do this from your development computer to create module globals:
    ampy --port YOUR_SERIAL_PORT put micropython/globals.py lib/globals.py
In other words, you just need globals.py on your MicroPython library
path (/lib/). (If you do not want to clone the repo to get sources,
just make any empty file and "ampy put" it into lib/globals.py).

Finally, before using o2litepy.o2lite, you need to initialize your
MicroPython Wi-Fi connection. The only requirement is to connect and
store your internal IP address into globals.internal_ip_address.

Here is some sample setup code (see micropython/wifiprefix.py):
--------------------------------------------------------
WIFI_NAME = "WIFI_NETWORK_ID"
WIFI_PASSWORD = "WIFI_PASSWORD"

import globals
import time
import network

sta_if = network.WLAN(network.STA_IF)
sta_if.active(True)
sta_if.connect(WIFI_NAME, WIFI_PASSWORD)
while not sta_if.isconnected():
   time.sleep(1.0)

globals.internal_ip_address = sta_if.ifconfig()[0]
print("IP Address", globals.internal_ip_address)
--------------------------------------------------------
Of course, you must use actual values in place of
WIFI_NETWORK_ID and WIFI_PASSWORD.

Alternatively, see
    https://www.cs.cmu.edu/~rbd/blog/upyesp/upesp-blog4mar2024.html
which shows how to avoid storing your password in every program you
develop.

Now, you can import and use o2lite:

from o2litepy import o2lite
o2lite.initialize("test")  # pass in your O2 ensemble name
...

See ../README.md for O2lite documentation.
