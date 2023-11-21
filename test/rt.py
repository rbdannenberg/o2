# regression_tests.py -- a rewrite of regression_tests.sh for Windows
#    using Python 3
#
# Roger Dannenberg   revised Oct 2022 to avoid pipes,
#                        which seem not to work on Linux
# Roger Dannenberg   Jan 2017, 2020
#
# Run this in the o2/tests directory where it is found
#

# get print to be compatible even if using python 2.x:
from __future__ import print_function

import sys
import os
import platform
import subprocess
import shlex
import threading
from threading import Timer
from checkports import checkports
import time

print('Optional flag after "-a" to print all output')
print("Optional argument is (relative) path to tests, e.g. ../Release")

print_all_output = False

IS_OSX = False
TIMEOUT_SEC = 60  # appfollow/applead take about 40s (is 60s long enough?)
LOCALDOMAIN = "local"  # but on linux, it's "localhost"

# I saw a failure of oscbndlsend+oscbndlrecv because port 8100 could
# not be bound, but I could then run by hand, so I am guessing that
# maybe it was used in a previous test and linux would not reuse it
# so quickly. So now, we wait betwen tests to see if it helps.
# 5s was not enough, but 30s seems to work. Trying 20s now.
STALL_SEC = 120  # time after run to make sure ports are free and
    # see if this avoids race condition where Avahi already has "test"
    # registered from a previously running instance of O2

if platform.system() == "Darwin":
    STALL_SEC = 1  # I don't think we need to stall for macOS
    IS_OSX = True
    # This no longer applies:
    # input("macOS tip: turn Firewall OFF to avoid orphaned ports ")
    HTMLOPEN = "open "

allOK = True

# Is this Windows?
EXE = ""
if os.name == 'nt':
    EXE = ".exe"
    HTMLOPEN = "start firefox "

# Find the binaries
if os.path.isdir(os.path.join(os.getcwd(), '../Debug')):
   BIN="../Debug"
else:
   BIN=".."
# In linux, there is likely to be a debug version of the
# library copied to ../Debug, but tests are built in ..
if platform.system() == 'Linux':
    BIN=".."
    HTMLOPEN = "xdg-open "
    LOCALDOMAIN = "localhost"

for arg in sys.argv[1:]:
    if arg[0] == '-':
        print_all_output = ('a' in arg)
        print("Printing all output")
    else:
        BIN = arg
    print("Directory for test binaries:", BIN)

def findLineInString(line, aString):
    return ('\n' + line + '\n') in aString


def kill_process(process, command):
    print("\nTimeout: killing", command)
    process.kill()


class runInBackground(threading.Thread):
    def __init__(self, command, id):
        self.command = command
        self.output = ""
        self.errout = ""
        self.id = id
        threading.Thread.__init__(self)

    def run(self):
        args = shlex.split(self.command)
        args[0] = BIN + '/' + args[0] + EXE
        output = " 1> stdout" + self.id + ".txt 2> stderr" + self.id + ".txt"
        process = subprocess.Popen(" ".join(args) + output, shell=True)
        timer = Timer(TIMEOUT_SEC, kill_process,
                      args=[process, self.command])
        try:
            timer.start()
            process.wait()
        finally:
            timer.cancel()
        

