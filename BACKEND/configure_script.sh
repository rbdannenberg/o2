#!/bin/bash

# script invoked as IP, username, password, OS Type
# 2>&1
HOMEDIR="/Users/aparrnaa/Desktop/CMU/Practicum/o2_MAIN_COPY/Outputs"
ip="$1"
os="$2"
password="$3"
username="$4"
testcase="$5"
hostname="aparrnaa"
hostip="128.237.194.69"
echo "executing ssh..."
HOMEPATH="/home/osboxes/o2"
HOMEPATH="$HOMEPATH/$testcase.txt"
echo $HOMEPATH
/usr/local/bin/sshpass -p $3 ssh -t -t -o StrictHostKeyChecking=no $username@$ip /home/osboxes/build_script.sh $os $testcase
echo "Done.."
if [ ! -d "$HOMEDIR/$ip" ];
then
mkdir $HOMEDIR/$ip
else
  cd $HOMEDIR/$ip
fi
/usr/local/bin/sshpass -p $3 ssh -t -t -o StrictHostKeyChecking=no $username@$ip <<EOF
sshpass -p "123" scp $HOMEPATH $hostname@$hostip:$HOMEDIR/$ip/
logout
EOF
