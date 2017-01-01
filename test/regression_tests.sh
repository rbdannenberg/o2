#!/bin/sh

# runtest testname - runs testname, saving output in output.txt,
#    searches output.txt for single full line containing "DONE",
#    returns status=0 if DONE was found (indicating success), or
#    else status=-1 (indicating failure).
runtest(){
    printf "%24s: "  "$1"
    ../Debug/$1 > output.txt
    if grep -Fxq "DONE" output.txt
    then
        echo "PASS"
        status=0
    else
        status=-1
    fi
}


rundouble(){
    printf "%24s: "  "$1+$3"
    ./regression_run_two.sh "../Debug/$1" "../Debug/$3" &>misc.txt 2>&1
    if grep -Fxq "$2" output.txt
    then
        if grep -Fxq "$4" output2.txt
        then
            echo "PASS"
            status=0
        else
            status=1
        fi
    else
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

    runtest "coercetest"
    if [ $status == -1 ]; then break; fi

    runtest "longtest"
    if [ $status == -1 ]; then break; fi

    runtest "arraytest"
    if [ $status == -1 ]; then break; fi

    runtest "bundletest"
    if [ $status == -1 ]; then break; fi

    rundouble "o2client" "CLIENT DONE" "o2server" "SERVER DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "oscrecvtest" "OSCRECV DONE" "oscsendtest" "OSCSEND DONE"
    if [ $status == -1 ]; then break; fi

    rundouble "tcpclient" "CLIENT DONE" "tcpserver" "SERVER DONE"
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
    echo "All O2 regression tests PASSED."
fi
