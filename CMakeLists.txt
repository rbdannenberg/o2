cmake_minimum_required(VERSION 3.2.2)

project(o2)

if(WIN32)
  add_definitions("-D_CRT_SECURE_NO_WARNINGS -D_WINSOCK_DEPRECATED_NO_WARNINGS -DIS_BIG_ENDIAN=0")
  set(EXTRA_LIBS winmm.lib ws2_32.lib Iphlpapi.lib)
endif(WIN32)

if(APPLE)
  set(FRAMEWORK_PATH ${CMAKE_OSX_SYSROOT}/System/Library/Frameworks) 
  set(EXTRA_LIBS "${FRAMEWORK_PATH}/CoreAudio.framework") 
endif(APPLE)

#set(CMAKE_CXX_FLAGS "-stdlib=libc++")
#set(CMAKE_EXE_LINKER_FLAGS "-stdlib=libc++")

# o2
 
set(O2_SRC  
  src/o2_dynamic.c src/o2_dynamic.c 
  src/o2.c src/o2.h
  src/o2_discovery.c src/o2_discovery.h
  src/o2_error.h 
  src/o2_internal.h
  src/o2_message.c src/o2_message.h 
  src/o2_sched.c src/o2_sched.h
  src/o2_search.c src/o2_search.h 
  src/o2_send.c src/o2_send.h 
  src/o2_socket.c src/o2_socket.h 
  src/o2_clock.c src/o2_clock.h
  # src/o2_debug.c src/o2_debug.h
  src/o2_interoperation.c
  )  
 
add_library(o2_static STATIC ${O2_SRC})  
 
#target_include_directories(o2_static PRIVATE ${no include directories})

#######################
# BUILD CONFIGURATION #
#######################

set(LIBRARIES o2_static ${EXTRA_LIBS} CACHE INTERNAL "") 

# EXECUTABLE
# Create executables 
add_executable(dispatchtest test/dispatchtest.c)  
target_include_directories(dispatchtest PRIVATE ${CMAKE_SOURCE_DIR}/src)  
target_link_libraries(dispatchtest ${LIBRARIES}) 
 
add_executable(typestest test/typestest.c)   
target_include_directories(typestest PRIVATE ${CMAKE_SOURCE_DIR}/src)   
target_link_libraries(typestest ${LIBRARIES}) 
  
add_executable(coercetest test/coercetest.c)    
target_include_directories(coercetest PRIVATE ${CMAKE_SOURCE_DIR}/src)    
target_link_libraries(coercetest ${LIBRARIES}) 
   
add_executable(longtest test/longtest.c)     
target_include_directories(longtest PRIVATE ${CMAKE_SOURCE_DIR}/src)     
target_link_libraries(longtest ${LIBRARIES}) 
    
add_executable(arraytest test/arraytest.c)     
target_include_directories(arraytest PRIVATE ${CMAKE_SOURCE_DIR}/src)     
target_link_libraries(arraytest ${LIBRARIES}) 
    
add_executable(o2client test/o2client.c) 
target_include_directories(o2client PRIVATE ${CMAKE_SOURCE_DIR}/src) 
target_link_libraries(o2client ${LIBRARIES}) 

add_executable(o2server test/o2server.c) 
target_include_directories(o2server PRIVATE ${CMAKE_SOURCE_DIR}/src) 
target_link_libraries(o2server ${LIBRARIES}) 

add_executable(tcpclient test/tcpclient.c)  
target_include_directories(tcpclient PRIVATE ${CMAKE_SOURCE_DIR}/src)  
target_link_libraries(tcpclient ${LIBRARIES}) 
 
add_executable(tcpserver test/tcpserver.c)  
target_include_directories(tcpserver PRIVATE ${CMAKE_SOURCE_DIR}/src)  
target_link_libraries(tcpserver ${LIBRARIES}) 

add_executable(clockslave test/clockslave.c) 
target_include_directories(clockslave PRIVATE ${CMAKE_SOURCE_DIR}/src) 
target_link_libraries(clockslave ${LIBRARIES}) 

add_executable(clockmaster test/clockmaster.c)  
target_include_directories(clockmaster PRIVATE ${CMAKE_SOURCE_DIR}/src)  
target_link_libraries(clockmaster ${LIBRARIES}) 

