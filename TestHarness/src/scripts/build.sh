#!/bin/bash
# Build script for building O2 on Mac, Windows and Ubuntu platforms and executing the test executables for these machines

# Please refer to Developer Documentation for section in the User Manual for a detailed explanation #
echo OSname="$1"
testsToRun="$2"
testcases=$(echo "$2" | tr -d '"') # Renove "" from testcases string
echo Tests="$testcases"
echo password="$3"
echo Build Version#="$4"

# If Machine OS is ubuntu
if [ "$1" = "ubuntu" ]
then
TESTDIR="$HOME/o2Test"

#Check if TESTDIR exists, if not, create the structure
if [ ! -d $TESTDIR ]
then
  mkdir $TESTDIR
  cd $TESTDIR
  mkdir Outputs
  mkdir Build
fi

DIRECTORY="$TESTDIR/o2"
echo $DIRECTORY
cd $HOME
# Verifying installations for g++, cmake, git
echo "Checking for g++ installation.."
dpkg -s g++ | grep -q "installed"
if [ $? -eq 0 ]
  then
    echo "VERIFIED: g++ is installed.."
else
  echo "ERROR: 'g++' is not installed"
  sudo -S <<< $3 apt-get update
  sudo -S <<< $3 apt-get install -yy build-essential
fi

echo "Checking for libasound packages installation.."
dpkg -s libasound2-dev | grep -q "installed"
if [ $? -eq 0 ]
  then
    echo "VERIFIED: libasound is installed.."
else
  echo "ERROR: 'libasound' is not installed"
  sudo -S <<< $3 apt-get install -yy libasound2-dev
fi

echo "Checking for git installation..."
sudo -S <<< $3 apt-cache policy git | grep -q none
if [ $? -eq 0 ]
  then
    echo "ERROR: 'Git' is not installed"
    sudo -S <<< $3 apt-get install -yy git
else
  echo "VERIFIED: git is installed..."
  echo "Continuing.."
fi

echo "Checking for cmake installation..."
  apt-cache policy cmake | grep -q none
if [ $? -eq 0 ]
then
  echo "ERROR: 'CMake' is not installed"
  sudo -S <<< $3 apt-get install -yy cmake
else
  echo "VERIFIED: CMake is installed..."
  echo "Continuing.."
fi

buildDir="$TESTDIR/Build/build_"$4
libloDir="$DIRECTORY/liblo-0.28"
outputsDir="$TESTDIR/Outputs/"
echo "Directory Path Listing"
echo $buildDir
echo $libloDir
echo $outputsDir
# Check if specific build version # exists
  echo "Checking for the specified build version..."
  if [ -d $buildDir ]
  then
    echo "Specific build version exists.."
  else
    cd $TESTDIR
    git clone https://github.com/TejuGupta/o2.git
    cd $DIRECTORY
    git fetch --all
    git reset --hard origin/test-env
    git checkout $4
    if [ $? != 0 ] ; then
      echo "Wrong build version.."
      exit 1
    fi

    # For Liblo build, check if liblo library exists, else download it
   if [ -d $libloDir ]
   then
     echo "Liblo installation found.. Continue.."
   else
    echo "Installing liblo 0.28.."
    curl https://sourceforge.net/projects/liblo/files/liblo/0.28/liblo-0.28.tar.gz/download -L -o liblo-0.28.tar
    tar xopf liblo-0.28.tar
    rm -rf liblo-0.28.tar
    cd liblo-0.28
    ./configure --enable-static
    make
    cp src/.libs/liblo.a liblo_s64.a
   fi
    echo "Building o2.."
    cd $DIRECTORY
    # Specify build location as BuildDir
    cmake -H. -B$buildDir
    if [ $? != 0 ]
    then
      echo "Error in CMake Build.."
      exit 1
    fi
    cd $buildDir
    make
    if [ $? != 0 ]
    then
      echo "Error in make.."
      exit 1
    fi
  fi
