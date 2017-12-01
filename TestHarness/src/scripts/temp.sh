#!/bin/sh
echo "test script"
echo $1
echo $2
echo $3
echo $4
echo $5
export PATH=$PATH:/usr/bin/which
export PATH=$PATH:/usr/local/bin
currDir=`pwd`
currDir=$currDir/src/scripts
ipString=$1
testsToRun=$2
localOS=$3
testID=$4
buildHash=$5
datevar=`date +%Y-%m-%d-%H-%M-%S`
filename=$testID"_"$datevar
echo $filename
echo $testsToRun
IFS=','; IP=($ipString); unset IFS;
IFS=';'; testsList=($testsToRun); unset IFS;
i=0
dummyvar=${testsList[$i]}
echo ${#testsList[@]}

if [ "$localOS" = "ubuntu" ]
then
  echo "Ubuntu is the host machine.."
  echo "Transferring script files to remote machine.."
  echo "Checking SSHPass installation.."
  which sshpass <<< pathSshpass
  echo $pathSshpass
  if [ -z $pathSshpass ]; then
    echo "SSHPass is not installed.."
    echo "Installing SSHPass..."
    wget -O http://downloads.sourceforge.net/project/sshpass/sshpass/1.05/sshpass-1.05.tar.gz
    tar -xvf sshpass-1.05.tar.gz
    rm -rf sshpass-1.05.tar.gz
    cd sshpass-1.05
    ./configure
    make install
  else
    echo "VERIFIED: SSHPass installed.."
  fi

  for element in "${IP[@]}"
  do
    if [ ${#element} = 3 ]
    then
      continue
    else
    machineDetails=($element)
    ip=${machineDetails[0]}
    os=${machineDetails[1]}
    username=${machineDetails[2]}
    password=${machineDetails[3]}
    sshpass -p $password scp -o ConnectTimeout=6 -r build.sh $username@$ip:
  fi
  done
  echo "Script file transfer completed.."
  for element in "${IP[@]}"
  do
    #echo $element
    machineDetails=($element)
    ip=${machineDetails[0]}
    os=${machineDetails[1]}
    username=${machineDetails[2]}
    password=${machineDetails[3]}
    testcases=${testsList[$i]}
    i=$((i+1))
    echo $ip
    echo $username
    echo $os
    echo $password
    echo $testcases
    if [ -z $password ]
    then
      echo "Executing in local machine.."
      if [ $os = "Mac OS X" ] || [ $os = "Mac" ] ||  [ $os = "mac" ] ||  [ $os = "ubuntu" ]
      then
        echo "here"
        $currDir/build.sh "$os" "$testcases" "$password" "$buildHash" &
      else
          echo "Windows machine.."
      fi
    else
      echo "Executing in remote machine.."
     sshpass -p $password ssh -f -o ConnectTimeout=6 -o StrictHostKeyChecking=no $username@$ip $HOME/build.sh "$os" "$testcases" "$password" "$buildHash" &

  fi
  done

elif [ "$localOS" = "Mac OS X" ] || [ "$localOS" = "Mac" ] || [ "$localOS" = "mac" ]
then
  echo "Mac is the host machine.."
  echo "Checking SSHPass installation.."
  pathSshpass="which sshpass"
  eval $pathSshpass
pwd
  which sshpass > File
  if [[ 'grep 'sshpass' $File' ]]; then
     echo "Got something here "
   else
     echo "SSHPass is not installed.."
     echo "Installing SSHPass..."
     curl http://downloads.sourceforge.net/project/sshpass/sshpass/1.05/sshpass-1.05.tar.gz -L -o sshpass-1.05.tar.gz
     tar -xvf sshpass-1.05.tar.gz
     rm -rf sshpass-1.05.tar.gz
     cd sshpass-1.05
     ./configure
     make install
     sshPath=`pwd`
     which sshpass > File
     File=`which sshpass`
     export PATH=$PATH:$sshPath
   fi
  for element in "${IP[@]}"
  do
    machineInfo=($element)
    echo ${#machineInfo[@]}
    if [ ${#machineInfo[@]} = 3 ]
    then
      continue
    else
      echo "Transferring script files to remote machine.."
      machineDetails=($element)
      ip=${machineDetails[0]}
      os=${machineDetails[1]}
      username=${machineDetails[2]}
      password=${machineDetails[3]}
      sshpass -p $password scp -r /Users/aparrnaa/Desktop/o2test/TestHarness/src/scripts/build.sh $username@$ip:
      echo "Script file transfer completed.."
    fi
  done
i=0;
  for element in "${IP[@]}"
  do
    echo $element
    machineDetails=($element)
    ip=${machineDetails[0]}
    os=${machineDetails[1]}
    username=${machineDetails[2]}
    password=${machineDetails[3]}
    testcase=${testsList[$i]}
    testcase=$(sed -e "s/\[//g;s/\]//g" <<< $testcase)
  #  testcase=$(sed -e "s/\ /;/g" <<< $testcase)
    i=$((i+1))
    echo "$ip"
    echo "$username"
    echo "$os"
    echo "$password"
    echo "$testcase"
    testcases=\"$testcase\"
    echo $testcases
    which sshpass
    echo "SSHPASS ABOVE MAIN"
    if [ -z $password ]
    then
      echo "Executing in local machine.."
      if [ $os == "Mac OS X" ] || [ $os == "Mac" ] ||  [ $os == "mac" ] ||  [ $os == "ubuntu" ]
      then
        echo "Here in local execution"
        pwd
        echo $currDir
        $currDir/build.sh "$os" "$testcases" "$password" "$buildHash" 2>&1 &
      else
      echo "Windows machine.."
      fi
    else
      echo "Executing in remote machine.."
      echo $PATH
      which sshpass
      echo "Checj above"
      pwd
      cd $HOME
     result=$(sshpass -p $password ssh -f -o ConnectTimeout=6 -o StrictHostKeyChecking=no $username@$ip ./build.sh "$os" "$testcases" "$password" "$buildHash" 2>&1) &
     echo result=$?
    fi
  done
  wait
  i=0;
echo "Test case has been successfully executed.."
sleep 60
ipString=$1
echo $1
echo "I came here "
echo $ipString
#ipString=${ipString:0:2}
testsToRun=$2
#testsToRun=${testsToRun:0:2}
localOS=$3
testID=$4
IFS=','; IP=($ipString); unset IFS;
IFS=';'; testsList=($testsToRun); unset IFS;
i=0;
echo $testsToRun
mkdir $HOME/o2Outputs/$filename
for element in "${IP[@]}"
do
  echo $element
  machineDetails=($element)
  ip=${machineDetails[0]}
  os=${machineDetails[1]}
  username=${machineDetails[2]}
  password=${machineDetails[3]}
  testcase=${testsList[$i]}
  testcase=$(sed -e "s/\[//g;s/\]//g" <<< $testcase)
#  testcase=$(sed -e "s/\ /;/g" <<< $testcase)
  i=$((i+1))
  echo "$ip"
  echo "$username"
  echo "$os"
  echo "$password"
  echo "$testcase"
  temptest=\"$testcase\"
  echo "$temptest"
  IFS=","; elm=($testcase); unset IFS;
  echo "got the test case name"
  val="${#elm[@]}"
  const=3
  j=0;
  if [ ! -z $password ]
  then
    for elmTemp in "${elm[@]}"
    do
      IFS=" "; elmSpace=($elmTemp); unset IFS;
      val="${#elmSpace[@]}"
      if [[ $val -eq $const ]]; then
        sshpass -p $password scp -o StrictHostKeyChecking=no -r $username@$ip:/home/$username/o2Test/Outputs/${elmSpace[0]}${elmSpace[1]}${elmSpace[2]}.txt $HOME/o2Outputs/$filename
      else
        sshpass -p $password scp -o StrictHostKeyChecking=no -r $username@$ip:/home/$username/o2Test/Outputs/${elmSpace[0]}.txt $HOME/o2Outputs/$filename
      fi
    done
  else
    for elmTemp in "${elm[@]}"
    do
      IFS=" "; elmSpace=($elmTemp); unset IFS;
      val="${#elmSpace[@]}"
      if [[ $val -eq $const ]]; then
        mv -f $HOME/o2Test/Outputs${elmSpace[0]}${elmSpace[1]}${elmSpace[2]}.txt $HOME/o2Outputs/$filename
      else
        mv -f $HOME/o2Test/Outputs/${elmSpace[0]}.txt $HOME/o2Outputs/$filename
      fi
    done
  fi
    echo "Executing done.."
done
else
  echo "Windows.."
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
