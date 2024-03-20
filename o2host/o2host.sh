#!/bin/sh
mypath=$0
cd "${mypath%/*}"
pwd > /Users/rbd/tmp/msg.txt
open -a "Terminal" ./o2host
