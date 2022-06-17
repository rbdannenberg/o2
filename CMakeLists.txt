cmake_minimum_required(VERSION 3.20.0)
cmake_policy(SET CMP0091 NEW)
project(o2)

option(CMAKE_BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(TESTS_BUILD "Compile regression and other tests" ON)

option(TESTS_WITH_LIBLO "Compile tests that use liblo, 
requiring liblo library (only enabled if TESTS_BUILD is ON)" OFF)

option(TESTS_MIDI_EXAMPLE "Compile midiclient & midiserver, 
requiring portmidi library" OFF)

option(BUILD_WITH_DEBUG_SUPPORT 
       "Allow debug flags to control printing info" ON)
option(BUILD_WITH_PATTERN_SUPPORT "Allow pattern matching" ON)
option(BUILD_WITH_OSC_SUPPORT "Include OSC support" ON)
option(BUILD_WITH_HUB_SUPPORT "Include o2_hub() discovery" ON)
option(BUILD_WITH_BUNDLE_SUPPORT "Allow message bundles" ON)
option(BUILD_WITH_BRIDGE_SUPPORT "Allow other transport add-ons" ON)
option(BUILD_WITH_WEBSOCKET_SUPPORT "Support O2 over websockets" ON)
option(BUILD_WITH_SHAREDMEM_SUPPORT 
       "Include shared memory bridge API, requires bridge support" ON)
option(BUILD_WITH_MESSAGE_PRINT
"Provide o2_message_print even in non-debug builds (it is always
provided in debug builds)" ON)

option(BUILD_WITH_O2LITE_DISCOVERY
       "Added code to o2lite clients for faster discovery" ON)
option(BUILD_WITH_O2LITE_CLOCKSYNC
       "Added code to o2lite clients to synchronize clock with  O2" ON)
set(BUILD_WITH_O2MEM_DEBUG "0" CACHE STRING
    "0=off, 1=high space overhead, 2=high space and runtime overhead")


# O2 intentionally writes outside of declared array bounds (and
#  carefully insures that space is allocated beyond array bounds,
#  especially for message data, which is declared char[4], but can
#  be arbitrariily long -- well, at least up to O2_MAX_MSG_SIZE)
# Newer compilers will try to enforce char[4] in memcpy and strcpy
#  unless we turn off this behavior with the following macro definition:
add_definitions("-D_FORTIFY_SOURCE=0")
# add_definitions("-DO2_NO_ZEROCONF") 
add_definitions("-DO2_NO_O2DISCOVERY") 

add_definitions("-DO2MEM_DEBUG=${BUILD_WITH_O2MEM_DEBUG}")

if(BUILD_WITH_DEBUG_SUPPORT) 
else(BUILD_WITH_DEBUG_SUPPORT) 
  add_definitions("-DO2_NO_DEBUG") 
endif(BUILD_WITH_DEBUG_SUPPORT) 

if(BUILD_WITH_PATTERN_SUPPORT) 
else(BUILD_WITH_PATTERN_SUPPORT) 
  add_definitions("-DO2_NO_PATTERNS") 
endif(BUILD_WITH_PATTERN_SUPPORT) 

if(BUILD_WITH_OSC_SUPPORT)
else(BUILD_WITH_OSC_SUPPORT)
  add_definitions("-DO2_NO_OSC")
endif(BUILD_WITH_OSC_SUPPORT)

if(BUILD_WITH_HUB_SUPPORT)
else(BUILD_WITH_HUB_SUPPORT)
  add_definitions("-DO2_NO_HUB")
endif(BUILD_WITH_HUB_SUPPORT)

if(BUILD_WITH_BUNDLE_SUPPORT)
else(BUILD_WITH_BUNDLE_SUPPORT)
  add_definitions("-DO2_NO_BUNDLES")
endif(BUILD_WITH_BUNDLE_SUPPORT)

if (BUILD_WITH_WEBSOCKET_SUPPORT)  
  set(BUILD_WITH_BRIDGE_SUPPORT ON CACHE BOOL 
      "Allow other transport add-ons" FORCE) 
  message(STATUS 
    "Forcing BUILD_WITH_BRIDGE_SUPPORT needed by BUILD_WITH_WEBSOCKET_SUPPORT")
else (BUILD_WITH_WEBSOCKET_SUPPORT)  
  add_definitions("-DO2_NO_WEBSOCKETS")  
endif (BUILD_WITH_WEBSOCKET_SUPPORT)  

if(BUILD_WITH_BRIDGE_SUPPORT)  
else(BUILD_WITH_BRIDGE_SUPPORT)  
  add_definitions("-DO2_NO_BRIDGES")  
endif(BUILD_WITH_BRIDGE_SUPPORT)  
 
if(BUILD_WITH_SHAREDMEM_SUPPORT)
else(BUILD_WITH_SHAREDMEM_SUPPORT)
  add_definitions("-DO2_NO_SHAREDMEM")
endif(BUILD_WITH_SHAREDMEM_SUPPORT)

if(BUILD_WITH_MESSAGE_PRINT)
else(BUILD_WITH_MESSAGE_PRINT)
  add_definitions("-DO2_MSGPRINT")
endif(BUILD_WITH_MESSAGE_PRINT)

if(WIN32)
  add_definitions("-D_CRT_SECURE_NO_WARNINGS -D_WINSOCK_DEPRECATED_NO_WARNINGS -DIS_BIG_ENDIAN=0")
  set(BONJOUR_INCLUDE_PATH "C:/Program Files/Bonjour SDK/Include" CACHE
      PATH "where to find dns_sd.h")
  set(BONJOUR_LIB_DIR "C:/Program Files/Bonjour SDK/Lib/x64" CACHE
      PATH "where to find dnssd.lib")
  link_directories(${BONJOUR_LIB_DIR})  
  set(EXTRA_LIBS winmm.lib ws2_32.lib Iphlpapi.lib dnssd.lib)
endif(WIN32)

if(UNIX)
  if(APPLE)
    set(FRAMEWORK_PATH ${CMAKE_OSX_SYSROOT}/System/Library/Frameworks)
    set(EXTRA_LIBS "${FRAMEWORK_PATH}/CoreAudio.framework")
  else(APPLE) # must be Linux
# struct addrinfo not defined by default, so fix it here:
    add_definitions("-D_POSIX_C_SOURCE=201112L")
    set(EXTRA_LIBS m atomic avahi-client avahi-common rt) # needed by liblo
#   FOR DEBUGGING, I COMPILED MY OWN LOCAL LIBRARY:
#    link_directories(BEFORE avahi-0.7/avahi-common/.libs
#                     avahi-0.7/avahi-core/.libs
#                     avahi-0.7/avahi-utils/.libs
#                     avahi-0.7/avahi-client/.libs)
  endif(APPLE)
endif(UNIX)

# o2

set(O2_SRC
  src/o2atomic.cpp src/o2atomic.h
  src/o2base.h
  src/bridge.cpp src/bridge.h
  src/clock.cpp src/clock.h
  src/debug.cpp src/debug.h
  src/discovery.cpp src/discovery.h
  src/hostip.c src/hostip.h src/hostipimpl.h
  src/vec.cpp src/vec.h
  src/dynarray.cpp src/dynarray.h
  src/o2node.cpp src/o2node.h
  # src/hashnode.cpp src/hashnode.h
  src/message.cpp src/message.h
  src/mqtt.cpp src/mqtt.h
  src/mqttcomm.cpp src/mqttcomm.h
  src/msgsend.cpp src/msgsend.h
  src/msgprint.cpp
  src/o2network.cpp src/o2network.h 
  src/websock.cpp src/websock.h
  src/o2sha1.cpp src/o2sha1.h
  src/o2zcdisc.cpp
  src/o2.cpp src/o2.h src/o2base.h src/o2internal.h
  src/o2osc.cpp src/o2osc.h
  src/pathtree.cpp src/pathtree.h
  src/processes.cpp src/processes.h
  src/properties.cpp src/properties.h
  src/o2sched.cpp src/o2sched.h
  src/o2sleep.c
  src/services.cpp src/services.h
  src/sharedmem.cpp src/sharedmem.h
  src/o2mem.cpp src/o2mem.h
  src/o2obj.h
  src/sharedmemclient.h
  src/stun.cpp src/stun.h
  )

# library name default is o2_static if BUILD_SHARED_LIBS is off
# because Xcode strips off ".a" from "libX.a" and links to
# "libX.dylib", making it harder to achieve static linking or
# even specifying the desired version (consider an old and even
# unknown libo2.dylib installed by someone else getting linked 
# instead of a new static libo2.a that was intended.
set(O2_STATIC_LIBRARY_NAME "o2_static" CACHE STRING 
    "When you build static libraries, what is the o2 library name?")
set(O2LITE_STATIC_LIBRARY_NAME "o2lite_static" CACHE STRING 
    "When you build static libraries, what is the o2lite library name?")
if(BUILD_SHARED_LIBS)
  set(O2 "o2")
  set(O2LITE "o2lite")
else() 
  set(O2 ${O2_STATIC_LIBRARY_NAME})
  set(O2LITE ${O2LITE_STATIC_LIBRARY_NAME})
endif() 

add_library(o2 ${O2_SRC})
# O2MEM_DEBUG uses mutex from pthreads except for Windows:
if(${BUILD_WITH_O2MEM_DEBUG} GREATER 0 AND NOT WIN32)
  target_link_libraries(o2 PRIVATE pthread)
endif()
target_include_directories(o2 PRIVATE ${BONJOUR_INCLUDE_PATH})  
 set_target_properties(o2 PROPERTIES
    OUTPUT_NAME ${O2}
    MSVC_RUNTIME_LIBRARY MultiThreaded
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/src") 

# o2lite

set(O2LITE_SRC
  src/o2lite.c src/o2lite.h
  src/o2sleep.c
# src/hostip.c src/hostip.h -- included in o2lite.c
# These are various implementations of the o2ldisc API.
# It is OK to include them all since conditionals will select one:
  src/o2liteo2disc.c src/o2liteavahi.c src/o2litebonjour.c
  )

add_library(o2lite ${O2LITE_SRC})
target_include_directories(o2lite PRIVATE ${BONJOUR_INCLUDE_PATH})
set_target_properties(o2lite PROPERTIES
    OUTPUT_NAME ${O2LITE}
    MSVC_RUNTIME_LIBRARY MultiThreaded
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/src") 

message(STATUS "CMAKE_SOURCE_DIR ${CMAKE_SOURCE_DIR}")

set_target_properties(o2 PROPERTIES C_VISIBILITY_PRESET hidden)

if(BUILD_WITH_O2LITE_DISCOVERY)
else(BUILD_WITH_O2LITE_DISCOVERY)
  target_compile_definitions(o2lite PRIVATE O2L_NO_DISCOVERY O2LDEBUG=1)
endif(BUILD_WITH_O2LITE_DISCOVERY)

if(BUILD_WITH_O2LITE_CLOCKSYNC)
else(BUILD_WITH_O2LITE_CLOCKSYNC)
  target_compile_definitions(o2lite PRIVATE O2L_NO_CLOCKSYNC)
endif(BUILD_WITH_O2LITE_CLOCKSYNC)


#######################
# BUILD CONFIGURATION #
#######################

target_link_libraries(o2 PUBLIC ${EXTRA_LIBS})  
set(LIBRARIES "o2 ${EXTRA_LIBS}" CACHE INTERNAL "")

target_link_libraries(o2lite PUBLIC ${EXTRA_LIBS})
set(O2LITE_LIBS "o2lite ${EXTRA_LIBS}" CACHE INTERNAL "")

if(TESTS_BUILD)
message(STATUS "Building test programs") 

# EXECUTABLE
# Create executables

if(WIN32)
  # non-o2 code to check out non-blocking sockets on windows
  add_executable(winnbserver test/winnbserver.cpp)
  target_link_libraries(winnbserver "${EXTRA_LIBS}")
  add_executable(winnbclient test/winnbclient.cpp)
  target_link_libraries(winnbclient "${EXTRA_LIBS}")
endif(WIN32)

function(o2testprogram name) # extralibs is optional
  add_executable(${name} test/${name}.cpp)
  target_include_directories(${name} PRIVATE src ${BONJOUR_INCLUDE_PATH})
  target_link_libraries(${name} PRIVATE o2)
  if(NOT ("${ARGV1}" STREQUAL ""))
    target_link_libraries(${name} PRIVATE ${ARGV1})
  endif(NOT ("${ARGV1}" STREQUAL ""))
  set_property(TARGET ${name} PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)
endfunction()

function(o2litetestprogram name ext)
  add_executable(${name} test/${name}.${ext})
  target_include_directories(${name} PRIVATE src ${BONJOUR_INCLUDE_PATH})
  target_link_libraries(${name} PRIVATE o2lite)
  set_property(TARGET ${name} PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)
endfunction()

o2testprogram(dispatchtest)
o2testprogram(typestest)
o2testprogram(taptest)
o2testprogram(coercetest)
o2testprogram(longtest)
o2testprogram(arraytest)
o2testprogram(o2client)
o2testprogram(o2server)
o2testprogram(nonblocksend)
o2testprogram(nonblockrecv)
o2testprogram(statusclient)
o2testprogram(statusserver)
o2testprogram(tcpclient)
o2testprogram(tcpserver)
o2testprogram(clockmirror)
o2testprogram(clockref)
o2testprogram(appfollow)
o2testprogram(applead)
o2testprogram(infotest1)
o2testprogram(infotest2)
o2testprogram(o2utclient)
o2testprogram(o2utserver)
o2testprogram(rawtcpserver)
o2testprogram(proptest)
o2testprogram(o2block)
o2testprogram(o2unblock)
o2testprogram(propsend)
o2testprogram(proprecv)
o2testprogram(tappub)
o2testprogram(tapsub)
o2testprogram(dropserver)
o2testprogram(dropclient)
o2testprogram(websockhost)
o2testprogram(stuniptest)
o2testprogram(mqttclient)
o2testprogram(mqttserver)
o2testprogram(unipub)
o2testprogram(unisub)
o2testprogram(o2litehost)
o2litetestprogram(o2litemsg c)
o2litetestprogram(o2liteserv c)

if(BUILD_WITH_PATTERN_SUPPORT)
  o2testprogram(patterntest)
endif(BUILD_WITH_PATTERN_SUPPORT)

if(BUILD_WITH_BRIDGE_SUPPORT)
  o2testprogram(bridgeapi)
endif(BUILD_WITH_BRIDGE_SUPPORT)

if(BUILD_WITH_SHAREDMEM_SUPPORT)
  o2testprogram(shmemserv ${PTHREAD})
  o2testprogram(shmembench ${PTHREAD})
  if(UNIX)
    target_link_libraries(shmemserv PRIVATE pthread)
    target_link_libraries(shmembench PRIVATE pthread)
  endif()    # windows uses winmm timer instead of pthreads
endif(BUILD_WITH_SHAREDMEM_SUPPORT)

if(BUILD_WITH_OSC_SUPPORT)
  o2testprogram(oscsendtest)
  o2testprogram(oscrecvtest)
  o2testprogram(oscanytest)
  if(BUILD_WITH_BUNDLE_SUPPORT)
    o2testprogram(bundletest)
    o2testprogram(oscbndlsend)
    o2testprogram(oscbndlrecv)
  endif(BUILD_WITH_BUNDLE_SUPPORT)
endif(BUILD_WITH_OSC_SUPPORT)

if(UNIX)
#  o2testprogram(rawtcpclient)
#  o2testprogram(rawudpserver)
#  o2testprogram(rawudpclient)
endif(UNIX)

if(BUILD_WITH_HUB_SUPPORT)
  o2testprogram(hubserver)
  o2testprogram(hubclient)
endif(BUILD_WITH_HUB_SUPPORT)

endif(TESTS_BUILD)

if(UNIX)
  if(APPLE)
    # EXTRA_LO_LIBS is libraries needed by Liblo applications
    set(EXTRA_LO_LIBS ${COREAUDIO_LIB} ${COREFOUNDATION_LIB}
                      ${COREMIDI_LIB} ${CORESERVICES_LIB})
  else(APPLE)
    # linux
    set(CMAKE_C_FLAGS "-std=c11 -mcx16")
    add_compile_options(-fno-exceptions -fno-rtti -fno-stack-protector -fomit-frame-pointer -ffunction-sections -fdata-sections -Wl,-gc-sections -fmerge-all-constants -Wl,-z,norelro -Wl,-hash-style=gnu) 
    # EXTRA_LO_LIBS is libraries needed by Liblo applications
    set(PTHREAD pthread CACHE INTERNAL "")
    set(EXTRA_LO_LIBS asound ${PTHREAD} m)
  endif(APPLE)
  if(TESTS_MIDI_EXAMPLE)
    # Use PortMidi Library
    set(PORTMIDI_DBG_LIB PORTMIDI_DBG_LIB-NOTFOUND)
    set(PORTMIDI_OPT_LIB PORTMIDI_OPT_LIB-NOTFOUND)

    if(APPLE)
      set(COREMIDI_LIB "${FRAMEWORK_PATH}/CoreMIDI.framework")
    else(APPLE)
    endif(APPLE)

    set(PORTMIDI_BASE_PATH ../portmedia/portmidi
        CACHE STRING "Where is portmidi?")
    set(PORTMIDI_PATH ${PORTMIDI_BASE_PATH}/pm_common CACHE INTERNAL
                      "Where is portmidi.h?" FORCE)
    message(STATUS "PORTMIDI_BASE_PATH is " ${PORTMIDI_BASE_PATH})

    if(USE_STATIC_LIBS)
      if(UNIX)
        find_library(PORTMIDI_DBG_LIB portmidi_s ${PORTMIDI_BASE_PATH}
                     ${PORTMIDI_BASE_PATH}/Debug
                     ${PORTMIDI_PATH} ${PORTMIDI_PATH}/Debug)
      else(UNIX) # always use dll for windows debug
        find_library(PORTMIDI_DBG_LIB portmidi HINTS
                     ${PORTMIDI_BASE_PATH} ${PORTMIDI_BASE_PATH}/Debug
                     ${PORTMIDI_PATH} ${PORTMIDI_PATH}/Debug)
      endif(UNIX)

      message(STATUS "*** in USE_STATIC_LIBS, USE_MIDI ${USE_MIDI} PORTMIDI_DBG_LIB ${PORTMIDI_DBG_LIB}")

    else(USE_STATIC_LIBS)
      find_library(PORTMIDI_DBG_LIB portmidi HINTS
                   ${PORTMIDI_BASE_PATH} ${PORTMIDI_BASE_PATH}/Debug
	           ${PORTMIDI_BASE_PATH}/x64/Debug
                   ${PORTMIDI_PATH} ${PORTMIDI_PATH}/Debug
                   ${PORTMIDI_PATH}/x64/Debug)
    endif(USE_STATIC_LIBS)

    add_executable(midiclient test/midiclient.cpp test/cmtio.cpp test/cmtio.h)
    target_include_directories(midiclient PRIVATE ${CMAKE_SOURCE_DIR}/src
    					          ${BONJOUR_INCLUDE_PATH})
    target_link_libraries(midiclient ${LIBRARIES})
    set_property(TARGET midiclient PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)

    add_executable(midiserver test/midiserver.cpp)
    target_include_directories(midiserver PRIVATE ${CMAKE_SOURCE_DIR}/src
          ${PORTMIDI_PATH} ${PORTMIDI_PATH}/../porttime
	  ${BONJOUR_INCLUDE_PATH})
    target_link_libraries(midiserver ${LIBRARIES} ${PORTMIDI_DBG_LIB})
    set_property(TARGET midiserver PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)
  endif(TESTS_MIDI_EXAMPLE)
endif(UNIX)

message(STATUS LIBRARIES=${LIBRARIES})

# this is some networking test code, not part of O2
#add_executable(broadcastclient test/broadcastclient.cpp)
#add_executable(broadcastserver test/broadcastserver.cpp)
#add_executable(tcppollclient test/tcppollclient.cpp)
#add_executable(tcppollserver test/tcppollserver.cpp)

##########################################################
# liblo was used for some performance comparisons, but it
# is disabled/commented out to remove the liblo path and
# library dependencies from this CMakeLists.txt file
##########################################################

message(STATUS "TESTS_BUILD ${TESTS_BUILD} BUILD_WITH_OSC_SUPPORT ${BUILD_WITH_OSC_SUPPORT} BUILD_TESTS_WITH_LIBLO ${BUILD_TESTS_WITH_LIBLO}")

if(TESTS_BUILD AND BUILD_WITH_OSC_SUPPORT AND BUILD_TESTS_WITH_LIBLO)
  set(LIBLO_PATH "${CMAKE_SOURCE_DIR}/../liblo-0.28" CACHE PATH
                 "Where to find liblo_64s.a, the liblo library.")
  set(LIBLO_LIB LIBLO_LIB-NOTFOUND CACHE FILEPATH
             "The liblo library; should be set automatically if LIBLO_PATH is correct and liblo_s64.a exists.")
  find_library(LIBLO_LIB NAMES lo_s64 lo_static PATHS ${LIBLO_PATH}
               PATH_SUFFIXES Release Debug)
  # where to find liblo include files:
  set(LIBLO_INCLUDE_PATH ${LIBLO_PATH} CACHE PATH "where to find liblo include files")

  message(STATUS "LIBLO_LIB is ${LIBLO_LIB}")

  # call with name only of main program, then additional source files
  function(o2libloprogram name)
    add_executable(${name}  test/${name}.cpp ${ARGN})
    # src is included because lo_bndlrecv includes o2base.h
    target_include_directories(${name} PRIVATE src ${LIBLO_INCLUDE_PATH})
    target_link_libraries(${name} ${LIBLO_LIB} ${EXTRA_LO_LIBS})
    set_property(TARGET ${name} PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)
  endfunction()

  # Create executables
  o2libloprogram(lo_benchmk_server)
  o2libloprogram(lo_benchmk_client)
  o2libloprogram(lo_oscrecv)
  o2libloprogram(lo_oscsend)

  if(BUILD_WITH_BUNDLE_SUPPORT)
    o2libloprogram(lo_bndlsend)
    o2libloprogram(lo_bndlrecv src/o2sleep.c)
  endif(BUILD_WITH_BUNDLE_SUPPORT)

endif(TESTS_BUILD AND BUILD_WITH_OSC_SUPPORT AND BUILD_TESTS_WITH_LIBLO)
