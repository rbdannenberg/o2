#!/bin/bash

# script invoked as IP, username, password, OS Type
# 2>&1
ip="$1"
os="$2"
password="$3"
username="$4"
echo "executing ssh..."
/usr/local/bin/sshpass -p $3 ssh -T -o StrictHostKeyChecking=no $username@$ip /home/osboxes/build_script.sh $os arraytest
echo "Done.."
