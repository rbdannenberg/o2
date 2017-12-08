#!/bin/sh

# Refer to the Developer Documentation section in the User Manual for a detailed explanation #
echo "Test Script started"
# Printing arguments
echo "Printing arguments.."
echo List of IPS=$1
echo List of tests=$2
echo Local OS=$3
echo ID of Tests=$4
echo Build version#=$5
currDir=`pwd`
ipString=$1
testsToRun=$2
localOS=$3
testID=$4
buildHash="$5"
datevar=`date +%Y-%m-%d-%H-%M-%S`
filename=$testID"_"$datevar
IFS=','; IP=($ipString); unset IFS;
IFS=';'; testsList=($testsToRun); unset IFS;
jobIndex=0
testcaseIndex=0

# If the local OS is Ubuntu
if [ "$localOS" = "ubuntu" ]
then
  echo "Ubuntu is the host machine.."
  echo "Transferring script files to remote machine.."

  # Check if SSHPass is installed
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
# Transferring build script to the remote machine
for element in "${IP[@]}"
  do
    machineInfo=($element)
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
      sshpass -p $password scp -r $currDir/build.sh $username@$ip:
      if [ $? = 0 ]; then
        echo "Script file transfer completed.."
      else
        echo "Error in Transferring file.. Exiting.."
        exit 1
      fi
    fi
  done

  # For each machine, start the remote execution for the executables allocated to that machine
  testcaseIndex=0;
  for element in "${IP[@]}"
  do
    echo $element
    machineDetails=($element)
    ip=${machineDetails[0]}
    os=${machineDetails[1]}
    username=${machineDetails[2]}
    password=${machineDetails[3]}
    testcase=${testsList[$testcaseIndex]}
    # Remove [] from the testcase string
    testcase=$(sed -e "s/\[//g;s/\]//g" <<< $testcase)
    testcaseIndex=$((testcaseIndex+1))
    echo "$ip"
    echo "$username"
    echo "$os"
    echo "$password"
    echo "$testcase"
    testcases=\"$testcase\"
    echo $testcases
    # If password is not given, implies it is local machine
    if [ -z $password ]
    then
      echo "Executing in local machine.."
      if [ $os == "Mac OS X" ] || [ $os == "Mac" ] ||  [ $os == "mac" ] ||  [ $os == "ubuntu" ]
      then
        echo "Here in local execution"
        pwd
        echo $currDir
        chmod +x $currDir/src/scripts/build.sh
        $currDir/src/scripts/build.sh "$os" "$testcases" "$password" "$buildHash" 2>&1 &
      else
      echo "Windows machine.."
      chmod +x $currDir/src/scripts/build.sh
      "C:/Program\\ Files/Git/bin/sh $currDir/src/scripts/build.sh "$os" "$testcases" "$password" "$buildHash"" 2>&1 &
      fi
    else
      echo "Executing in remote machine.."
      # If remote machine is windows, use sh utility to execute the build script
      if [ $os == "windows" ] ; then
        echo "$ip"
        echo "$username"
        echo "$os"
        echo "$password"
        echo "$testcase"
        sshpass -p $password ssh -o ConnectTimeout=6 -o StrictHostKeyChecking=no $username@$ip "C:/Program\\ Files/Git/bin/sh build.sh $os \'$testcase\' $password $buildHash" 2>&1 &
        else
        sshpass -p $password ssh -o ConnectTimeout=6 -o StrictHostKeyChecking=no $username@$ip ./build.sh "$os" "$testcases" "$password" "$buildHash" 2>&1 &
     fi
    fi
    cd $HOME
  done
  wait
echo "Test case has been successfully executed.."
sleep 60
ipString=$1
testsToRun=$2
localOS=$3
testID=$4
IFS=','; IP=($ipString); unset IFS;
IFS=';'; testsList=($testsToRun); unset IFS;
testcaseIndex=0;

