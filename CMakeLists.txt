cmake_minimum_required(VERSION 3.2.2)

project(o2)

if(WIN32)
  add_definitions("-D_CRT_SECURE_NO_WARNINGS -D_WINSOCK_DEPRECATED_NO_WARNINGS -DIS_BIG_ENDIAN=0")
  set(EXTRA_NEEDED_LIBS winmm.lib ws2_32.lib Iphlpapi.lib)
endif(WIN32)


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

# EXECUTABLE
# Create executables 
add_executable(dispatchtest test/dispatchtest.c) 
target_include_directories(dispatchtest PRIVATE ${CMAKE_SOURCE_DIR}/src) 
target_link_libraries(dispatchtest ${LIBRARIES}) 

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

add_executable(broadcastclient test/broadcastclient.c) 
add_executable(broadcastserver test/broadcastserver.c) 
add_executable(tcppollclient test/tcppollclient.c) 
add_executable(tcppollserver test/tcppollserver.c) 



# I don't know if this is necessary. If it is, it should be duplicated
# for o2client and o2server:
if(WIN32)
  set_target_properties(o2test PROPERTIES 
                               LINK_FLAGS "/SUBSYSTEM:WINDOWS")
  set_property(TARGET o2test PROPERTY WIN32_EXECUTABLE TRUE)
endif(WIN32)

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

set(FRAMEWORK_PATH ${CMAKE_OSX_SYSROOT}/System/Library/Frameworks) 
set(COREAUDIO_LIB "${FRAMEWORK_PATH}/CoreAudio.framework") 
set(LIBRARIES o2_static ${COREAUDIO_LIB} CACHE INTERNAL "") 

#message(STATUS "LO_LIB is ${LO_LIB}") 

# Create executables 
#add_executable(lo_benchmk_server  test/lo_benchmk_server.c) 
#target_include_directories(lo_benchmk_server PRIVATE ${LO_PATH})
#target_link_libraries(lo_benchmk_server ${LO_LIB} ${EXTRA_NEEDED_LIBS}) 

#add_executable(lo_benchmk_client  test/lo_benchmk_client.c) 
#target_include_directories(lo_benchmk_client PRIVATE ${LO_PATH})
#target_link_libraries(lo_benchmk_client ${LO_LIB}  ${EXTRA_NEEDED_LIBS})
