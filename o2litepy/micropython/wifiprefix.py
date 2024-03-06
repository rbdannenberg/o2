# wifiprefix.py - template for your ~/bin/secrets/wifiprefix.py file.
#
# Allows you to keep your Wi-Fi password out of your MicroPython projects.
#
# See https://www.cs.cmu.edu/~rbd/blog/upyesp/upesp-blog4mar2024.html
# for a detailed description.
#
# Quick setup/checklist:
# - If ~/bin does not exist, run "mkdir ~/bin" and put the full path
#     to your ~/bin directory on your PATH. (See instructions on the web
#     for your particular OS and shell.
# - mkdir ~/bin/secrets
# - chmod og-r ~/bin/secrets
# - copy this file to ~/bin/secrets/wifiprefix.py
# - chmod og-r ~/bin/secrets/wifiprefix.py
# - edit ~/bin/secrets/wifiprefix.py to put in your real Wi-Fi network
#     ID and password; also remove all these comments -- you do not want
#     do upload them to your microcontroller every time you run a program.
# - install micropython on your microcontroller if not already installed.
# - install ampy, the Adafruit MicroPython Tool
# - upload the file ../src/globals.py to /lib/globals.py on your
#     microcontroller with ampy
# - cp uprun ~/bin/uprun
# - chmod a+x ~/bin/uprun
# - edit ~/bin/uprun:
#     define BINPATH with the actual path to your ~/bin directory
#     define SERIAL with your actual serial port
#
# Now, uprun it will run "make upload" which you can define to upload
# changed files (see for example ../Makefile), then it will prepend
# wifiprefix.py to main.py (or the file you specify as argument) and
# run it on your microcontroller with the ampy run command.

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
