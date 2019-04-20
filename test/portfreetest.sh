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

    rundouble "statusserver" "SERVER DONE" "statusclient" "CLIENT DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "statusserver" "SERVER DONE" "statusclient" "CLIENT DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "statusserver" "SERVER DONE" "statusclient" "CLIENT DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "statusserver" "SERVER DONE" "statusclient" "CLIENT DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "statusserver" "SERVER DONE" "statusclient" "CLIENT DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "statusserver" "SERVER DONE" "statusclient" "CLIENT DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "statusserver" "SERVER DONE" "statusclient" "CLIENT DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "statusserver" "SERVER DONE" "statusclient" "CLIENT DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "statusserver" "SERVER DONE" "statusclient" "CLIENT DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "statusserver" "SERVER DONE" "statusclient" "CLIENT DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "statusserver" "SERVER DONE" "statusclient" "CLIENT DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "statusserver" "SERVER DONE" "statusclient" "CLIENT DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "statusserver" "SERVER DONE" "statusclient" "CLIENT DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "statusserver" "SERVER DONE" "statusclient" "CLIENT DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "statusserver" "SERVER DONE" "statusclient" "CLIENT DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "statusserver" "SERVER DONE" "statusclient" "CLIENT DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "statusserver" "SERVER DONE" "statusclient" "CLIENT DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "statusserver" "SERVER DONE" "statusclient" "CLIENT DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "statusserver" "SERVER DONE" "statusclient" "CLIENT DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "statusserver" "SERVER DONE" "statusclient" "CLIENT DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "statusserver" "SERVER DONE" "statusclient" "CLIENT DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "statusserver" "SERVER DONE" "statusclient" "CLIENT DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "statusserver" "SERVER DONE" "statusclient" "CLIENT DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "statusserver" "SERVER DONE" "statusclient" "CLIENT DONE"
    if [ $status == -1 ]; then break; fi

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
