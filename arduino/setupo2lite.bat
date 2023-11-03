rem setupo2lite.bat -- batch file to copy o2/src files to Arduino development
rem    directory for ESP32.
rem
rem Roger B. Dannenberg
rem Jan 2022
rem
echo Usage: setupo2lite path-to-Arduino-project-sources
echo Run this bat file in this directory (arduino).

copy ..\src\o2lite.c %1
copy ..\src\o2lite.h %1
copy ..\src\o2base.h %1
copy ..\src\hostipimpl.h %1
copy ..\src\hostip.h %1
copy ..\src\o2liteesp32.cpp %1

echo "Be sure to add (tabs) for o2lite.c and o2liteesp32.cpp"
