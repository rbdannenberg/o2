#!/bin/bash
home = $HOME
DIRECTORY = $home/o2
cd home
logger -s "Checking for git installation.."
if pkgutil --pkgs=.\+git.\+ > grep git ; then
  echo "VERIFIED: Git is installed.."
else
  echo "ERROR: 'Git' is not installed"
fi
logger -s "Checking for CMake installation.."
if open -Ra cmake ; then
  echo "VERIFIED: 'CMake' is installed.."
else
  echo "ERROR: 'CMake' is not installed.."
fi

if [ -d "$DIRECTORY" ];
then
logger -s "Directory exists.."
rm -rf $DIRECTORY
logger -s "Cloning O2 from Git.."
git clone https://github.com/rbdannenberg/o2.git
logger -s "Building O2.."
cd $DIRECTORY
cmake -H. -Bbuild
cmake --build build -- -j3
cd build
make

else
logger -s "Cloning O2 from Git.."
git clone https://github.com/rbdannenberg/o2.git
logger -s "Building O2.."
cd $DIRECTORY
cmake -H. -Bbuild
cmake --build build -- -j3
cd build
make
fi

logger -s "Executing test case.."
if [ -f "$2" ];
then
./$2 > $DIRECTORY/$2.txt 2>&1
else
logger -s "Invalid test case input, Please enter valid test case name.."
fi

fi