add_executable(oscsendtest test/oscsendtest.c)  
target_include_directories(oscsendtest PRIVATE ${CMAKE_SOURCE_DIR}/src)  
target_link_libraries(oscsendtest ${LIBRARIES}) 
 
add_executable(oscrecvtest test/oscrecvtest.c)  
target_include_directories(oscrecvtest PRIVATE ${CMAKE_SOURCE_DIR}/src)  
target_link_libraries(oscrecvtest ${LIBRARIES}) 
 
 
if(UNIX)
  # Use PortMidi Library
  set(PORTMIDI_DBG_LIB PORTMIDI_DBG_LIB-NOTFOUND)
  set(PORTMIDI_OPT_LIB PORTMIDI_OPT_LIB-NOTFOUND)

  if(APPLE)
    set(COREMIDI_LIB "${FRAMEWORK_PATH}/CoreMIDI.framework")
    set(EXTRA_NEEDED_LIBS ${COREAUDIO_LIB} ${COREFOUNDATION_LIB}
                       ${COREMIDI_LIB} ${CORESERVICES_LIB})
  else(APPLE)
    # LINUX
    set(PTHREAD_LIB pthread)
    set(EXTRA_NEEDED_LIBS asound ${PTHREAD_LIB})
  endif(APPLE)
  # using ../../liblo/liblo_s.a. The builder must explicitly make a 
  # static liblo library with --enable-static and copy the result,
  # ../../liblo/src/.libs/liblo.a, to ../../liblo/liblo_s.a. Otherwise,
  # at least XCode might try to link with /usr/local/lib/liblo.dylib
  # By renaming, we gain confidence as to what is actually being linked.
  #
  # for UNIX...

  set(PORTMIDI_BASE_PATH ../portmedia/portmidi 
      CACHE STRING "Where is portmidi?")
  set(PORTMIDI_PATH ${PORTMIDI_BASE_PATH}/pm_common CACHE INTERNAL 
                    "Where is portmidi.h?" FORCE)
  # message(STATUS "PORTMIDI_BASE_PATH is " ${PORTMIDI_BASE_PATH})

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

  add_executable(midiclient test/midiclient.c test/cmtio.c test/cmtio.h) 
  target_include_directories(midiclient PRIVATE ${CMAKE_SOURCE_DIR}/src) 
  target_link_libraries(midiclient ${LIBRARIES})

  add_executable(midiserver test/midiserver.c)
  target_include_directories(midiserver PRIVATE ${CMAKE_SOURCE_DIR}/src
        ${PORTMIDI_PATH} ${PORTMIDI_PATH}/../porttime)
  target_link_libraries(midiserver ${LIBRARIES} ${PORTMIDI_DBG_LIB}) 
endif(UNIX)

message(STATUS LIBRARIES=${LIBRARIES}) 

#add_executable(broadcastclient test/broadcastclient.c) 
#add_executable(broadcastserver test/broadcastserver.c) 
#add_executable(tcppollclient test/tcppollclient.c) 
#add_executable(tcppollserver test/tcppollserver.c) 



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

#set(LO_LIB LO_LIB-NOTFOUND) 
# where to find library: lo_s64 

#set(LIBLO_PATH ${CMAKE_SOURCE_DIR}/../../liblo) 
#find_library(LO_LIB lo_s64 ${LIBLO_PATH}) 
# where to find liblo include files:
#set(LO_PATH ${LIBLO_PATH} CACHE PATH "where to find liblo include files") 

#message(STATUS "LO_LIB is ${LO_LIB}") 

# Create executables 
#add_executable(lo_benchmk_server  test/lo_benchmk_server.c) 
#target_include_directories(lo_benchmk_server PRIVATE ${LO_PATH})
#target_link_libraries(lo_benchmk_server ${LO_LIB} ${EXTRA_NEEDED_LIBS}) 

#add_executable(lo_benchmk_client  test/lo_benchmk_client.c) 
#target_include_directories(lo_benchmk_client PRIVATE ${LO_PATH})
#target_link_libraries(lo_benchmk_client ${LO_LIB}  ${EXTRA_NEEDED_LIBS})