#Transfer output files to this machine
mkdir -p $HOME/o2Outputs/$filename
for element in "${IP[@]}"
do
  echo $element
  machineDetails=($element)
  ip=${machineDetails[0]}
  os=${machineDetails[1]}
  username=${machineDetails[2]}
  password=${machineDetails[3]}
  testcase=${testsList[$testcaseIndex]}
  testcase=$(sed -e "s/\[//g;s/\]//g" <<< $testcase)
  testcaseIndex=$((testcaseIndex+1))
  echo "$ip"
  echo "$username"
  echo "$os"
  echo "$password"
  echo "$testcase"
  temptest=\"$testcase\"
  echo "$temptest"
  IFS=","; elm=($testcase); unset IFS;
  val="${#elm[@]}"
  j=0;
  # If password is not empty, use SSH to transfer output files from remote machine
  if [ ! -z $password ]
  then
    for elmTemp in "${elm[@]}"
    do
      IFS=" "; elmSpace=($elmTemp); unset IFS;
      val="${#elmSpace[@]}"
    if [[ $os == "ubuntu" ]] ;
      then
      if [[ $val -eq 3 ]] && [[ "${elmSpace[2]}" =~ ^[0-9]+$ ]] ; then
        sshpass -p $password scp -o StrictHostKeyChecking=no -r $username@$ip:/home/$username/o2Test/Outputs/${elmSpace[0]}${elmSpace[1]}${elmSpace[2]}.txt $HOME/o2Outputs/$filename
      elif [[ $val -eq 3 ]] && ! [[ "${elmSpace[2]}" =~ ^[0-9]+$ ]]; then
        sshpass -p $password scp -o StrictHostKeyChecking=no -r $username@$ip:/home/$username/o2Test/Outputs/${elmSpace[0]}.txt $HOME/o2Outputs/$filename
      else
        sshpass -p $password scp -o StrictHostKeyChecking=no -r $username@$ip:/home/$username/o2Test/Outputs/${elmSpace[0]}.txt $HOME/o2Outputs/$filename
      fi
    elif [[ $os == "mac" ]] ; then
      if [[ $val -eq 3 ]] && [[ "${elmSpace[2]}" =~ ^[0-9]+$ ]] ; then
        sshpass -p $password scp -o StrictHostKeyChecking=no -r $username@$ip:/Users/$username/o2Test/Outputs/${elmSpace[0]}${elmSpace[1]}${elmSpace[2]}.txt $HOME/o2Outputs/$filename
      elif [[ $val -eq 3 ]] && ! [[ "${elmSpace[2]}" =~ ^[0-9]+$ ]]; then
        sshpass -p $password scp -o StrictHostKeyChecking=no -r $username@$ip:/Users/$username/o2Test/Outputs/${elmSpace[0]}.txt $HOME/o2Outputs/$filename
      else
        sshpass -p $password scp -o StrictHostKeyChecking=no -r $username@$ip:/Users/$username/o2Test/Outputs/${elmSpace[0]}.txt $HOME/o2Outputs/$filename
      fi
    else
      if [[ $val -eq 3 ]] && [[ "${elmSpace[2]}" =~ ^[0-9]+$ ]] ; then
        sshpass -p $password scp -o StrictHostKeyChecking=no -r $username@$ip:C:/Users/$username/o2Test/Outputs/${elmSpace[0]}${elmSpace[1]}${elmSpace[2]}.txt $HOME/o2Outputs/$filename
      elif [[ $val -eq 3 ]] && ! [[ "${elmSpace[2]}" =~ ^[0-9]+$ ]]; then
        sshpass -p $password scp -o StrictHostKeyChecking=no -r $username@$ip:C:/Users/$username/o2Test/Outputs/${elmSpace[0]}.txt $HOME/o2Outputs/$filename
      else
        sshpass -p $password scp -o StrictHostKeyChecking=no -r $username@$ip:C:/Users/$username/o2Test/Outputs/${elmSpace[0]}.txt $HOME/o2Outputs/$filename
      fi
    fi
    done
  else
    # Password is empty, implies that the execution has happened in the local machine..
    # Use mv command to transfer files to the output consolidation directory
    for elmTemp in "${elm[@]}"
    do
      IFS=" "; elmSpace=($elmTemp); unset IFS;
      val="${#elmSpace[@]}"
      if [[ $val -eq 3 ]] && [[ "${elmSpace[2]}" =~ ^[0-9]+$ ]]; then
        mv -f $HOME/o2Test/Outputs/${elmSpace[0]}${elmSpace[1]}${elmSpace[2]}.txt $HOME/o2Outputs/$filename
      elif [[ $val -eq 3 ]] && ! [[ "${elmSpace[2]}" =~ ^[0-9]+$ ]]; then
          mv -f $HOME/o2Test/Outputs/${elmSpace[0]}.txt $HOME/o2Outputs/$filename
      else
        mv -f $HOME/o2Test/Outputs/${elmSpace[0]}.txt $HOME/o2Outputs/$filename
      fi
    done
  fi
    echo "Executing done.."
