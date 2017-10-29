REM reg.exe ADD HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System /v EnableLUA /t REG_DWORD /d 0 /f
@echo off
set ip=%1
set os=%2
set username=%3
set password=%4
set testcases=%5
echo %ip%
echo %username%
echo %password%
echo %testcases%
reg Query "HKLM\Hardware\Description\System\CentralProcessor\0" | find /i "x86" > NUL && set OS=32BIT || set OS=64BIT 
if %OS%==32BIT echo This is a 32bit operating system 
if %OS%==64BIT echo This is a 64bit operating system
setlocal ENABLEEXTENSIONS
set prog=C:\Users\Aparrnaa Raghuraman\Downloads\Git-2.14.3-64-bit.exe
echo Checking for git installation...
set GITKEY=HKEY_LOCAL_MACHINE\Software\GitForWindows
set V=CurrentVersion
set version=none
for /f "tokens=3* delims= " %%k in ('reg query "%GITKEY%" /v %V% ^| findstr "%V%"') do set version=%%k

if NOT %version% == none (
ECHO Git is installed...
ECHO Git version: %version%
)
if %version% == none (
ECHO Git is not installed...
ECHO Installing Git...
ECHO Please wait..
if %OS% == 32BIT (
powershell -Command "(New-Object Net.WebClient).DownloadFile('https://github.com/git-for-windows/git/releases/download/v2.14.3.windows.1/Git-2.14.3-32-bit.exe', 'Git-2.14.3-32-bit.exe')"
Git-2.14.3-32-bit.exe /VERYSILENT /SP-
)
if %OS% == 64BIT (
powershell -Command "(New-Object Net.WebClient).DownloadFile('https://github.com/git-for-windows/git/releases/download/v2.14.3.windows.1/Git-2.14.3-64-bit.exe', 'Git-2.14.3-64-bit.exe')"
Git-2.14.3-64-bit.exe /VERYSILENT /SP-
)
echo Done..
)
echo Checking for CMake Installation...
cmake --version > find /i "Install" > NUL && set CMAKEFOUND=yes || set CMAKEFOUND=no
if %CMAKEFOUND% == yes (
echo CMake found
cmake --version
)

if %CMAKEFOUND% == no (
echo CMake not found..
echo Downloading CMake..
if %OS% == 64BIT (
powershell -Command "(New-Object Net.WebClient).DownloadFile(' https://cmake.org/files/v3.10/cmake-3.10.0-rc2-win64-x64.msi', 'cmake-3.10.0-rc2-win64-x64.msi')"
echo Installing CMake..
MSIEXEC /i cmake-3.10.0-rc3-win64-x64.msi /passive
setx PATH "C:\Program Files\CMake\bin" /m
echo %PATH%
echo Checking CMake installation...
cmake --version
)
if %OS% == 32BIT (
powershell -Command "(New-Object Net.WebClient).DownloadFile('https://cmake.org/files/v3.10/cmake-3.10.0-rc3-win32-x86.msi', 'cmake-3.10.0-rc3-win32-x86.msi')"
echo Installing CMake..
MSIEXEC /i cmake-3.10.0-rc3-win32-x86.msi /passive 
setx PATH "%PATH%;C:\Program Files\CMake\bin" /m
if "%PATH:~-1%"=="\" ( SETX PATH "%PATH%\" ) else ( SETX PATH "%PATH%" )
echo Checking CMake installation...
cmake --version
)
)
echo Checking for Visual Studio Installation..
reg Query "HKLM\SOFTWARE\VSTUDIO" | find /i "Status" > NUL && set VSFOUND=yes || set VSFOUND=no 
echo %VSFOUND%
if %VSFOUND% == yes (
echo Visual Studio installation found..
echo Continuing..
)

if %VSFOUND% == no (
echo Downloading Visual Studio 17..
powershell -Command "(New-Object Net.WebClient).DownloadFile('https://aka.ms/vs/15/release/vs_enterprise.exe', 'vs_enterprise.exe')"
vs_Enterprise.exe -q --includeRecommended
echo Installing Visual C++ Build Tools..
powershell -Command "(New-Object Net.WebClient).DownloadFile('http://go.microsoft.com/fwlink/?LinkId=691126&__hstc=268264337.036b1f506d1033a2f19266caa1b74b52.1508992021548.1508992021548.1508992021548.1&__hssc=268264337.3.1508992021548&__hsfp=4252074785&fixForIE=.exe', 'vsbuildtools.exe')"
vsbuildtools.exe /Full /Quiet
echo Adding registry key to detect during future runs...
reg add HKLM\SOFTWARE\VSTUDIO /f /v Status /t REG_DWORD /d 1
echo Registry key added...
echo Updating Path for MSBuild..
setx PATH "%PATH%;C:\Program Files (x86)\Microsoft Visual Studio\2017\Enterprise\MSBuild\15.0\Bin" /m
echo Installation done...
)

echo Cloning into o2..
rmdir /Q /S o2
git clone https://github.com/rbdannenberg/o2.git
cmake o2
