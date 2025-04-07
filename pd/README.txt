README.txt for o2/pd
Roger B. Dannenberg
March 2025

Notes on building and using:

My build procedure:
1. Use CMake to build project
2. Depends on ../build/Release/o2.lib and ../build/Debug/o2.lib
3. Build the Release version using BuildAll in Xcode
4. source install_macos.sh -- this writes Release libraries to
   ~/Documents/Pd/externals and uses install_name_tool to patch
   libraries to refer to the right library.
5. Run Pd-0.54.1.app
6. Open amo2.pd
7. Clicking the status message will print to console: O2_FAIL
8. For simple test, you can run serpent64 o2mon. (o2util.srp is
    in serpent/programs/o2util.srp)
   - Use command f <flags> if you want to set debug flags, e.g.
     "odr" will trace low-level socket operations, discovery
     protocol and non-system receive messages (e.g. from Pd).
   - Use "r am-ens am" to receive messages from amo2.pd (will
     print messages for am even if you do not set the "r" flag).
   - Now clicking the status message should print 