# runTest testname - runs testname, saving output in output.txt,
#    searches output.txt for single full line containing "DONE",
#    returns status=0 if DONE was found (indicating success), or
#    else status=-1 (indicating failure).
# stall parameter is ignored now
def runTest(command, stall=False, quit_on_port_loss=False):
    global allOK
    # time.sleep(1)  # see runDouble for extensive comment on this
    print(command.rjust(30) + ": ", end='', flush=True)
    args = shlex.split(command)
    args[0] = BIN + '/' + args[0] + EXE
    output = " 1> stdout.txt 2> stderr.txt"
    process = subprocess.Popen(" ".join(args) + output, shell=True)
    timer = Timer(TIMEOUT_SEC, process.kill)
    try:
        timer.start()
        process.wait()
    finally:
        timer.cancel()

    # Ports were left open in some earlier versions of O2, so we checked
    # them carefully, but now with Bonjour/Avahi, this is not a problem
    # and we are not using any particular et of ports, so I have removed
    # the checks (except at the very end, just in case):
    # portsOK, countmsg = checkports(False, False)
    countmsg = ""
    portsOK = True

    # is there a race condition with the file system?  Could use dostall,
    time.sleep(1)  # but delay is short and output is distracting

    with open("stdout.txt", "r") as outf:
        stdout = outf.read()
    with open("stderr.txt", "r") as errf:
        stderr = errf.read()
    
    if findLineInString("DONE", stdout):
        print("PASS", countmsg)
        # to return success (True), process must not have orphaned a port,
        # but I can't figure out why macOS orphans ports, and I can't get
        # through a complete test run without losing at least one port,
        # so we print errors, but we do not stop the testing
        if ((not IS_OSX) or quit_on_port_loss) and (not portsOK):
            allOK = False # halt the testing
    else:
        allOK = False
    if (not portsOK) or (not allOK):
        if not portsOK:
            print("FAIL a port was not freed, now we have fewer", countmsg)
        print("**** Failing output:")
        print(stdout)
        print("**** Failing error output:")
        print(stderr)
    elif print_all_output:
        print("**** stdout")
        print(stdout)

    os.remove("stdout.txt")
    os.remove("stderr.txt")
    dostall(STALL_SEC)
    return allOK


def dostall(dur):
    msg = "stalling (" + str(dur) + "s) ..."
    print(msg, end="", flush=True)
    time.sleep(dur)
    # back up to beginning of message:
    for i in range(len(msg)):
        print("\b", end="")
    # erase message:
    for i in range(len(msg)):
        print(" ", end="")
    # back up to beginning again
    for i in range(len(msg)):
        print("\b", end="")
    print("", end="", flush=True)


def startDouble(prog1, prog2, url=""):
    name2 = prog2 if url == "" else url
    print((prog1 + '+' + name2).rjust(30) + ": ", end='', flush=True)
    p1 = runInBackground(prog1, "1")
    p1.start()
    p2 = runInBackground(prog2, "2")
    p2.start()
    return (p1, p2)


def finishDouble(prog1, p1, out1, prog2, p2, out2, stall):
    global allOK
    p1.join()
    p2.join()
    # could use dostall() here, but the delay is short and output is distracting
    time.sleep(1)  # debugging test: is there a race to get stdout?

    # Ports were left open in some earlier versions of O2, so we checked
    # them carefully, but now with Bonjour/Avahi, this is not a problem
    # and we are not using any particular set of ports, so I have removed
    # the checks (except at the very end, just in case):
    # portsOK, countmsg = checkports(False, False)
    countmsg = ""
    portsOK = True

    with open("stdout1.txt", "r") as outf:
        p1output = outf.read()
    with open("stdout2.txt", "r") as outf:
        p2output = outf.read()

    if findLineInString(out1, p1output):
        if findLineInString(out2, p2output):
            print("PASS", countmsg)
            if (not IS_OSX) and (not portsOK):
                allOK = False # halt the testing
        else:
            allOK = False
    else:
        allOK = False

    if (not portsOK) or (not allOK):
        print("FAIL")
        print("**** Failing output from " + prog1)
        print(p1output)
        print("**** Failing error output from " + prog1)
        with open("stderr2.txt", "r") as errf:
            print(errf.read())

        print("**** Failing output from " + prog2)
        print(p2output)
        print("**** Failing error output from " + prog2)
        with open("stderr2.txt", "r") as errf:
            print(errf.read())

    elif print_all_output:
        print("**** p1.output")        
        print(p1output)
        print("**** p2.output")
        print(p2output)

    os.remove("stdout1.txt")
    os.remove("stderr1.txt")
    os.remove("stdout2.txt")
    os.remove("stderr2.txt")
    dostall(STALL_SEC)
    return allOK


# note: stall parameter is ignored now
#
def runDouble(prog1, out1, prog2, out2, stall=False):
    global allOK
    # time.sleep(1)  # for some reason, I'm getting programs that fail 
    # immediately and nothing shows up in stdout or stderr, which is
    # not reproducible running from the command line. Is there are 
    # bug or race condition in Python's subprocess (for macOS)? This 
    # sleep puts a pause between the shutting down of one pair of 
    # processes and the starting of the next. This is NOT the right
    # way to solve problems, but since this is very time dependent,
    # I'm not even sure where to start to track it down.
    p1p2 = startDouble(prog1, prog2)
    return finishDouble(prog1, p1p2[0], out1, prog2, p1p2[1], out2, stall)


