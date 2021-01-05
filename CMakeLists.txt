cmake_minimum_required(VERSION 2.8.0)

project(o2)

set(TESTS_BUILD ON CACHE BOOL "Compile regression and other tests")

set(TESTS_WITH_LIBLO OFF CACHE BOOL "Compile tests that use liblo,
requiring liblo library (only enabled if TESTS_BUILD is ON)")

set(TESTS_MIDI_EXAMPLE OFF CACHE BOOL "Compile midiclient & midiserver,
requiring portmidi library")

set(BUILD_WITH_PATTERN_SUPPORT ON CACHE BOOL "Allow pattern matching")   
set(BUILD_WITH_OSC_SUPPORT ON CACHE BOOL "Include OSC support")
set(BUILD_WITH_HUB_SUPPORT ON CACHE BOOL "Include o2_hub() discovery")
set(BUILD_WITH_BUNDLE_SUPPORT ON CACHE BOOL "Allow message bundles")  
set(BUILD_WITH_BRIDGE_SUPPORT ON CACHE BOOL "Allow other transport add-ons")   
set(BUILD_WITH_SHAREDMEM_SUPPORT ON CACHE BOOL 
    "Include shared memory bridge API, requires bridge support")
set(BUILD_WITH_MESSAGE_PRINT OFF CACHE BOOL
"Provide O2message_print even in non-debug builds (it is always provided in debug builds)")

set(BUILD_WITH_O2LITE_DISCOVERY ON CACHE BOOL 
"Added code to o2lite clients for faster discovery") 
set(BUILD_WITH_O2LITE_CLOCKSYNC ON CACHE BOOL 
"Added code to o2lite clients to synchronize clock with  O2") 


# O2 intentionally writes outside of declared array bounds (and
#  carefully insures that space is allocated beyond array bounds,
#  especially for message data, which is declared char[4], but can
#  be arbitrariily long -- well, at least up to O2_MAX_MSG_SIZE)
# Newer compilers will try to enforce char[4] in memcpy and strcpy
#  unless we turn off this behavior with the following macro definition:
add_definitions("-D_FORTIFY_SOURCE=0")

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
  include(static.cmake)
  set(EXTRA_LIBS winmm.lib ws2_32.lib Iphlpapi.lib)
endif(WIN32)

if(UNIX)
  if(APPLE)
    set(FRAMEWORK_PATH ${CMAKE_OSX_SYSROOT}/System/Library/Frameworks) 
    set(EXTRA_LIBS "${FRAMEWORK_PATH}/CoreAudio.framework")
  else(APPLE) # must be Linux
    set(EXTRA_LIBS m atomic) # needed by liblo
  endif(APPLE)
endif(UNIX)

#set(CMAKE_CXX_FLAGS "-stdlib=libc++")
#set(CMAKE_EXE_LINKER_FLAGS "-stdlib=libc++")

# o2
 
set(O2_SRC  
  src/o2atomic.cpp src/o2atomic.h
  src/o2base.h
  src/bridge.cpp src/bridge.h 
  src/clock.cpp src/clock.h 
  src/debug.cpp src/debug.h 
  src/discovery.cpp src/discovery.h 
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
  src/o2.cpp src/o2.h src/o2base.h src/o2internal.h 
  src/o2osc.cpp src/o2osc.h
  src/pathtree.cpp src/pathtree.h
  src/processes.cpp src/processes.h
  src/properties.cpp src/properties.h
  src/o2sched.cpp src/o2sched.h
  src/o2usleep.cpp src/o2usleep.h
  src/services.cpp src/services.h 
  src/sharedmem.cpp src/sharedmem.h
  src/o2mem.cpp src/o2mem.h 
  src/o2obj.h
  src/sharedmemclient.h
  src/stun.cpp src/stun.h
  )  

add_library(o2_static STATIC ${O2_SRC})  

# o2lite


set(O2LITE_SRC  
  src/o2lite.c src/o2lite.h
  )
  
add_library(o2lite_static STATIC ${O2LITE_SRC})

if(BUILD_WITH_O2LITE_DISCOVERY)   
else(BUILD_WITH_O2LITE_DISCOVERY)  
  target_compile_definitions(o2lite_static, "-DO2L_NO_DISCOVERY")   
endif(BUILD_WITH_O2LITE_DISCOVERY)  

if(BUILD_WITH_O2LITE_CLOCKSYNC)   
else(BUILD_WITH_O2LITE_CLOCKSYNC)  
  target_compile_definitions(o2lite_static, "-DO2L_NO_CLOCKSYNC")   
endif(BUILD_WITH_O2LITE_CLOCKSYNC)  

#target_include_directories(o2_static PRIVATE ${no include directories})

#######################
# BUILD CONFIGURATION #
#######################

set(LIBRARIES o2_static ${EXTRA_LIBS} CACHE INTERNAL "") 
set(O2LITE_LIBS o2lite_static ${EXTRA_LIBS} CACHE INTERNAL "")

if(TESTS_BUILD)
message(STATUS "Building test programs")

set(BUILD_STATIC_LIB TRUE CACHE BOOL "Build a static lib -- currently the only thing supported, turning OFF probably gives you a static library without a static runtime library option.")

# DEAL WITH WINDOWS OPTIONS: DEATH BY A THOUSAND CUTS
if(WIN32)
  if(USE_STATIC_LIBS)
    # release will use static runtime library
    foreach(flag_var CMAKE_CXX_FLAGS_RELEASE CMAKE_C_FLAGS_RELEASE
        CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_MINSIZEREL
        CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CFLAGS CMAKE_C_FLAGS_MINSIZEREL
        CMAKE_C_FLAGS_RELWITHDEBINFO)
      if(${flag_var} MATCHES "/MD")
        string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
      endif(${flag_var} MATCHES "/MD")
    endforeach(flag_var)
    message(STATUS 
      "Note: overriding CMAKE_*_FLAGS_* to use Visual C static multithread library")
    set(VERBOSE_WARNINGS 0 CACHE BOOL "Do not use _CRT_SECURE_NO_WARNINGS to disable suggestions to use 'secure' versions of strcpy, etc.")
  endif(USE_STATIC_LIBS)
endif(WIN32)


# EXECUTABLE
# Create executables 
add_executable(dispatchtest test/dispatchtest.cpp)  
target_include_directories(dispatchtest PRIVATE ${CMAKE_SOURCE_DIR}/src)  
target_link_libraries(dispatchtest ${LIBRARIES}) 
 
add_executable(typestest test/typestest.cpp)   
target_include_directories(typestest PRIVATE ${CMAKE_SOURCE_DIR}/src)   
target_link_libraries(typestest ${LIBRARIES}) 

add_executable(taptest test/taptest.cpp)   
target_include_directories(taptest PRIVATE ${CMAKE_SOURCE_DIR}/src)   
target_link_libraries(taptest ${LIBRARIES}) 

add_executable(coercetest test/coercetest.cpp)    
target_include_directories(coercetest PRIVATE ${CMAKE_SOURCE_DIR}/src)    
target_link_libraries(coercetest ${LIBRARIES}) 
   
add_executable(longtest test/longtest.cpp)     
target_include_directories(longtest PRIVATE ${CMAKE_SOURCE_DIR}/src)     
target_link_libraries(longtest ${LIBRARIES}) 
    
add_executable(arraytest test/arraytest.cpp)     
target_include_directories(arraytest PRIVATE ${CMAKE_SOURCE_DIR}/src)     
target_link_libraries(arraytest ${LIBRARIES}) 

if(BUILD_WITH_PATTERN_SUPPORT)
  add_executable(patterntest test/patterntest.cpp)     
  target_include_directories(patterntest PRIVATE ${CMAKE_SOURCE_DIR}/src)     
  target_link_libraries(patterntest ${LIBRARIES})  
endif(BUILD_WITH_PATTERN_SUPPORT)

if(BUILD_WITH_BRIDGE_SUPPORT)  
  add_executable(bridgeapi test/bridgeapi.cpp)     
  target_include_directories(bridgeapi PRIVATE ${CMAKE_SOURCE_DIR}/src)     
  target_link_libraries(bridgeapi ${LIBRARIES})  
endif(BUILD_WITH_BRIDGE_SUPPORT)  
 
if(BUILD_WITH_SHAREDMEM_SUPPORT)  
  add_executable(shmemserv test/shmemserv.cpp)     
  target_include_directories(shmemserv PRIVATE ${CMAKE_SOURCE_DIR}/src)     
  target_link_libraries(shmemserv ${LIBRARIES} ${PTHREAD})  
endif(BUILD_WITH_SHAREDMEM_SUPPORT)  
 
add_executable(o2client test/o2client.cpp)  
target_include_directories(o2client PRIVATE ${CMAKE_SOURCE_DIR}/src)  
target_link_libraries(o2client ${LIBRARIES}) 
 
add_executable(o2server test/o2server.cpp)  
target_include_directories(o2server PRIVATE ${CMAKE_SOURCE_DIR}/src)  
target_link_libraries(o2server ${LIBRARIES}) 
 
add_executable(nonblocksend test/nonblocksend.cpp)  
target_include_directories(nonblocksend PRIVATE ${CMAKE_SOURCE_DIR}/src)  
target_link_libraries(nonblocksend ${LIBRARIES}) 
 
add_executable(nonblockrecv test/nonblockrecv.cpp)  
target_include_directories(nonblockrecv PRIVATE ${CMAKE_SOURCE_DIR}/src)  
target_link_libraries(nonblockrecv ${LIBRARIES}) 
 
add_executable(statusclient test/statusclient.cpp)
target_include_directories(statusclient PRIVATE ${CMAKE_SOURCE_DIR}/src)   
target_link_libraries(statusclient ${LIBRARIES}) 
  
add_executable(statusserver test/statusserver.cpp)   
target_include_directories(statusserver PRIVATE ${CMAKE_SOURCE_DIR}/src)   
target_link_libraries(statusserver ${LIBRARIES}) 
 
add_executable(tcpclient test/tcpclient.cpp)   
target_include_directories(tcpclient PRIVATE ${CMAKE_SOURCE_DIR}/src)   
target_link_libraries(tcpclient ${LIBRARIES}) 
  
add_executable(tcpserver test/tcpserver.cpp)   
target_include_directories(tcpserver PRIVATE ${CMAKE_SOURCE_DIR}/src)   
target_link_libraries(tcpserver ${LIBRARIES}) 
 
add_executable(clockmirror test/clockmirror.cpp) 
target_include_directories(clockmirror PRIVATE ${CMAKE_SOURCE_DIR}/src) 
target_link_libraries(clockmirror ${LIBRARIES}) 

add_executable(clockref test/clockref.cpp)  
target_include_directories(clockref PRIVATE ${CMAKE_SOURCE_DIR}/src)  
target_link_libraries(clockref ${LIBRARIES}) 

add_executable(appfollow test/appfollow.cpp) 
target_include_directories(appfollow PRIVATE ${CMAKE_SOURCE_DIR}/src) 
target_link_libraries(appfollow ${LIBRARIES}) 

add_executable(applead test/applead.cpp)  
target_include_directories(applead PRIVATE ${CMAKE_SOURCE_DIR}/src)  
target_link_libraries(applead ${LIBRARIES}) 

if(BUILD_WITH_OSC_SUPPORT) 
  add_executable(oscsendtest test/oscsendtest.cpp)  
  target_include_directories(oscsendtest PRIVATE ${CMAKE_SOURCE_DIR}/src)  
  target_link_libraries(oscsendtest ${LIBRARIES}) 
 
  add_executable(oscrecvtest test/oscrecvtest.cpp)   
  target_include_directories(oscrecvtest PRIVATE ${CMAKE_SOURCE_DIR}/src)   
  target_link_libraries(oscrecvtest ${LIBRARIES}) 
  
  add_executable(oscanytest test/oscanytest.cpp)   
  target_include_directories(oscanytest PRIVATE ${CMAKE_SOURCE_DIR}/src)   
  target_link_libraries(oscanytest ${LIBRARIES}) 
  
  if(BUILD_WITH_BUNDLE_SUPPORT)
    add_executable(bundletest test/bundletest.cpp)   
    target_include_directories(bundletest PRIVATE ${CMAKE_SOURCE_DIR}/src)   
    target_link_libraries(bundletest ${LIBRARIES}) 
  
    add_executable(oscbndlsend test/oscbndlsend.cpp)   
    target_include_directories(oscbndlsend PRIVATE ${CMAKE_SOURCE_DIR}/src)   
    target_link_libraries(oscbndlsend ${LIBRARIES}) 

    add_executable(oscbndlrecv test/oscbndlrecv.cpp)    
    target_include_directories(oscbndlrecv PRIVATE ${CMAKE_SOURCE_DIR}/src)    
    target_link_libraries(oscbndlrecv ${LIBRARIES})  
  endif(BUILD_WITH_BUNDLE_SUPPORT)
endif(BUILD_WITH_OSC_SUPPORT) 

add_executable(infotest1 test/infotest1.cpp)    
target_include_directories(infotest1 PRIVATE ${CMAKE_SOURCE_DIR}/src)    
target_link_libraries(infotest1 ${LIBRARIES})  
 
add_executable(infotest2 test/infotest2.cpp)    
target_include_directories(infotest2 PRIVATE ${CMAKE_SOURCE_DIR}/src)    
target_link_libraries(infotest2 ${LIBRARIES})  

add_executable(o2utclient test/o2utclient.cpp)  
target_include_directories(o2utclient PRIVATE ${CMAKE_SOURCE_DIR}/src)  
target_link_libraries(o2utclient ${LIBRARIES}) 
 
add_executable(o2utserver test/o2utserver.cpp)   
target_include_directories(o2utserver PRIVATE ${CMAKE_SOURCE_DIR}/src)    
target_link_libraries(o2utserver ${LIBRARIES}) 

add_executable(rawtcpserver test/rawtcpserver.cpp)   
target_link_libraries(rawtcpserver ${LIBRARIES}) 

if(APPLE)
  add_executable(rawtcpclient test/rawtcpclient.cpp)   
  target_link_libraries(rawtcpclient ${LIBRARIES}) 

  add_executable(rawudpserver test/rawudpserver.cpp)   
  target_link_libraries(rawudpserver ${LIBRARIES}) 

  add_executable(rawudpclient test/rawudpclient.cpp)   
  target_link_libraries(rawudpclient ${LIBRARIES}) 
endif(APPLE)

add_executable(proptest test/proptest.cpp)    
target_include_directories(proptest PRIVATE ${CMAKE_SOURCE_DIR}/src)     
target_link_libraries(proptest ${LIBRARIES}) 
 
add_executable(o2block test/o2block.cpp)    
target_include_directories(o2block PRIVATE ${CMAKE_SOURCE_DIR}/src)     
target_link_libraries(o2block ${LIBRARIES}) 
 
add_executable(o2unblock test/o2unblock.cpp)    
target_include_directories(o2unblock PRIVATE ${CMAKE_SOURCE_DIR}/src)     
target_link_libraries(o2unblock ${LIBRARIES}) 

add_executable(propsend test/propsend.cpp)    
target_include_directories(propsend PRIVATE ${CMAKE_SOURCE_DIR}/src)     
target_link_libraries(propsend ${LIBRARIES}) 

add_executable(proprecv test/proprecv.cpp)     
target_include_directories(proprecv PRIVATE ${CMAKE_SOURCE_DIR}/src)      
target_link_libraries(proprecv ${LIBRARIES}) 
 
add_executable(tappub test/tappub.cpp)  
target_include_directories(tappub PRIVATE ${CMAKE_SOURCE_DIR}/src)       
target_link_libraries(tappub ${LIBRARIES}) 
  
add_executable(tapsub test/tapsub.cpp)      
target_include_directories(tapsub PRIVATE ${CMAKE_SOURCE_DIR}/src)       
target_link_libraries(tapsub ${LIBRARIES}) 

if(BUILD_WITH_HUB_SUPPORT)
  add_executable(hubserver test/hubserver.cpp)
  target_include_directories(hubserver PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(hubserver ${LIBRARIES})
  
  add_executable(hubclient test/hubclient.cpp)
  target_include_directories(hubclient PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(hubclient ${LIBRARIES})
endif(BUILD_WITH_HUB_SUPPORT)
   
add_executable(dropserver test/dropserver.cpp)
target_include_directories(dropserver PRIVATE ${CMAKE_SOURCE_DIR}/src)       
target_link_libraries(dropserver ${LIBRARIES}) 
  
add_executable(dropclient test/dropclient.cpp)       
target_include_directories(dropclient PRIVATE ${CMAKE_SOURCE_DIR}/src)        
target_link_libraries(dropclient ${LIBRARIES}) 

add_executable(o2litemsg test/o2litemsg.c)
target_include_directories(o2litemsg PRIVATE ${CMAKE_SOURCE_DIR}/src)         
target_link_libraries(o2litemsg ${O2LITE_LIBS})  
    
add_executable(o2liteserv test/o2liteserv.c)
target_include_directories(o2liteserv PRIVATE ${CMAKE_SOURCE_DIR}/src)         
target_link_libraries(o2liteserv ${O2LITE_LIBS})  

add_executable(o2litehost test/o2litehost.cpp)
target_include_directories(o2litehost PRIVATE ${CMAKE_SOURCE_DIR}/src)         
target_link_libraries(o2litehost ${LIBRARIES})  

add_executable(stuniptest test/stuniptest.cpp)
target_include_directories(stuniptest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(stuniptest ${LIBRARIES})

add_executable(mqttclient test/mqttclient.cpp) 
target_include_directories(mqttclient PRIVATE ${CMAKE_SOURCE_DIR}/src)         
target_link_libraries(mqttclient ${LIBRARIES})  

add_executable(mqttserver test/mqttserver.cpp) 
target_include_directories(mqttserver PRIVATE ${CMAKE_SOURCE_DIR}/src)         
target_link_libraries(mqttserver ${LIBRARIES})  

endif(TESTS_BUILD)  
 
if(UNIX)
  if(APPLE)
    # EXTRA_LO_LIBS is libraries needed by Liblo applications
    set(EXTRA_LO_LIBS ${COREAUDIO_LIB} ${COREFOUNDATION_LIB}
                      ${COREMIDI_LIB} ${CORESERVICES_LIB})
  else(APPLE)
    # linux
    set(CMAKE_C_FLAGS "-std=c11 -mcx16")
    set(PTHREAD pthread CACHE INTERNAL "")
    # EXTRA_LO_LIBS is libraries needed by Liblo applications
    set(EXTRA_LO_LIBS asound ${PTHREAD} m CACHE INTERNAL "")
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
    target_include_directories(midiclient PRIVATE ${CMAKE_SOURCE_DIR}/src) 
    target_link_libraries(midiclient ${LIBRARIES})

    add_executable(midiserver test/midiserver.cpp)
    target_include_directories(midiserver PRIVATE ${CMAKE_SOURCE_DIR}/src
          ${PORTMIDI_PATH} ${PORTMIDI_PATH}/../porttime)
    target_link_libraries(midiserver ${LIBRARIES} ${PORTMIDI_DBG_LIB}) 
  endif(TESTS_MIDI_EXAMPLE) 
endif(UNIX)

message(STATUS LIBRARIES=${LIBRARIES}) 

# this is some networking test code, not part of O2
#add_executable(broadcastclient test/broadcastclient.cpp) 
#add_executable(broadcastserver test/broadcastserver.cpp) 
#add_executable(tcppollclient test/tcppollclient.cpp) 
#add_executable(tcppollserver test/tcppollserver.cpp) 



# I don't know if this is necessary. If it is, it should be duplicated
# for o2client and o2server:
#if(WIN32)
  #set_target_properties(o2test PROPERTIES 
                               #LINK_FLAGS "/SUBSYSTEM:WINDOWS")
  #set_property(TARGET o2test PROPERTY WIN32_EXECUTABLE TRUE)
#endif(WIN32)

##########################################################
# liblo was used for some performance comparisons, but it 
# is disabled/commented out to remove the liblo path and 
# library dependencies from this CMakeLists.txt file 
##########################################################

message(STATUS "TESTS_BUILD ${TESTS_BUILD} BUILD_WITH_OSC_SUPPORT ${BUILD_WITH_OSC_SUPPORT} BUILD_TESTS_WITH_LIBLO ${BUILD_TESTS_WITH_LIBLO}")

if(TESTS_BUILD)
if(BUILD_WITH_OSC_SUPPORT)  
if(BUILD_TESTS_WITH_LIBLO)
  set(LIBLO_PATH "${CMAKE_SOURCE_DIR}/../liblo-0.28" CACHE PATH
                 "Where to find liblo_64s.a, the liblo library.") 
  set(LIBLO_LIB LIBLO_LIB-NOTFOUND CACHE FILEPATH
             "The liblo library; should be set automatically if LIBLO_PATH is correct and liblo_s64.a exists.") 
  find_library(LIBLO_LIB NAMES lo_s64 lo_static PATHS ${LIBLO_PATH}
               PATH_SUFFIXES Release Debug)
  # where to find liblo include files:
  set(LIBLO_INCLUDE_PATH ${LIBLO_PATH} CACHE PATH "where to find liblo include files") 

  message(STATUS "LIBLO_LIB is ${LIBLO_LIB}") 

  # Create executables 
  add_executable(lo_benchmk_server  test/lo_benchmk_server.cpp) 
  target_include_directories(lo_benchmk_server PRIVATE ${LIBLO_INCLUDE_PATH})
  target_link_libraries(lo_benchmk_server ${LIBLO_LIB} ${EXTRA_LO_LIBS}) 

  add_executable(lo_benchmk_client  test/lo_benchmk_client.cpp) 
  target_include_directories(lo_benchmk_client PRIVATE ${LIBLO_INCLUDE_PATH})  
  target_link_libraries(lo_benchmk_client ${LIBLO_LIB}  ${EXTRA_LO_LIBS})  

  add_executable(lo_oscrecv test/lo_oscrecv.cpp) 
  target_include_directories(lo_oscrecv PRIVATE ${LIBLO_INCLUDE_PATH})  
  target_link_libraries(lo_oscrecv ${LIBLO_LIB}  ${EXTRA_LO_LIBS})   

  add_executable(lo_oscsend test/lo_oscsend.cpp) 
  target_include_directories(lo_oscsend PRIVATE ${LIBLO_INCLUDE_PATH})  
  target_link_libraries(lo_oscsend ${LIBLO_LIB}  ${EXTRA_LO_LIBS})    

  if(BUILD_WITH_BUNDLE_SUPPORT)
    add_executable(lo_bndlsend test/lo_bndlsend.cpp) 
    target_include_directories(lo_bndlsend PRIVATE ${LIBLO_INCLUDE_PATH})  
    target_link_libraries(lo_bndlsend ${LIBLO_LIB}  ${EXTRA_LO_LIBS})     

    add_executable(lo_bndlrecv test/lo_bndlrecv.cpp) 
    target_include_directories(lo_bndlrecv PRIVATE ${LIBLO_INCLUDE_PATH})  
    target_link_libraries(lo_bndlrecv ${LIBLO_LIB}  ${EXTRA_LO_LIBS})     
  endif(BUILD_WITH_BUNDLE_SUPPORT)
endif(BUILD_TESTS_WITH_LIBLO)
endif(BUILD_WITH_OSC_SUPPORT)  
endif(TESTS_BUILD)