# Clear all output files in outputsDir folder
rm -f $outputsDir/*

# For each test executable in $testcases, check if it exists and then execute it as a background process
IFS=","; test=($testcases); unset IFS;
for element in "${test[@]}"
do
echo element=$element
IFS=" "; elm=($element); unset IFS;
echo "Check if test case exists.."
if [ -f $buildDir/${elm[0]} ] ; then
echo "File exists"
else
echo "File "${elm[0]}" does not exist"
exit 1
fi
done

# Declare an array for storing process IDs
pids=()
mkdir -p $outputsDir
# Executing test executables
for element in "${test[@]}"
do
echo element=$element
IFS=" "; elm=($element); unset IFS;
if [ ${#elm[@]} = 3 ] ; then # check if test executable has three substrings - Eg. "o2client maxmsgs debugflags" or "o2localmutli 1 N"
  if ! [[ "${elm[2]}" =~ ^[0-9]+$ ]] ; then # last element in test executable is not a number, hence it is a debug flag
    filename=${elm[0]}
    echo numfilename=$filename
  else # last element in test executable is a number
  name=$(echo "$element" | tr -d ' ')
  echo filename=$name
  fi
$buildDir/$element > $outputsDir/$name.txt 2>&1 & # last element in test executable is a number
#add pid of background processes to the pid array, separated by commas
pids+=$!
pids+=",";

else
  if [ ${#elm[@]} = 2 ] ; then
    name=$(echo "$element" | tr -d ' ')
    echo filename=$name
    $buildDir/$element > $outputsDir/$name.txt 2>&1 &
    pids+=$!
    pids+=",";
  fi

  if [ ${#elm[@]} = 1 ] ; then
    name=${elm[0]}
    echo filename=$name
    $buildDir/${elm[0]} > $outputsDir/$name.txt 2>&1 &
    pids+=$!
    pids+=",";
  fi
fi
done

echo "Waiting.."
sleep 50
p=(${pids//,/ })
for id in "${p[@]}"
  do
    if ps -p $id > /dev/null
    then
      kill $id
    fi
  done
exit
fi # End of OS is ubuntu

# If Machine OS is Mac
if [ "$1" = "mac" ] ;
then
  export PATH=$PATH:/usr/bin/which
  export PATH=$PATH:/usr/local/bin
  echo "Mac OS X build.."

  #Check if TESTDIR exists, if not, create the structure
  TESTDIR="$HOME/o2Test"
  if  [ ! -d $TESTDIR ]
  then
    mkdir $TESTDIR
    cd $TESTDIR
    mkdir -p Outputs
    mkdir -p Build
  fi
  DIRECTORY="$TESTDIR/o2"

  cd $HOME/

  # Verifying installations for g++, cmake, git
  echo "Checking for g++ installation.."
  gcc -dumpversion | cut -f1,2,3 -d.
  if [ $? -eq 0 ]
    then
      echo "VERIFIED: g++ is installed.."
  else
    echo "ERROR: 'g++' is not installed"
    echo "Installing xcode command line tools"
    xcode-select --install
  fi

  echo "Checking for git installation..."
  gitPath=`which git`
  if [ $? -eq 0 ]
  then
    echo "VERIFIED: git is installed..."
    echo "Continuing.."
  else
    echo "ERROR: 'Git' is not installed"
    xcode-select --install
  fi

  echo "Checking for cmake installation..."
  cmakePath=$(which cmake)
  if [ $? = 0 ]
  then
    echo "VERIFIED: CMake is installed..."
    echo "Continuing.."
  export PATH=$PATH:$cmakePath
  else
    echo "ERROR: 'CMake' is not installed"
    curl https://cmake.org/files/v3.10/cmake-3.10.0-Darwin-x86_64.tar.gz -O -L cmake.tar.gz
    tar xopf cmake.tar.gz
    rm -rf cmake.tar.gz
    cd cmake
    var=`pwd`
    export PATH=$PATH:$var
  fi

  # Check if specific build version # exists
  buildDir="$TESTDIR/Build/build_"$4
  libloDir="$TESTDIR/liblo-0.28"
  outputsDir="$TESTDIR/Outputs"

  echo "Checking for the specified build version..."
  if [ -d $buildDir ]
    then
      echo "Specific build version exists.."
    else
      cd $TESTDIR
      git clone https://github.com/TejuGupta/o2.git
      cd $DIRECTORY
      git fetch --all
      git reset --hard origin/test-env
      git checkout $4
      if [ $? != 0 ] ; then
        echo "Wrong build version.."
        exit 1
      fi

      # For Liblo build, check if liblo library exists, else download it
      if [ -d $libloDir ]
      then
           echo "Liblo installation found.. Continue.."
      else
          echo "Installing liblo 0.28.."
          curl https://sourceforge.net/projects/liblo/files/liblo/0.28/liblo-0.28.tar.gz/download -L -o liblo-0.28.tar
          tar xopf liblo-0.28.tar
          rm -rf liblo-0.28.tar
          cd liblo-0.28
          ./configure --enable-static
          make
          cp src/.libs/liblo.a liblo_s64.a
      fi
      echo "Building o2.."
      cd $DIRECTORY
      # Specify build location as buildDir
      cmake -H. -B$buildDir
      if [ $? != 0 ]
      then
        echo "Error in CMake Build.."
        exit 1
      fi
      cd $buildDir
      make
      if [ $? != 0 ]
      then
        echo "Error in make.."
        exit 1
      fi
    fi
  # Clear all output files in outputsDir folder
  rm -f $outputsDir/*
  #mkdir -p $outputsDir

  # For each test executable in $testcases, check if it exists and then execute it as a background process
  IFS=","; test=($testcases); unset IFS;
  for element in "${test[@]}"
    do
      echo element=$element
      IFS=" "; elm=($element); unset IFS;
      echo "Check if test case exists.."
      if [ -f $buildDir/${elm[0]} ] ; then
        echo "File exists"
      else
        echo "File "${elm[0]}" does not exist"
        exit 1
      fi
    done

# Declare an array for storing process IDs
  pids=()
  # Executing test executables
  for element in "${test[@]}"
  do
    echo element= $element
    IFS=" "; elm=($element); unset IFS;
    if [ ${#elm[@]} = 3 ] ; then # check if test executable has three substrings - Eg. "o2client maxmsgs debugflags" or "o2localmutli 1 N"
      if ! [[ "${elm[2]}" =~ ^[0-9]+$ ]] ; then # last element in test executable is not a number, hence it is a debug flag
        name=${elm[0]}
        echo numfilename=$name
      else # last element in test executable is a number
        name=$(echo "$element" | tr -d ' ')
        echo filename=$name
      fi
    echo $name
    echo $buildDir/$element
    echo $outputsDir/$name.txt
    $buildDir/$element > $outputsDir/$name.txt 2>&1 &

    pids+=$! # last element in test executable is a number
    pids+=","; #add pid of background processes to the pid array, separated by commas

    else
      if [ ${#elm[@]} = 2 ] ; then
        name=${elm[0]}
        echo filename=$name
        $buildDir/$element > $outputsDir/$name.txt 2>&1 &
        pids+=$!
        pids+=",";
      fi

      if [ ${#elm[@]} = 1 ] ; then
        name=${elm[0]}
        echo filename=$name
        $buildDir/${elm[0]} > $outputsDir/$name.txt 2>&1 &
        pids+=$!
        pids+=",";
      fi
    fi
  done

  echo "Waiting.."
  sleep 50
  p=(${pids//,/ })
  for id in "${p[@]}"
  do
    if ps -p $id > /dev/null
    then
      kill $id
    fi
  done
  exit
fi #end of OS is Mac

# If Machine OS is windows
if [ $1 = "windows" ] ;
then
  echo "Local OS is Windows.."
  path="C:\Windows\System32\reg.exe"
  res=$(reg Query "HKLM\Hardware\Description\System\CentralProcessor\0" | grep -q 64)
    if [ $? = 0 ] ; then
      arch="64bit"
    else
      arch="32bit"
    fi
  echo $arch
  TESTDIR=$USERPROFILE/o2Test
  echo $TESTDIR
    if  [ ! -d "$TESTDIR" ] ;
    then
      mkdir "$TESTDIR"
      cd "$TESTDIR"
      mkdir Outputs
      mkdir Build
    fi
    DIRECTORY="$TESTDIR/o2"
    cd "$USERPROFILE"

  echo "Checking for g++ installation.."
  gccVersion=$(gcc -dumpversion | cut -f1,2,3 -d.)
  if [ $? -eq 0 ]
  then
    echo "VERIFIED: g++ is installed.."
  else
    echo "ERROR: 'g++' is not installed"
  fi

  echo "Checking for git installation..."
  gitVersion=$(git --version)
  #REG QUERY "HKCU\Software\GitForWindows"
  #gitPath=$($path query HKLM\Software\GitForWindows | grep -i InstallPath)
  echo $gitVersion
  if [[ $gitVersion =~ .*version.* ]]
  then
    echo "VERIFIED: git is installed..."
    echo "Continuing.."
  else
    echo "ERROR: 'Git' is not installed"
    if [ $arch = "64bit" ] ; then
      curl https://github.com/git-for-windows/git/releases/download/v2.14.3.windows.1/Git-2.14.3-64-bit.exe -L -o Git-2.14.3-64-bit.exe
      Git-2.14.3-64-bit.exe /VERYSILENT /SP-
    else
      curl https://github.com/git-for-windows/git/releases/download/v2.14.3.windows.1/Git-2.14.3-32-bit.exe -L -o Git-2.14.3-32-bit.exe
      Git-2.14.3-32-bit.exe /VERYSILENT /SP-
    fi
  fi

  echo "Checking for cmake installation.."
  cmakeVersion=$(cmake --version)
  echo $cmakeVersion
  if [[ $cmakeVersion =~ .*version.* ]]
  then
    echo "VERIFIED: CMake is installed..."
    echo "Continuing.."
  else
    echo "ERROR: 'CMake' is not installed"
    if [ $arch = 64BIT ] ; then
      curl https://cmake.org/files/v3.10/cmake-3.10.0-rc2-win64-x64.msi -L -o cmake-3.10.0-rc2-win64-x64.msi
      echo Installing CMake..
      MSIEXEC /i cmake-3.10.0-rc3-win64-x64.msi /passive
      export PATH=$PATH:"C:\Program Files\CMake\bin"
      echo Checking CMake installation...
      cmake --version
    else
      curl https://cmake.org/files/v3.10/cmake-3.10.0-rc3-win32-x86.msi -L -o cmake-3.10.0-rc3-win32-x86.msi
      echo Installing CMake..
      MSIEXEC /i cmake-3.10.0-rc3-win32-x86.msi /passive
      export PATH=$PATH:"C:\Program Files\CMake\bin"
      echo Checking CMake installation...
      cmake --version
    fi
fi

  echo "Checking for Visual Studio installation.."
  reg Query "HKLM\Software\Microsoft\VisualStudio\14.0"
  if [ $? = 0 ] ; then
    echo "Visual Studio 15 2017 version detected.."
    echo Continuing..
  else
    echo "Visual Studio 15 2017 not present.."
    curl https://aka.ms/vs/15/release/vs_enterprise.exe -L -o vs_enterprise.exe
    vs_enterprise.exe -q --includeRecommended
  fi

  DIRECTORY="$TESTDIR/o2"
  buildDir="$TESTDIR/Build/build_"$4
  debugDir="$buildDir/Debug"
  libloDir="$TESTDIR/liblo-0.28"
  outputsDir="$TESTDIR/Outputs"

  # Check if specific build version # exists
  echo "Checking for the specified build version..."
  if [ -d "$buildDir" ] ; then
    echo "Specific build version exists.."
  else
    cd "$TESTDIR"
    git clone https://github.com/TejuGupta/o2.git
    cd "$DIRECTORY"
    git fetch --all
    git reset --hard origin/test-env
    git checkout $4
    if [ $? != 0 ] ; then
      echo "Wrong build version.."
      exit 1
    fi
    echo "Building o2.."
    cd "$DIRECTORY"
    # Specify build location as buildDir
    cmake -H. -B"$buildDir"
    if [ $? != 0 ]
    then
      echo "Error in CMake Build.."
      exit 1
    fi

    buildPath="C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\MSBuild\15.0\Bin"
    export PATH=$PATH:$buildPath
    #MSBuild.exe
    #echo $PATH
    cd "$buildDir"
    MSBuild.exe ALL_BUILD.vcxproj
    if [ $? != 0 ]
    then
      echo "Error in make.."
      exit 1
    fi
  fi
  # Clear all output files in outputsDir folder
  rm -f "$outputsDir"/*

  # For each test executable in $testcases, check if it exists and then execute it as a background process
  IFS=","; test=($testcases); unset IFS;
  for element in "${test[@]}"
  do
    echo element=$element
    IFS=" "; elm=($element); unset IFS;
    echo "Check if test case exists.."
    if [ -f "$debugDir"/${elm[0]} ] ; then
      echo "File exists"
    else
      echo "File "${elm[0]}" does not exist"
      exit 1
    fi
  done

  # Declare an array for storing process IDs
  pids=()

  # Executing test executables
  for element in "${test[@]}"
  do
    echo element= $element
    IFS=" "; elm=($element); unset IFS;
    if [ ${#elm[@]} = 3 ] ; then # check if test executable has three substrings - Eg. "o2client maxmsgs debugflags" or "o2localmutli 1 N"
      if ! [[ "${elm[2]}" =~ ^[0-9]+$ ]] ; then # last element in test executable is not a number, hence it is a debug flag
        name=${elm[0]}
        echo numfilename=$name
      else # last element in test executable is a number
        name=$(echo -e "$element" | tr -d ' ')
        echo filename=$name
      fi
      "$debugDir"/$element.exe > "$outputsDir"/$name.txt 2>&1 &
      pid2=$!
      #echo pid2=$pid2
      pid=`ps -W | grep -i $name`
      #echo pid=$pid
      pids+=$pid
      pids+=",";

    else
      if [ ${#elm[@]} = 2 ] ; then
        name=$(echo -e "$element" | tr -d ' ')
        echo filename=$name
        "$debugDir"/$element.exe > "$outputsDir"/$name.txt 2>&1 &
        pid2=$!
        #echo pid2=$pid2
        pid=`ps -W | grep -i $name`
        #echo pid=$pid
        pids+=$pid
        pids+=",";
      fi

      if [ ${#elm[@]} = 1 ] ; then
        name=${elm[0]}
        echo filename=$name
        "$debugDir"/${elm[0]}.exe > "$outputsDir"/$name.txt 2>&1 &
        pid2=$!
      #  echo pid2=$pid2
        pid=`ps -W | grep -i $name`
      #  echo pid=$pid
        pids+=$pid
        pids+=",";
      fi
    fi
  done

  echo "Waiting.."
  sleep 50
  p=(${pids//,/ })
  for id in "${p[@]}"
  do
    echo $id
    /bin/kill -f $id
    #TASKKILL /PID $id /T /F
  done
  exit 0
fi
echo "Exiting..."
exit 0
