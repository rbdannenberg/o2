# script invoked as IP, username, password, OS Type
# 2>&1
HOMEDIR="/Users/aparrnaa/Desktop/CMU/Practicum/o2_MAIN_COPY/Outputs"
ip="$1"
os="$2"
password="$3"
username="$4"
testcases="$5"
hostdetails="$6"
hosts=(${hostdetails//,/ })
hostname=${hosts[2]}
hostip=${hosts[0]}
hostpwd=${hosts[3]}
echo $hostname
echo $hostip
echo $hostpwd
echo "executing ssh..."
datevar=`date +%Y-%m-%d-%H-%M-%S`
HOMEPATH="/home/osboxes/o2"
test=(${testcases//,/ })
echo $HOMEPATH
echo $datevar
/usr/local/bin/sshpass -p $3 ssh -t -t -o StrictHostKeyChecking=no $username@$ip /home/osboxes/build_script.sh $os $testcases
echo "Done.."
if [ ! -d "$HOMEDIR/$ip/$datevar" ];
then
mkdir $HOMEDIR/$ip/$datevar
else
  cd $HOMEDIR/$ip/$datevar
fi
for element in "${test[@]}"
do
   echo "$element"
   /usr/local/bin/sshpass -p $3 ssh -t -t -o StrictHostKeyChecking=no $username@$ip <<EOF
   sshpass -p "123" scp $HOMEPATH/$element.txt $hostname@$hostip:$HOMEDIR/$ip/$datevar
   logout
/usr/local/bin/sshpass -p $3 ssh -t -t -o StrictHostKeyChecking=no $username@$ip <<EOF
sshpass -p $hostpwd scp $HOMEPATH/$element.txt $hostname@$hostip:$HOMEDIR/$ip/
logout
EOF
done
