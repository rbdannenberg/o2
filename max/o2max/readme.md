## How to Buikd and Use

Put these files of O2 in the ```o2max/o2``` directory, or add the path of them to CMakeLists: ```hostip.h, o2.dll, o2.h, o2.lib, o2base.h```

Use [Max SDK](https://github.com/Cycling74/max-sdk) and CMake to build the external.

Place this folder in the ```max-sdk/source``` directory, and add ```add_subdirectory(source/o2max)``` to CMakeLists.txt in the ```max-sdk``` directory. Then follow the instruction of max-sdk on github to build the external.

To use the external, you need to add the path of ```o2.dll``` (for Windows) to a place that is accessible for Max's executable file, such as system environment variable, or the folder that max.exe is in.