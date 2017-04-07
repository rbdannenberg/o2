cmake_minimum_required(VERSION 2.8.0)

project(o2)

set(BUILD_TESTS ON CACHE BOOL "Compile regression and other tests")

set(BUILD_TESTS_WITH_LIBLO OFF CACHE BOOL "Compile tests that use liblo,
requiring liblo library (only enabled if BUILD_TESTS is ON)")

set(BUILD_MIDI_EXAMPLE OFF CACHE BOOL "Compile midiclient & midiserver,
requiring portmidi library")

if(WIN32)
  add_definitions("-D_CRT_SECURE_NO_WARNINGS -D_WINSOCK_DEPRECATED_NO_WARNINGS -DIS_BIG_ENDIAN=0")
  include(static.cmake)
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
  src/o2_dynamic.c src/o2_dynamic.h
  src/o2.c src/o2.h src/o2_internal.h 
  src/o2_discovery.c src/o2_discovery.h
  src/o2_message.c src/o2_message.h 
  src/o2_sched.c src/o2_sched.h
  src/o2_search.c src/o2_search.h 
  src/o2_send.c src/o2_send.h 
  src/o2_socket.c src/o2_socket.h 
  src/o2_clock.c src/o2_clock.h
  # src/o2_debug.c src/o2_debug.h
  src/o2_interoperation.c src/o2_interoperation.h
  )  
 
add_library(o2_static STATIC ${O2_SRC})  
 
#target_include_directories(o2_static PRIVATE ${no include directories})

#######################
# BUILD CONFIGURATION #
#######################

set(LIBRARIES o2_static ${EXTRA_LIBS} CACHE INTERNAL "") 

if(BUILD_TESTS)
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

add_executable(statusclient test/statusclient.c)
target_include_directories(statusclient PRIVATE ${CMAKE_SOURCE_DIR}/src)   
target_link_libraries(statusclient ${LIBRARIES}) 
  
add_executable(statusserver test/statusserver.c)   
target_include_directories(statusserver PRIVATE ${CMAKE_SOURCE_DIR}/src)   
target_link_libraries(statusserver ${LIBRARIES}) 
 
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
  
add_executable(oscanytest test/oscanytest.c)   
target_include_directories(oscanytest PRIVATE ${CMAKE_SOURCE_DIR}/src)   
target_link_libraries(oscanytest ${LIBRARIES}) 
  
add_executable(bundletest test/bundletest.c)   
target_include_directories(bundletest PRIVATE ${CMAKE_SOURCE_DIR}/src)   
target_link_libraries(bundletest ${LIBRARIES}) 
  
add_executable(oscbndlsend test/oscbndlsend.c)   
target_include_directories(oscbndlsend PRIVATE ${CMAKE_SOURCE_DIR}/src)   
target_link_libraries(oscbndlsend ${LIBRARIES}) 

add_executable(oscbndlrecv test/oscbndlrecv.c)   
target_include_directories(oscbndlrecv PRIVATE ${CMAKE_SOURCE_DIR}/src)   
target_link_libraries(oscbndlrecv ${LIBRARIES}) 
endif(BUILD_TESTS)

if(UNIX)
  if(APPLE)
  else(APPLE)
    # linux
    set(CMAKE_C_FLAGS "-std=gnu99")
  endif(APPLE)
  if(BUILD_MIDI_EXAMPLE)
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

    add_executable(midiclient test/midiclient.c test/cmtio.c test/cmtio.h) 
    target_include_directories(midiclient PRIVATE ${CMAKE_SOURCE_DIR}/src) 
    target_link_libraries(midiclient ${LIBRARIES})

    add_executable(midiserver test/midiserver.c)
    target_include_directories(midiserver PRIVATE ${CMAKE_SOURCE_DIR}/src
          ${PORTMIDI_PATH} ${PORTMIDI_PATH}/../porttime)
    target_link_libraries(midiserver ${LIBRARIES} ${PORTMIDI_DBG_LIB}) 
  endif(BUILD_MIDI_EXAMPLE) 
endif(UNIX)

message(STATUS LIBRARIES=${LIBRARIES}) 

# this is some networking test code, not part of O2
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

if(BUILD_TESTS)
if(BUILD_TESTS_WITH_LIBLO)
  set(LIBLO_PATH "${CMAKE_SOURCE_DIR}/../liblo-0.28" CACHE PATH
                 "Where to find liblo_64s.a, the liblo library.") 
  set(LIBLO_LIB LIBLO_LIB-NOTFOUND CACHE FILEPATH
             "The liblo library; should be set automatically if LIBLO_PATH is correct and liblo_64s.a exists.") 
  find_library(LIBLO_LIB lo_s64 ${LIBLO_PATH})
  # where to find liblo include files:
  set(LIBLO_INCLUDE_PATH ${LIBLO_PATH} CACHE PATH "where to find liblo include files") 

  message(STATUS "LIBLO_LIB is ${LIBLO_LIB}") 

  # Create executables 
  add_executable(lo_benchmk_server  test/lo_benchmk_server.c) 
  target_include_directories(lo_benchmk_server PRIVATE ${LIBLO_INCLUDE_PATH})
  target_link_libraries(lo_benchmk_server ${LIBLO_LIB} ${EXTRA_NEEDED_LIBS}) 

  add_executable(lo_benchmk_client  test/lo_benchmk_client.c) 
  target_include_directories(lo_benchmk_client PRIVATE ${LIBLO_INCLUDE_PATH})  
  target_link_libraries(lo_benchmk_client ${LIBLO_LIB}  ${EXTRA_NEEDED_LIBS})  

  add_executable(lo_oscrecv test/lo_oscrecv.c) 
  target_include_directories(lo_oscrecv PRIVATE ${LIBLO_INCLUDE_PATH})  
  target_link_libraries(lo_oscrecv ${LIBLO_LIB}  ${EXTRA_NEEDED_LIBS})   

  add_executable(lo_oscsend test/lo_oscsend.c) 
  target_include_directories(lo_oscsend PRIVATE ${LIBLO_INCLUDE_PATH})  
  target_link_libraries(lo_oscsend ${LIBLO_LIB}  ${EXTRA_NEEDED_LIBS})    

  add_executable(lo_bndlsend test/lo_bndlsend.c) 
  target_include_directories(lo_bndlsend PRIVATE ${LIBLO_INCLUDE_PATH})  
  target_link_libraries(lo_bndlsend ${LIBLO_LIB}  ${EXTRA_NEEDED_LIBS})     

  add_executable(lo_bndlrecv test/lo_bndlrecv.c) 
  target_include_directories(lo_bndlrecv PRIVATE ${LIBLO_INCLUDE_PATH})  
  target_link_libraries(lo_bndlrecv ${LIBLO_LIB}  ${EXTRA_NEEDED_LIBS})     
endif(BUILD_TESTS_WITH_LIBLO)
endif(BUILD_TESTS)
