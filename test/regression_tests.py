# regression_tests.py -- a rewrite of regression_tests.sh for Windows
#    using Python 3 (and hopefully compatible with Python 2.7 on OS X)
#
# Roger Dannenberg   Jan 2017
#
# Run this in the o2/tests directory where it is found
#

# get print to be compatible even if using python 2.x:
from __future__ import print_function

import os
import subprocess
import shlex
import threading

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


def findLineInString(line, str):
    return ('\n' + line + '\n') in str


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
        (self.output, self.errout) = process.communicate()
        

# runTest testname - runs testname, saving output in output.txt,
#    searches output.txt for single full line containing "DONE",
#    returns status=0 if DONE was found (indicating success), or
#    else status=-1 (indicating failure).
def runTest(command):
    global allOK
    print(command.rjust(30) + ": ", end='')
    args = shlex.split(command)
    args[0] = BIN + '/' + args[0] + EXE
    process = subprocess.Popen(args,
                               shell=False, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    (stdout, stderr) = process.communicate()
    if findLineInString("DONE", stdout):
        print("PASS")
        return True
    else:
        print("FAIL")
        print("**** Failing output:")
        print(stdout)
        print("**** Failing error output:")
        print(stderr)
        allOK = False
        return False


def runDouble(prog1, out1, prog2, out2):
    global allOK
    print((prog1 + '+' + prog2).rjust(30) + ": ", end='')
    p1 = RunInBackground(prog1)
    p1.start()
    p2 = RunInBackground(prog2)
    p2.start()
    p1.join()
    p2.join()
    if findLineInString(out1, p1.output):
        if findLineInString(out2, p2.output):
            print("PASS")
            return True
    print("FAIL")
    print("**** Failing output from " + prog1)
    print(p1.output)
    print("**** Failing error output from " + prog1)
    print(p1.errout)

    print("**** Failing output from " + prog2)
    print(p2.output)
    print("**** Failing error output from " + prog2)
    print(p2.errout)

    allOK = False
    return False


def runAllTests():
    print("Running regression tests for O2 ...")
    if not runTest("dispatchtest"): return
    if not runTest("typestest"): return
    if not runTest("coercetest"): return
    if not runTest("longtest"): return
    if not runTest("arraytest"): return
    if not runTest("bundletest"): return

    if not runDouble("clockmaster", "CLOCKMASTER DONE",
                     "clockslave", "CLOCKSLAVE DONE"): return
    if not runDouble("o2client", "CLIENT DONE",
                     "o2server", "SERVER DONE"): return
    if not runDouble("oscsendtest u", "OSCSEND DONE",
                     "oscrecvtest u", "OSCRECV DONE"): return
    if not runDouble("oscsendtest", "OSCSEND DONE",
                     "oscrecvtest", "OSCRECV DONE"): return
    if not runDouble("tcpclient", "CLIENT DONE",
                     "tcpserver", "SERVER DONE"): return
    if not runDouble("oscbndlsend u", "OSCSEND DONE",
                     "oscbndlrecv u", "OSCRECV DONE"): return
    if not runDouble("oscbndlsend", "OSCSEND DONE",
                     "oscbndlrecv", "OSCRECV DONE"): return

# tests for compatibility with liblo are run only if the binaries were built
# In CMake, set BUILD_TESTS_WITH_LIBLO to create the binaries

    if os.path.isfile(BIN + '/' + "lo_oscrecv" + EXE):
        if not runDouble("oscsendtest Mu", "OSCSEND DONE",
                         "lo_oscrecv u", "OSCRECV DONE"): return
        if not runDouble("oscsendtest M", "OSCSEND DONE",
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
        if not runDouble ("oscbndlsend Mu", "OSCSEND DONE",
                          "lo_bndlrecv u", "OSCRECV DONE"): return
        if not runDouble ("oscbndlsend M", "OSCSEND DONE",
                          "lo_bndlrecv", "OSCRECV DONE"): return


runAllTests()
if allOK:
    print("****    All O2 regression tests PASSED.")
else:
    print("ERROR: exiting regression tests because a test failed.")
    print("       See output.txt (and possibly output2.txt) for ")
    print("       output from the failing test(s).")
