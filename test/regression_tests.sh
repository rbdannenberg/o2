#!/bin/sh

if [ -d "../Debug" ]; then
    BIN="../Debug"
else
    BIN=".."
fi
# for linux, ../Debug may exist to hold a debug copy of the
# O2 library, but we need to indicate BIN is .. to find tests
if [ `uname` == 'Linux' ]; then
    BIN=".."
fi


# runtest testname - runs testname, saving output in output.txt,
#    searches output.txt for single full line containing "DONE",
#    returns status=0 if DONE was found (indicating success), or
#    else status=-1 (indicating failure).
runtest(){
    printf "%30s: "  "$1"
    $BIN/$1 > output.txt
    if grep -Fxq "DONE" output.txt
    then
        echo "PASS"
        status=0
    else
        status=-1
    fi
}


rundouble(){
    printf "%30s: "  "$1+$3"
    ./regression_run_two.sh "$BIN/$1" "$BIN/$3" &>misc.txt 2>&1
    if grep -Fxq "$2" output.txt
    then
        if grep -Fxq "$4" output2.txt
        then
            echo "PASS"
            status=0
        else
            echo "FAIL"
            status=-1
        fi
    else
        echo "FAIL"
        status=-1
    fi
}


# the while loop never iterates, it is here to make "break"
# into a kind of "goto error" when an error is encountered
while true; do

    errorflag=1
    echo "Running regression tests for O2 ..."

    runtest "dispatchtest"
    if [ $status == -1 ]; then break; fi

    runtest "typestest"
    if [ $status == -1 ]; then break; fi

    runtest "taptest"
    if [ $status == -1 ]; then break; fi

    runtest "coercetest"
    if [ $status == -1 ]; then break; fi

    runtest "longtest"
    if [ $status == -1 ]; then break; fi

    runtest "arraytest"
    if [ $status == -1 ]; then break; fi

    runtest "bundletest"
    if [ $status == -1 ]; then break; fi

    runtest "infotest1"
    if [ $status == -1 ]; then break; fi

    rundouble "statusserver" "SERVER DONE" "statusclient" "CLIENT DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "infotest2" "INFOTEST2 DONE" "clockslave" "CLOCKSLAVE DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "clockmaster" "CLOCKMASTER DONE" "clockslave" "CLOCKSLAVE DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "o2client" "CLIENT DONE" "o2server" "SERVER DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "oscsendtest u" "OSCSEND DONE" "oscrecvtest u" "OSCRECV DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "oscsendtest u" "OSCSEND DONE" "oscanytest u" "OSCANY DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "oscsendtest" "OSCSEND DONE" "oscrecvtest" "OSCRECV DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "tcpclient" "CLIENT DONE" "tcpserver" "SERVER DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "oscbndlsend u" "OSCSEND DONE" "oscbndlrecv u" "OSCRECV DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "oscbndlsend" "OSCSEND DONE" "oscbndlrecv" "OSCRECV DONE"
    if [ $status == -1 ]; then break; fi

# tests for compatibility with liblo are run only if the binaries were built
# In CMake, set BUILD_TESTS_WITH_LIBLO to create the binaries
    if [ -f "$BIN/lo_oscrecv" ]; then
        rundouble "oscsendtest Mu" "OSCSEND DONE" "lo_oscrecv u" "OSCRECV DONE"
        if [ $status == -1 ]; then break; fi

        rundouble "oscsendtest M" "OSCSEND DONE" "lo_oscrecv" "OSCRECV DONE"
        if [ $status == -1 ]; then break; fi
    fi

    if [ -f "$BIN/lo_oscsend" ]; then
        rundouble "lo_oscsend u" "OSCSEND DONE" "oscrecvtest u" "OSCRECV DONE"
        if [ $status == -1 ]; then break; fi

        rundouble "lo_oscsend" "OSCSEND DONE" "oscrecvtest" "OSCRECV DONE"
        if [ $status == -1 ]; then break; fi
    fi

    if [ -f "$BIN/lo_bndlsend" ]; then
        rundouble "lo_bndlsend u" "OSCSEND DONE" "oscbndlrecv u" "OSCRECV DONE"
        if [ $status == -1 ]; then break; fi

        rundouble "lo_bndlsend" "OSCSEND DONE" "oscbndlrecv" "OSCRECV DONE"
        if [ $status == -1 ]; then break; fi
    fi

    if [ -f "$BIN/lo_bndlrecv" ]; then
        rundouble  "oscbndlsend Mu" "OSCSEND DONE" "lo_bndlrecv u" "OSCRECV DONE"
        if [ $status == -1 ]; then break; fi

        rundouble  "oscbndlsend M" "OSCSEND DONE" "lo_bndlrecv" "OSCRECV DONE"
        if [ $status == -1 ]; then break; fi
    fi

    # exit with no errors
    errorflag=0
    break

done

if [ "$errorflag" == 1 ]
then
    echo "ERROR: exiting regression tests because a test failed."
    echo "       See output.txt (and possibly output2.txt) for "
    echo "       output from the failing test(s)."
else
    echo "****    All O2 regression tests PASSED."
fi
