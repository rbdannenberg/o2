# CMakeLists.txt - for o2host
#
# Roger B. Dannenberg
# Feb 2024

cmake_minimum_required(VERSION 3.24)
project(o2host)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})

set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING
              "Semicolon-separated list of supported configuration types"
              FORCE)

add_definitions("-DO2_NO_O2DISCOVERY")

set(O2HOST_SRC src/o2host.cpp src/o2host.h
    src/fieldentry.cpp src/fieldentry.h
    src/configuration.cpp src/configuration.h
    src/confread.cpp src/confread.h
    src/midiservice.cpp src/midiservice.h 
    src/o2oscservice.cpp src/o2oscservice.h 
    )

add_executable(o2host ${O2HOST_SRC})  
 
# Where to find portmidi
option(USE_MIDI "Enable midi message I/O" TRUE)
if(EXISTS ${CMAKE_SOURCE_DIR}/libraries.txt)
  message(STATUS "Loading libraries.txt")
  include(libraries.txt) 
else()
  message(ERROR
    " You MUST create libraries.txt. See libraries-example.txt for details.")  
endif()

set(O2_LIB o2_static)
if(APPLE) 
  set(CURSES_LIB curses)
  set(CMAKE_CXX_FLAGS "-std=c++11")
  find_library(COREAUDIO CoreAudio)
  set(EXTRA_LIBS "${COREAUDIO}")
  option(BUILD_APPLICATION "Build application or debug command line executable?"
         TRUE)
  if(BUILD_APPLICATION)
    set(MACOSX_BUNDLE_ICON_FILE "o2host.icns")
    if(EXISTS "${CMAKE_SOURCE_DIR}/${MACOSX_BUNDLE_ICON_FILE}")
      set(ICON_FULL_PATH "${CMAKE_SOURCE_DIR}/${MACOSX_BUNDLE_ICON_FILE}")
    else()
      set(ICON_FULL_PATH 
          "${CMAKE_CURRENT_SOURCE_DIR}/${MACOSX_BUNDLE_ICON_FILE}")
    endif() 
    message(STATUS "after icon ${ICON_FULL_PATH}") 
    set(MACOSX_BUNDLE_INFO_STRING "o2host - Version 1.0") 
    set(MACOSX_BUNDLE_BUNDLE_VERSION 1.0) 
    set(MACOSX_BUNDLE_BUNDLE_NAME o2host) 
    # if you call this MAXOSX_BUNDLE_EXECUTABLE_NAME, it will not work!: 
    set(BUNDLE_EXECUTABLE_NAME o2host.sh) 
    target_sources(o2host PUBLIC ${ICON_FULL_PATH}) 
    set_target_properties(o2host PROPERTIES 
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/Info.plist.in 
        MACOSX_BUNDLE TRUE 
        MACOSX_BUNDLE_ICON_FILE ${MACOSX_BUNDLE_ICON_FILE}
        RESOURCE ${ICON_FULL_PATH}) 
    # after building o2host.app, CMake will install only 
    # Contents/MacOS/o2host, but we want to run o2host in Terminal, so 
    # we created Info.plist to run o2host.sh. Now that CMake's "normal"
    # build is complete, we to fix o2host.app by installing o2host.sh: 
    add_custom_command(TARGET o2host POST_BUILD 
      COMMAND echo "running fix_o2host_app from target file dir $<TARGET_FILE_DIR:o2host>"
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/fix_o2host_app.sh 
      WORKING_DIRECTORY $<TARGET_FILE_DIR:o2host>
      COMMENT "Adding o2host.sh to o2host.app") 
  endif() 
  # could not get CMake builds to work unless I turn off code signing: 
  set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO")  
else() 
  set(CURSES_BASE ncurses CACHE PATH 
      "Where to find lib and include files for ncurses") 
  set(CURSES_LIB "${CURSES_BASE}/lib/pdcurses.lib") 
  set(BONJOUR_LIB_DIR "C:/Program Files/Bonjour SDK/Lib/x64" Cache 
      PATH "where to find dnssd.lib") 
  target_link_directories(o2host PRIVATE ${BONJOUR_LIB_DIR}) 
  target_include_directories(o2host PRIVATE "${CURSES_BASE}/include") 
  target_link_libraries(o2host PRIVATE 
      winmm.lib ws2_32.lib Iphlpapi.lib dnssd.lib) 
  # always static
  set_property(TARGET o2host PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)
endif() 


target_include_directories(o2host PRIVATE ../src ${BONJOUR_INCLUDE_PATH})
target_link_directories(o2host PRIVATE ..)

if(USE_MIDI)
  target_include_directories(o2host PRIVATE ${PM_INCL})
  target_link_libraries(o2host PRIVATE debug ${PM_DBG_LIB}
                                       optimized ${PM_OPT_LIB})
  if(APPLE) 
    find_library(COREFOUNDATION_LIBRARY CoreFoundation REQUIRED) 
    find_library(COREMIDI_LIBRARY CoreMIDI REQUIRED) 
    find_library(CORESERVICES_LIBRARY CoreServices REQUIRED) 
    LIST(APPEND EXTRA_LIBS ${EXTRA_LIBS} ${COREAUDIO_LIBRARY}
         ${COREFOUNDATION_LIBRARY} ${COREMIDI_LIBRARY} ${CORESERVICES_LIBRARY})
  elseif(UNIX)
    target_link_libraries(o2host PUBLIC -lasound ${PTHREAD_LIB})
  endif()
else()
  add_definitions("-DUSE_MIDI=0")
endif()

message(STATUS "libraries ${O2_LIB} ${CURSES_LIB} ${EXTRA_LIBS}")
target_link_libraries(o2host PRIVATE ${O2_LIB} ${CURSES_LIB} ${EXTRA_LIBS})

# if(UNIX AND NOT APPLE)  # linux
#   target_link_libraries(o2host PRIVATE
#                         avahi-client avahi-common atomic rt)
# endif()

