#!/bin/bash
if [ "$1" = "ubuntu" ]
then
	DIRECTORY="$HOME/try1/o2"
	cd $HOME/try1
	sudo -S <<< $3 apt-get -yy update
	sudo -S <<< $3 apt-get install -yy g++
	sudo -S <<< $3 apt-get install -yy build-essential
	echo "Checking for git installation..."
	sudo -S <<< $3 apt-get install -yy libasound2-dev
	apt-cache policy git | grep -q none
	if [ $? -eq 0 ]
		then
			logger -s "ERROR: 'Git' is not installed"
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

	if [ -d "$DIRECTORY" ];
		then
			echo "Directory exists.."
			rm -rf $DIRECTORY
			echo "Cloning O2 from Git.."
			git clone https://github.com/TejuGupta/o2.git
			cd $DIRECTORY
			echo "Building O2.."
			echo "Downloading liblo packages..."
			wget -O liblo-0.28 https://sourceforge.net/projects/liblo/files/liblo/0.28/liblo-0.28.tar.gz/download
			tar -xvf liblo-0.28
			cd liblo-0.28
			./configure --enable-static
			make
			cp src/.libs/liblo.a liblo_s64.a
			echo "Liblo configuring done.."
			cd $DIRECTORY
			cmake -H. -Bbuild
			cd build
			make

		else
			echo "Cloning O2 from Git.."
			git clone https://github.com/TejuGupta/o2.git
			cd $DIRECTORY/
			echo "Downloading liblo packages..."
			wget -O liblo-0.28 https://sourceforge.net/projects/liblo/files/liblo/0.28/liblo-0.28.tar.gz/download
			tar -xvf liblo-0.28
			cd liblo-0.28
			./configure --enable-static
			make
			cp src/.libs/liblo.a liblo_s64.a
			echo "Liblo configuring done.."
			echo "Building O2.."
			cd $DIRECTORY/
			cmake -H. -Bbuild
			cd build
			make
	fi

	echo "Executing test case.."
	testcases="$2"
	IFS=","; test=($testcases); unset IFS;
	pids=()
	for element in "${test[@]}"
	do
		 if [ -f $element ]; then
		     echo $element
				 ./$element > $DIRECTORY/$element.txt 2>&1 &
				 pids+=$!
				 pids+=",";
			 	 echo "Output successful.."
		 else
			   echo "Invalid test case input, Please enter valid test case name.."
		 fi
	done
	sleep 60
	pids=(${pids//,/ })
	echo "Terminating background processes.."
	for pid in "${pids[@]}"
	do
		echo $pid
		kill $pid
	done
fi
exit
