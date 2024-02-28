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

option(USE_STATIC_LIBS "Build with static libs?" TRUE)

add_definitions("-DO2_NO_O2DISCOVERY")

set(O2HOST_SRC src/o2host.cpp src/o2host.h
    src/fieldentry.cpp src/fieldentry.h
    src/configuration.cpp src/configuration.h
    src/confread.cpp src/confread.h
    src/midiservice.cpp src/midiservice.h 
    src/o2oscservice.cpp src/o2oscservice.h 
    )

# Where to find portmidi
option(USE_MIDI "Enable midi message I/O" TRUE)
set(PM_INCL "../../portmidi/pm_common" CACHE PATH
            "Main PortMidi include directory") 
set(PM_OPT_LIB
    "${CMAKE_CURRENT_SOURCE_DIR}/../../portmidi/Release/libportmidi_static.a"
    CACHE FILEPATH "PortMidi library - optimized version") 
set(PM_DBG_LIB
    "${CMAKE_CURRENT_SOURCE_DIR}/../../portmidi/Debug/libportmidi_static.a"
    CACHE FILEPATH "PortMidi library - debug version")


if(APPLE) 
  set(CURSES_LIB curses)
  set(O2_LIB o2_static)
  set(CMAKE_CXX_FLAGS "-std=c++11")
  find_library(COREAUDIO CoreAudio)
  set(EXTRA_LIBS "${COREAUDIO}")
else()
  set(CURSES_LIB ncurses)
endif() 

add_executable(o2host ${O2HOST_SRC})  
 
target_include_directories(o2host PRIVATE ../src ${BONJOUR_INCLUDE_PATH})
target_link_directories(o2host PRIVATE ..)

if(USE_MIDI)
  find_library(COREFOUNDATION_LIBRARY CoreFoundation REQUIRED) 
  find_library(COREMIDI_LIBRARY CoreMIDI REQUIRED) 
  find_library(CORESERVICES_LIBRARY CoreServices REQUIRED) 
  LIST(APPEND EXTRA_LIBS ${EXTRA_LIBS} ${COREAUDIO_LIBRARY}
         ${COREFOUNDATION_LIBRARY} ${COREMIDI_LIBRARY} ${CORESERVICES_LIBRARY})
  target_include_directories(o2host PRIVATE ${PM_INCL})
  target_link_libraries(o2host PRIVATE debug ${PM_DBG_LIB}
                                       optimized ${PM_OPT_LIB})
  if(UNIX AND NOT APPLE)
    target_link_libraries(srplib PUBLIC -lasound ${PTHREAD_LIB})
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
