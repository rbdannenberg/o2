echo "test script"
echo $1
echo $2
echo $3
ipString=$1
testsToRun=$2
localOS=$3
IFS=','; IP=($ipString); unset IFS;
IFS=','; testsList=($testsToRun); unset IFS;
i=0
if [ "$localOS" = "ubuntu" ]
then
  echo "Ubuntu is the host machine.."
  for element in "${IP[@]}"
  do
    echo $element
    machineDetails=($element)
    ip=${machineDetails[0]}
    os=${machineDetails[1]}
    username=${machineDetails[2]}
    password=${machineDetails[3]}
    testcases=${testsList[$i]}
    $i=$i+1
    echo $ip
    echo $username
    echo $os
    echo $password
    echo $testcases
    /Users/aparrnaa/Desktop/CMU/Practicum/BACKEND/sshpass-1.05/sshpass -p $password ssh -v -f -o StrictHostKeyChecking=no $username@$ip /home/osboxes/try1/build.sh $os $testcases $password
    #echo "Installing SSHPASS.."
    #curl -O -L http://downloads.sourceforge.net/project/sshpass/sshpass/1.05/sshpass-1.05.tar.gz
    #tar zxvf sshpass-1.05.tar.gz && cd sshpass-1.05
    #./configure
    #make
    #make install
    #var=`which sshpass`
    #export PATH=$PATH:$var
  done
elif [ "$localOS" = "Mac OS X" ]
then
  echo "Mac is the host machine.."
  #echo "Installing SSHPASS.."
  #curl -O -L http://downloads.sourceforge.net/project/sshpass/sshpass/1.05/sshpass-1.05.tar.gz
#  tar zxvf sshpass-1.05.tar.gz && cd sshpass-1.05
#  ./configure
#  make
#  make install
#  var=`which sshpass`
#  export PATH=$PATH:$var
  for element in "${IP[@]}"
  do
    echo $element
    machineDetails=($element)
    ip=${machineDetails[0]}
    os=${machineDetails[1]}
    username=${machineDetails[2]}
    password=${machineDetails[3]}
    testcases=${testsList[$i]}
    echo $ip
    echo $username
    echo $os
    echo $password
    echo $testcases
    /Users/aparrnaa/Desktop/CMU/Practicum/BACKEND/sshpass-1.05/sshpass -p $password ssh -v -f -o StrictHostKeyChecking=no $username@$ip /home/osboxes/try1/builder.sh /home/osboxes/try1/build.sh $os $testcases $password
  done

fi
#password="osboxes.org"
#username="osboxes"
#os="ubuntu"
#ip1="10.0.0.96"
#ip2="10.0.0.145"
#/Users/aparrnaa/Desktop/CMU/Practicum/BACKEND/sshpass-1.05/sshpass -p $password ssh -t -t -o StrictHostKeyChecking=no $username@$ip1 /home/osboxes/try1/builder.sh $os &
#/Users/aparrnaa/Desktop/CMU/Practicum/BACKEND/sshpass-1.05/sshpass -p $password ssh -t -t -o StrictHostKeyChecking=no $username@$ip2 /home/osboxes/try1/builder.sh $os &
#wait
#echo "done"