done
# If the local OS is Mac
elif [ "$localOS" = "Mac OS X" ] || [ "$localOS" = "Mac" ] || [ "$localOS" = "mac" ]
then
  echo "Mac is the host machine.."
  echo "Checking SSHPass installation.."
  # Add path of 'which' utility to PATH variable
  export PATH=$PATH:/usr/bin/which
  export PATH=$PATH:/usr/local/bin

  # Check if SSHPass is installed
  which sshpass <<< pathSshpass
  eval $pathSshpass
  pwd
  which sshpass > File
  if [ -z $pathSshpass  ]; then
     echo "VERIFIED: SSHPass installed.."
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
     which sshpass <<< File
     export PATH=$PATH:$File
   fi

   # Transferring build script to the remote machine
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
      echo $username@$ip
      echo $password
      echo $currDir
      chmod +x $currDir/src/scripts/build.sh
      sshpass -p $password scp -r $currDir/src/scripts/build.sh $username@$ip:
      if [ $? = 0 ]; then
        echo "Script file transfer completed.."
      else
        echo "Error in Transferring file.. Exiting.."
        exit 1
      fi
    fi
  done

  # For each machine, start the remote execution for the executables allocated to that machine
  testcaseIndex=0;
  for element in "${IP[@]}"
  do
    echo $element
    machineDetails=($element)
    ip=${machineDetails[0]}
    os=${machineDetails[1]}
    username=${machineDetails[2]}
    password=${machineDetails[3]}
    testcase=${testsList[$testcaseIndex]}
    testcase=$(sed -e "s/\[//g;s/\]//g" <<< $testcase)
    testcaseIndex=$((testcaseIndex+1))
    echo "$ip"
    echo "$username"
    echo "$os"
    echo "$password"
    echo "$testcase"
    testcases=\"$testcase\"
    echo $testcases
    # If password is not given, implies it is local machine
    if [ -z $password ]
    then
      echo "Executing in local machine.."
      if [ $os == "Mac OS X" ] || [ $os == "Mac" ] ||  [ $os == "mac" ] ||  [ $os == "ubuntu" ]
      then
        echo "Here in local execution"
        pwd
        echo $currDir
        chmod +x $currDir/src/scripts/build.sh
        $currDir/src/scripts/build.sh "$os" "$testcases" "$password" "$buildHash" 2>&1 &
      else
      echo "Windows machine.."
      "C:/Program\\ Files/Git/bin/sh $currDir/src/scripts/build.sh "$os" "$testcases" "$password" "$buildHash"" 2>&1 &
      fi
    else
      echo "Executing in remote machine.."
      if [ $os == "windows" ] ; then
        echo "$ip"
        echo "$username"
        echo "$os"
        echo "$password"
        echo "$testcase"
        sshpass -p $password ssh -o ConnectTimeout=6 -o StrictHostKeyChecking=no $username@$ip "C:/Program\\ Files/Git/bin/sh build.sh $os \'$testcase\' $password $buildHash" 2>&1 &
        else
        sshpass -p $password ssh -o ConnectTimeout=6 -o StrictHostKeyChecking=no $username@$ip ./build.sh "$os" "$testcases" "$password" "$buildHash" 2>&1 &
     fi
     cd $HOME
    fi
  done
  wait
echo "Test case has been successfully executed.."
sleep 30
ipString=$1
testsToRun=$2
localOS=$3
testID=$4
IFS=','; IP=($ipString); unset IFS;
IFS=';'; testsList=($testsToRun); unset IFS;
testcaseIndex=0;

