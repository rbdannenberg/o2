#!/bin/sh

# regression_run_wstest program1 bindir url
# runs the two programs in parallel, saves output of each,
# and waits for them to terminate. We use this script so
# that the shell output generated when a process finishes
# can be captured as output and redirected so as not to
# mess up the formatted output of regression_tests.sh

$1 > output.txt &
PID1=$!
$2/websockhost a@ > output2.txt
PID2=$!

open "http://localhost:8080/$3"

wait $PID1
wait $PID2
