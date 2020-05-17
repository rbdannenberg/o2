#!/bin/sh

# regression_run_two program1 program2
# runs the two programs in parallel, saves output of each,
# and waits for them to terminate. We use this script so
# that the shell output generated when a process finishes
# can be captured as output and redirected so as not to
# mess up the formatted output of regression_tests.sh

if [[ "$OSTYPE" == "darwin"* ]]; then
    script -t 1 output.txt $1 &
    PID1=$!
    script -t 1 output2.txt $2
    PID2=$!
else
    # LINUX
    $1 | tee output.txt &
    PID1=$!
    $2 | tee output2.txt
    PID2=$!
fi
wait $PID1
wait $PID2