mkdir -p $HOME/o2Outputs/$filename
for element in "${IP[@]}"
do
  echo $element
  machineDetails=($element)
  ip=${machineDetails[0]}
  os=${machineDetails[1]}
  username=${machineDetails[2]}
  password=${machineDetails[3]}
  testcase=${testsList[$testcaseIndex]}
    # Remove [] from the testcase string
  testcase=$(sed -e "s/\[//g;s/\]//g" <<< $testcase)
  testcaseIndex=$((testcaseIndex+1))
  echo "$ip"
  echo "$username"
  echo "$os"
  echo "$password"
  echo "$testcase"
  temptest=\"$testcase\"
  echo "$temptest"
  IFS=","; elm=($testcase); unset IFS;
  val="${#elm[@]}"
  j=0;

  if [ ! -z $password ]
  then
    for elmTemp in "${elm[@]}"
    do
      IFS=" "; elmSpace=($elmTemp); unset IFS;
      val="${#elmSpace[@]}"
      if [[ $os == "ubuntu" ]] ;
      then
      if [[ $val -eq 3 ]] && [[ "${elmSpace[2]}" =~ ^[0-9]+$ ]] ; then
        sshpass -p $password scp -o StrictHostKeyChecking=no -r $username@$ip:/home/$username/o2Test/Outputs/${elmSpace[0]}${elmSpace[1]}${elmSpace[2]}.txt $HOME/o2Outputs/$filename
      elif [[ $val -eq 3 ]] && ! [[ "${elmSpace[2]}" =~ ^[0-9]+$ ]]; then
        sshpass -p $password scp -o StrictHostKeyChecking=no -r $username@$ip:/home/$username/o2Test/Outputs/${elmSpace[0]}.txt $HOME/o2Outputs/$filename
      else
        sshpass -p $password scp -o StrictHostKeyChecking=no -r $username@$ip:/home/$username/o2Test/Outputs/${elmSpace[0]}.txt $HOME/o2Outputs/$filename
      fi
    elif [[ $os == "mac" ]] ; then
      if [[ $val -eq 3 ]] && [[ "${elmSpace[2]}" =~ ^[0-9]+$ ]] ; then
        sshpass -p $password scp -o StrictHostKeyChecking=no -r $username@$ip:/Users/$username/o2Test/Outputs/${elmSpace[0]}${elmSpace[1]}${elmSpace[2]}.txt $HOME/o2Outputs/$filename
      elif [[ $val -eq 3 ]] && ! [[ "${elmSpace[2]}" =~ ^[0-9]+$ ]]; then
        sshpass -p $password scp -o StrictHostKeyChecking=no -r $username@$ip:/Users/$username/o2Test/Outputs/${elmSpace[0]}.txt $HOME/o2Outputs/$filename
      else
        sshpass -p $password scp -o StrictHostKeyChecking=no -r $username@$ip:/Users/$username/o2Test/Outputs/${elmSpace[0]}.txt $HOME/o2Outputs/$filename
      fi
    else
      if [[ $val -eq 3 ]] && [[ "${elmSpace[2]}" =~ ^[0-9]+$ ]] ; then
        sshpass -p $password scp -o StrictHostKeyChecking=no -r $username@$ip:C:/Users/$username/o2Test/Outputs/${elmSpace[0]}${elmSpace[1]}${elmSpace[2]}.txt $HOME/o2Outputs/$filename
      elif [[ $val -eq 3 ]] && ! [[ "${elmSpace[2]}" =~ ^[0-9]+$ ]]; then
        sshpass -p $password scp -o StrictHostKeyChecking=no -r $username@$ip:C:/Users/$username/o2Test/Outputs/${elmSpace[0]}.txt $HOME/o2Outputs/$filename
      else
        sshpass -p $password scp -o StrictHostKeyChecking=no -r $username@$ip:C:/Users/$username/o2Test/Outputs/${elmSpace[0]}.txt $HOME/o2Outputs/$filename
      fi
    fi
    done
  else
    # Password is empty, implies that the execution has happened in the local machine..
    # Use mv command to transfer files to the output consolidation directory
    for elmTemp in "${elm[@]}"
    do
      IFS=" "; elmSpace=($elmTemp); unset IFS;
      val="${#elmSpace[@]}"
      if [[ $val -eq 3 ]] && [[ "${elmSpace[2]}" =~ ^[0-9]+$ ]]; then
        mv -f $HOME/o2Test/Outputs/${elmSpace[0]}${elmSpace[1]}${elmSpace[2]}.txt $HOME/o2Outputs/$filename
      elif [[ $val -eq 3 ]] && ! [[ "${elmSpace[2]}" =~ ^[0-9]+$ ]]; then
          mv -f $HOME/o2Test/Outputs/${elmSpace[0]}.txt $HOME/o2Outputs/$filename
      else
        mv -f $HOME/o2Test/Outputs/${elmSpace[0]}.txt $HOME/o2Outputs/$filename
      fi
    done
  fi
    echo "Executing done.."
done
else
  echo "Windows.."
 ## TO BE IMPLEMENTED - Dispatch machine has Windows as the local machine ##
fi