def runWsTest(prog1, out1, url, out2, stall=False):
    global allOK
    prog2 = "websockhost @"
    p1p2 = startDouble(prog1, prog2, url)
    os.system(HTMLOPEN + '"http://test.' + LOCALDOMAIN + ':8080/' + url + '"');
    return finishDouble(prog1, p1p2[0], out1, prog2, p1p2[1], 
                        out2, stall)


def runAllTests():
    extensions = input("Run pattern match and bundle tests? [y,n]: ")
    extensions = "y" in extensions.lower()
    websocketsTests = input("Run websocket tests? [y,n]: ")
    websocketsTests = "y" in websocketsTests.lower()
    have_hub_tests = os.path.exists(BIN + "/hubclient/" + EXE) and \
                     os.path.exists(BIN + "/hubserver/" + EXE)
    print("Running regression tests for O2 ...")

    if not runTest("memtest"): return
    if not runTest("stuniptest", quit_on_port_loss=True): return
    if not runTest("dispatchtest"): return
    if not runTest("typestest"): return
    if not runTest("taptest"): return
    if not runTest("coercetest"): return
    if not runTest("longtest"): return
    if not runTest("arraytest"): return
    if not runTest("bridgeapi"): return
    if not runTest("o2litemsg"): return

    if extensions:
        if not runTest("bundletest"): return
        if not runTest("patterntest"): return
        if not runDouble("oscbndlsend u", "OSCSEND DONE",
                         "oscbndlrecv u", "OSCRECV DONE", True): return
        if not runDouble("oscbndlsend", "OSCSEND DONE",
                         "oscbndlrecv", "OSCRECV DONE", True): return

    if websocketsTests:
        if not runWsTest("proprecv", "DONE", 
                         "propsend.htm", "WEBSOCKETHOST DONE"): return
        if not runWsTest("o2server - 20t", "SERVER DONE", 
                         "o2client.htm", "WEBSOCKETHOST DONE"): return
        if not runWsTest("tappub", "SERVER DONE", 
                         "tapsub.htm", "WEBSOCKETHOST DONE"): return
        if not runWsTest("statusclient", "CLIENT DONE", 
                         "statusserver.htm", "WEBSOCKETHOST DONE"): return
        if not runWsTest("statusserver", "SERVER DONE", 
                         "statusclient.htm", "WEBSOCKETHOST DONE"): return
        if not runWsTest("propsend a", "DONE", 
                         "proprecv.htm", "WEBSOCKETHOST DONE"): return
        if not runWsTest("o2litehost 500t da", "CLIENT DONE", 
                         "wsserv.htm", "WEBSOCKETHOST DONE"): return
        if not runWsTest("tapsub", "CLIENT DONE", 
                         "tappub.htm", "WEBSOCKETHOST DONE"): return
    dostall(120)  # infotest1 will fail if Bonjour has an entry from
                  # a previous o2 process; long timeout
    if not runTest("infotest1 o"): return
    # proptest returns almost instantly; maybe it takes awhile for
    #     the port to be released
    if not runTest("proptest s"): return

    if not runDouble("tappub d", "SERVER DONE",
                     "tapsub d", "CLIENT DONE"): return
    if not runDouble("unipub", "SERVER DONE",
                     "unisub", "CLIENT DONE"): return
    if not runDouble("o2litehost 500t d", "CLIENT DONE",
                     "o2liteserv t", "SERVER DONE"): return
    if not runDouble("o2litehost 500 d", "CLIENT DONE",
                     "o2liteserv u", "SERVER DONE"): return
    if not runDouble("statusclient", "CLIENT DONE",
                     "statusserver", "SERVER DONE"): return
    dostall(120)  # infotest2 will fail if Bonjour has an entry from
                  # a previous o2 process; long timeout
    if not runDouble("infotest2", "INFOTEST2 DONE",
                     "clockmirror", "CLOCKMIRROR DONE"): return
    if not runDouble("clockref", "CLOCKREF DONE",
                     "clockmirror", "CLOCKMIRROR DONE"): return
    if not runDouble("applead", "APPLEAD DONE",
                     "appfollow", "APPFOLLOW DONE"): return
    if not runDouble("o2client", "CLIENT DONE",
                     "o2server", "SERVER DONE"): return
    # run with TCP instead of UDP
    # I should fix the command line arguments here to be more regular:
    if not runDouble("o2client 1000t", "CLIENT DONE",
                     "o2server - 20t", "SERVER DONE"): return
    if not runDouble("nonblocksend", "CLIENT DONE",
                     "nonblockrecv", "SERVER DONE"): return
    if not runDouble("o2unblock", "CLIENT DONE",
                     "o2block", "SERVER DONE"): return
    if not runDouble("oscsendtest u", "OSCSEND DONE",
                     "oscrecvtest u", "OSCRECV DONE"): return
    if not runDouble("oscsendtest u", "OSCSEND DONE",
                     "oscanytest u", "OSCANY DONE"): return
    if not runDouble("oscsendtest", "OSCSEND DONE",
                     "oscrecvtest", "OSCRECV DONE"): return
    if not runDouble("tcpclient", "CLIENT DONE",
                     "tcpserver", "SERVER DONE"): return
    if have_hub_tests:
        if not runDouble("hubclient", "HUBCLIENT DONE",
                         "hubserver", "HUBSERVER DONE", True): return
    else:
        print("hubclient or hubserver not found, skipping hub test.")
    if not runDouble("propsend", "DONE",
                     "proprecv", "DONE"): return
    if not runDouble("dropclient", "DROPCLIENT DONE",
                     "dropserver", "DROPSERVER DONE"): return
    if not runDouble("o2client 1000t", "CLIENT DONE",
                     "shmemserv u", "SERVER DONE"): return

