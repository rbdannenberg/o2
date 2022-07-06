#!/bin/sh

# This was the original code for regression testing. It has been moved
# to Python which also runs on Windows and applies a time limit so that
# it always terminates. The websocket tests are included here from when
# I thought that Python had problems, but the websocket tests have not
# been tested yet in this script and may not work.

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

ABIN=`realpath $BIN`
read -p "Run pattern match and bundle tests? [y,n]: " extensions
case ${extensions:0:1} in
    y|Y )
        extensions="y"
    ;;
    * )
        extensions="n"
    ;;
esac
read -p "Run web socket tests? [y,n]: " websockettests
case ${websockettests:0:1} in
    y|Y )
        websockettests="y"
    ;;
    * )
        websockettests="n"
    ;;
esac


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


runwstest(){
    printf "%30s: "  "$1+$3"
    ./regression_run_wstest.sh "$BIN/$1" "$BIN" "$3" &>misc.txt 2>&1
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

    runtest "stuniptest"
    if [ $status == -1 ]; then break; fi

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

    runtest "bridgeapi"
    if [ $status == -1 ]; then break; fi

    runtest "o2litemsg"
    if [ $status == -1 ]; then break; fi

    if [ $websockettests == "y" ]; then
        runwstest "o2server - 20t" "SERVER DONE" "o2client.htm" "WEBSOCKETHOST DONE"
        if [ $status == -1 ]; then break; fi

        runwstest "tappub" "SERVER DONE" "tapsub.htm" "WEBSOCKETHOST DONE"
        if [ $status == -1 ]; then break; fi

        runwstest "statusclient" "CLIENT DONE" "statusserver.htm" "WEBSOCKETHOST DONE"
        if [ $status == -1 ]; then break; fi

        runwstest "statusserver" "SERVER DONE" "statusclient.htm" "WEBSOCKETHOST DONE"
        if [ $status == -1 ]; then break; fi

        runwstest "propsend a" "DONE" "proprecv.htm" "WEBSOCKETHOST DONE"
        if [ $status == -1 ]; then break; fi

        runwstest "o2litehost 500t da" "CLIENT DONE" "wsserv.htm" "WEBSOCKETHOST DONE"
        if [ $status == -1 ]; then break; fi

        runwstest "proprecv" "DONE" "propsend.htm" "WEBSOCKETHOST DONE"
        if [ $status == -1 ]; then break; fi

        runwstest "tapsub" "CLIENT DONE" "tappub.htm" "WEBSOCKETHOST DONE"
        if [ $status == -1 ]; then break; fi
    fi

    if [ $extensions == "y" ]; then
        runtest "bundletest"
        if [ $status == -1 ]; then break; fi

        runtest "patterntest"
        if [ $status == -1 ]; then break; fi
    fi

    runtest "infotest1"
    if [ $status == -1 ]; then break; fi

    runtest "proptest"
    if [ $status == -1 ]; then break; fi

    rundouble "o2litehost 500t d" "CLIENT DONE" "o2liteserv t" "SERVER DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "o2litehost 500 d" "CLIENT DONE" "o2liteserv u" "SERVER DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "statusclient" "CLIENT DONE" "statusserver" "SERVER DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "infotest2" "INFOTEST2 DONE" "clockmirror" "CLOCKMIRROR DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "clockref" "CLOCKREF DONE" "clockmirror" "CLOCKMIRROR DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "applead" "APPLEAD DONE" "appfollow" "APPFOLLOW DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "o2client" "CLIENT DONE" "o2server" "SERVER DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "o2client 1000t" "CLIENT DONE" "o2server - 20t" "SERVER DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "nonblocksend" "CLIENT DONE" "nonblockrecv" "SERVER DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "o2unblock" "CLIENT DONE" "o2block" "SERVER DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "oscsendtest u" "OSCSEND DONE" "oscrecvtest u" "OSCRECV DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "oscsendtest u" "OSCSEND DONE" "oscanytest u" "OSCANY DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "oscsendtest" "OSCSEND DONE" "oscrecvtest" "OSCRECV DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "tcpclient" "CLIENT DONE" "tcpserver" "SERVER DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "hubclient" "HUBCLIENT DONE" "hubserver" "HUBSERVER DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "propsend" "DONE" "proprecv" "DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "tappub" "SERVER DONE" "tapsub" "CLIENT DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "unipub" "SERVER DONE" "unisub" "CLIENT DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "dropclient" "DROPCLIENT DONE" "dropserver" "DROPSERVER DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "o2client 1000t" "CLIENT DONE" "shmemserv u" "SERVER DONE"
    if [ $status == -1 ]; then break; fi

    if [ $extensions == "y" ]; then
        rundouble "oscbndlsend u" "OSCSEND DONE" "oscbndlrecv u" "OSCRECV DONE"
        if [ $status == -1 ]; then break; fi

        rundouble "oscbndlsend" "OSCSEND DONE" "oscbndlrecv" "OSCRECV DONE"
        if [ $status == -1 ]; then break; fi
    fi
# tests for compatibility with liblo are run only if the binaries were built
# In CMake, set BUILD_TESTS_WITH_LIBLO to create the binaries
    if [ -f "$BIN/lo_oscrecv" ]; then
        rundouble "oscsendtest @u" "OSCSEND DONE" "lo_oscrecv u" "OSCRECV DONE"
        if [ $status == -1 ]; then break; fi

        rundouble "oscsendtest @" "OSCSEND DONE" "lo_oscrecv" "OSCRECV DONE"
        if [ $status == -1 ]; then break; fi
    fi

    if [ -f "$BIN/lo_oscsend" ]; then
        rundouble "lo_oscsend u" "OSCSEND DONE" "oscrecvtest u" "OSCRECV DONE"
        if [ $status == -1 ]; then break; fi

        rundouble "lo_oscsend" "OSCSEND DONE" "oscrecvtest" "OSCRECV DONE"
        if [ $status == -1 ]; then break; fi
    fi

    if [ $extensions == "y" ]; then
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
