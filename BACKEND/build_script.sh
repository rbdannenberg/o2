#!/bin/bash
if [ "$1" = "ubuntu" ]
then
DIRECTORY="/home/osboxes/o2"
cd ~
echo "Checking for git installation..."
apt-cache policy git | grep -q none
if [ $? -eq 0 ]
then
logger -s "ERROR: 'Git' is not installed"
sudo apt-get -y upgrade
sudo apt-get install -y git
else
echo "VERIFIED: git is installed..."
echo "Continuing.."
fi

echo "Checking for cmake installation..."
apt-cache policy cmake | grep -q none
if [ $? -eq 0 ]
then
echo "ERROR: 'CMake' is not installed"
sudo apt-get -y upgrade
sudo apt-get install -y cmake
else
echo "VERIFIED: CMake is installed..."
echo "Continuing.."
fi

if [ -d "$DIRECTORY" ];
then
echo "Directory exists.."
rm -rf $DIRECTORY
echo "Cloning O2 from Git.."
git clone https://github.com/rbdannenberg/o2.git
echo "Building O2.."
cd $DIRECTORY
cmake -H. -Bbuild
cmake --build build -- -j3
cd build
make

else
echo "Cloning O2 from Git.."
git clone https://github.com/rbdannenberg/o2.git
echo "Building O2.."
cd $DIRECTORY/
cmake -H. -Bbuild
cmake --build build -- -j3
cd build
make
fi
wait
echo "Executing test case.."
testcases="$2"
test=(${testcases//,/ })
for element in "${test[@]}"
do
	echo $element
	   if [ -f $element ];
	then
	./$element > $DIRECTORY/$element.txt
	if grep -Fxq "DONE" $DIRECTORY/$element.txt
	    then
		echo "PASS"
	fi
	echo "Output successful.."
	cat $DIRECTORY/$element.txt
	else
	echo "Invalid test case input, Please enter valid test case name.."
	fi
done
fi