# tests for compatibility with liblo are run only if the binaries were built
# In CMake, set BUILD_TESTS_WITH_LIBLO to create the binaries

    if os.path.isfile(BIN + '/' + "lo_oscrecv" + EXE):
        if not runDouble("oscsendtest @u", "OSCSEND DONE",
                         "lo_oscrecv u", "OSCRECV DONE"): return
        if not runDouble("oscsendtest @", "OSCSEND DONE",
                         "lo_oscrecv", "OSCRECV DONE"): return

    if os.path.isfile(BIN + '/' + "lo_oscsend" + EXE):
        if not runDouble("lo_oscsend u", "OSCSEND DONE",
                         "oscrecvtest u", "OSCRECV DONE"): return
        if not runDouble("lo_oscsend", "OSCSEND DONE",
                         "oscrecvtest", "OSCRECV DONE"): return

    if os.path.isfile(BIN + '/' + "lo_bndlsend" + EXE):
        if not runDouble("lo_bndlsend u", "OSCSEND DONE",
                         "oscbndlrecv u", "OSCRECV DONE"): return
        if not runDouble("lo_bndlsend", "OSCSEND DONE",
                         "oscbndlrecv", "OSCRECV DONE"): return

    if os.path.isfile(BIN + '/' + "lo_bndlrecv" + EXE):
        if not runDouble("oscbndlsend Mu", "OSCSEND DONE",
                         "lo_bndlrecv u", "OSCRECV DONE"): return
        if not runDouble("oscbndlsend M", "OSCSEND DONE",
                         "lo_bndlrecv", "OSCRECV DONE"): return


# Discovery ports were used by O2 before O2 discovery was switched to
#    Bonjour. Old-style built-in discovery is still an option though.
#    checkports() was created to test that allocated ports were properly
#    released (important because of some problems in older version of macOS).
#    This is no longer part of regression testing, but you might want to run
#    it (and the call to checkports() below) if using the (old) built-in
#    discovery protocol.
# print("Initial discovery port status ...")
# checkports(True, True)
runAllTests()
print("stall to recover ports".rjust(30) + ": ", end='', flush=True)
dostall(STALL_SEC)
print()

ports_ok = True
# ports_ok, countmsg = checkports(False, False)
if not ports_ok:
     print("ERROR: A port was not freed by some process. " + countmsg + "\n")
elif allOK:
     print("****    All O2 regression tests PASSED.")


if not allOK:
    print("ERROR: Exiting regression tests because a test failed.")
    print("       See above for output from the failing test(s).")
    print("\nNOTE:  If firewall pop-ups requested access to the network,")
    print("       that *might* affect timing and cause a test to fail.")
    print("       If you granted access, permission should be granted")
    print("       without delay if you run the test again.")

