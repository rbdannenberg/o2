# regression_tests.py -- a rewrite of regression_tests.sh for Windows
#    using Python 3
#
# Roger Dannenberg   Jan 2017, 2020
#
# Run this in the o2/tests directory where it is found
#

# get print to be compatible even if using python 2.x:
from __future__ import print_function

import os
import platform
import subprocess
import shlex
import threading
from threading import Timer
from checkports import checkports
import time

print_all_output = False

IS_OSX = False
TIMEOUT_SEC = 250
# I saw a failure of oscbndlsend+oscbndlrecv because port 8100 could
# not be bound, but I could then run by hand, so I am guessing that
# maybe it was used in a previous test and linux would not reuse it
# so quickly. So now, we wait betwen tests to see if it helps.
# 5s was not enough, but 30s seems to work. Trying 20s now.
STALL_SEC = 20  # time after run to make sure ports are free
if platform.system() == "Darwin":
    STALL_SEC = 1  # I don't think we need to stall for macOS
    IS_OSX = True
    input("macOS tip: turn Firewall OFF to avoid orphaned ports ")

allOK = True

# Is this Windows?
EXE = ""
if os.name == 'nt':
    EXE = ".exe"


# Find the binaries
if os.path.isdir(os.path.join(os.getcwd(), '../Debug')):
   BIN="../Debug"
else:
   BIN=".."
# In linux, there is likely to be a debug version of the
# library copied to ../Debug, but tests are built in ..
if platform.system() == 'Linux':
    BIN=".."

def findLineInString(line, aString):
    return ('\n' + line + '\n') in aString


class RunInBackground(threading.Thread):
    def __init__(self, command):
        self.command = command
        self.output = ""
        self.errout = ""
        threading.Thread.__init__(self)

    def run(self):
        output1 = ""
        output2 = ""
        args = shlex.split(self.command)
        args[0] = BIN + '/' + args[0] + EXE
        process = subprocess.Popen(args,
                                   shell=False, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE)
        timer = Timer(TIMEOUT_SEC, process.kill)
        try:
            timer.start()
            (self.output, self.errout) = process.communicate()
        finally:
            timer.cancel()
        self.output = self.output.decode("utf-8").replace('\r\n', '\n')
        self.errout = self.errout.decode("utf-8").replace('\r\n', '\n')
        

# runTest testname - runs testname, saving output in output.txt,
#    searches output.txt for single full line containing "DONE",
#    returns status=0 if DONE was found (indicating success), or
#    else status=-1 (indicating failure).
def runTest(command, stall=False, quit_on_port_loss=False):
    global allOK
    if stall: dostall()
    print(command.rjust(30) + ": ", end='', flush=True)
    args = shlex.split(command)
    args[0] = BIN + '/' + args[0] + EXE
    process = subprocess.Popen(args,
                               shell=False, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    timer = Timer(TIMEOUT_SEC, process.kill)
    try:
        timer.start()
        (stdout, stderr) = process.communicate()
    finally:
        timer.cancel()
    stdout = stdout.decode("utf-8").replace('\r\n', '\n')
    stderr = stderr.decode("utf-8").replace('\r\n', '\n')
    portsOK, countmsg = checkports(False, False)
    if findLineInString("DONE", stdout):
        print("PASS", countmsg)
        # to return success (True), process must not have orphaned a port,
        # but I can't figure out why macOS orphans ports, and I can't get
        # through a complete test run without losing at least one port,
        # so we print errors, but we do not stop the testing
        if ((not IS_OSX) or quit_on_port_loss) and (not portsOK):
            allOK = False # halt the testing
    else:
        print("FAIL", countmsg)
        allOK = False
    if (not portsOK) or (not allOK):
        print("**** Failing output:")
        print(stdout)
        print("**** Failing error output:")
        print(stderr)
    elif print_all_output:
        print("**** stdout")
        print(stdout)
    return allOK


def dostall():
    print("stall to recover ports".rjust(30) + ":", end='', flush=True)
    time.sleep(STALL_SEC)
    print(" DONE")


def runDouble(prog1, out1, prog2, out2, stall=False):
    global allOK
    if stall: dostall()
    print((prog1 + '+' + prog2).rjust(30) + ": ", end='', flush=True)
    p1 = RunInBackground(prog1)
    p1.start()
    p2 = RunInBackground(prog2)
    p2.start()
    p1.join()
    p2.join()
    portsOK, countmsg = checkports(False, False)
    allOK = False
    if findLineInString(out1, p1.output):
        if findLineInString(out2, p2.output):
            print("PASS", countmsg)
            allOK = True
    if not allOK:
        print("FAIL", countmsg)
    elif ((not IS_OSX) or quit_on_port_loss) and (not portsOK):
        allOK = False # halt the testing
    if (not portsOK) or (not allOK):
        print("**** Failing output from " + prog1)
        print(p1.output)
        print("**** Failing error output from " + prog1)
        print(p1.errout)

        print("**** Failing output from " + prog2)
        print(p2.output)
        print("**** Failing error output from " + prog2)
        print(p2.errout)
    elif print_all_output:
        print("**** p1.output")
        print(p1.output)
        print("**** p2.output")
        print(p2.output)
    return allOK


def runAllTests():
    print("Initial discovery port status ...")
    checkports(True, True)
    extensions = input("Run pattern match and bundle tests? [y,n]: ")
    extensions = "y" in extensions.lower()
    print("Running regression tests for O2 ...")

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
    if not runTest("infotest1"): return
    if not runTest("proptest"): return

    if not runDouble("statusclient", "CLIENT DONE",
                     "statusserver", "SERVER DONE"): return
    if not runDouble("infotest2", "INFOTEST2 DONE",
                     "clockmirror", "CLOCKMIRROR DONE"): return
    if not runDouble("clockref", "CLOCKREF DONE",
                     "clockmirror", "CLOCKMIRROR DONE"): return
    if not runDouble("applead", "APPLEAD DONE",
                     "appfollow", "APPFOLLOW DONE"): return
    if not runDouble("o2client", "CLIENT DONE",
                     "o2server", "SERVER DONE"): return
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
    if not runDouble("hubclient", "HUBCLIENT DONE",
                     "hubserver", "HUBSERVER DONE", True): return
    if not runDouble("propsend", "DONE",
                     "proprecv", "DONE"): return
    if not runDouble("tappub", "SERVER DONE",
                     "tapsub", "CLIENT DONE"): return
    if not runDouble("dropclient", "DROPCLIENT DONE",
                     "dropserver", "DROPSERVER DONE"): return
    if not runDouble("o2litehost 500t", "CLIENT DONE",
                     "o2liteserv t", "SERVER DONE"): return
    if not runDouble("o2litehost 500", "CLIENT DONE",
                     "o2liteserv u", "SERVER DONE"): return
    if not runDouble("o2client 1000t", "CLIENT DONE",
                     "shmemserv u", "SERVER DONE"): return
    if extensions:
        if not runDouble("oscbndlsend u", "OSCSEND DONE",
                         "oscbndlrecv u", "OSCRECV DONE", True): return
        if not runDouble("oscbndlsend", "OSCSEND DONE",
                         "oscbndlrecv", "OSCRECV DONE", True): return

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


runAllTests()
dostall()
ports_ok, countmsg = checkports(False, False)
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

