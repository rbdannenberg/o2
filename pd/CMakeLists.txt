# CMakeLists.txt for o2pd extensions
#
# Chukwuemeka Nkama and Roger B. Dannenberg
# Dec 2023
#
# This is intended to build Pd extensions for O2.
# Directory organization:
#     o2 -- the o2 working directory
#     o2/src -- o2 include files and sources
#     o2/pd -- where to find this file and Pd extension sources
#              (does not have to be named pd, e.g. you can copy
#              everything to o2/pd2 for temporary tests or development)
#     pd/pd-0.54-1 -- assumed location for Pd sources, but the
#              directory can be anywhere by setting PD_INCLUDE_PATH
# After building, libraries should be moved or copied to your Pd
# "search path for objects" directory. There are 4 libraries you need:
#     libo2pd - O2 library shared by all O2Pd objects
#     o2ensemble -- Pd external
#     o2property -- Pd external
#     o2received -- Pd external
#     o2send -- Pd external
# Open the o2-help.pd file (which is in this directory) for help
#     and examples for using O2Pd externals.


# cmake minimum version
cmake_minimum_required(VERSION 3.25)

macro(target_link_whole_library target optlibname dbglibname)
  target_link_libraries(${target} PRIVATE
          optimized "$<LINK_LIBRARY:WHOLE_ARCHIVE,${optlibname}>"
              debug "$<LINK_LIBRARY:WHOLE_ARCHIVE,${dbglibname}>")
endmacro()

# Set compilation definition for source files
add_definitions("-DO2_NO_O2DISCOVERY")


# Define BUILD_SHARED_LIBS
option(BUILD_SHARED_LIBS "Build Shared Libraries" OFF)

# This definition is important in setting O2_EXPORT in o2base.h
if (BUILD_SHARED_LIBS)
  add_definitions("-DBUILD_SHARED_LIBS=1")
endif()

# create name of project
project(o2pd DESCRIPTION "o2pd library"
	     VERSION "1.0")


# set paths to required files and libraries
set(PD_INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../rbd/pd-0.54-1/src" CACHE PATH
    "Where to find Pd include files")
set(O2_OPT_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../build/Debug/o2.lib" CACHE PATH
    "The O2 optimized (release) library")  
set(O2_DBG_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../build/Debug/o2.lib" CACHE PATH
    "The O2 debug library")  
# comment out the below line for UNIX systems (it is important for WINDOWS)
# Omitting the below on WINDOWS will lead to a lot of unresolved references
set(PD_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../../../rbd/pd-0.54-1/pd-0.54.1/bin/pd.lib" CACHE PATH 
 "Pd's optimized (release) library")

# Create needed variables
set(COMMON_PD_FLAGS -DPD -g -Wall -W0 -Wshadow -Wstrict-prototypes
    -Wno-unused -Wno-parentheses -Wno-switch)

set(WINDOWS_PD_FLAGS -DPD -Wall -W0) # This will work for windows; the above will cause an error

set(LINUXCFLAGS ${COMMON_PD_FLAGS})

set(APPLECFLAGS ${COMMON_PD_FLAGS})      
 
set(WIN32CFLAGS ${WINDOWS_PD_FLAGS})      
 
set(INCLUDEPATHS ../src/ "${PD_INCLUDE_PATH}")


if(UNIX)
  if(APPLE)
    set(O2PD_CFLAGS "${APPLECFLAGS}")
    find_library(COREAUDIO CoreAudio)
    set(EXTRA_LIBS "${COREAUDIO}" stdc++)
    set(DYLIB_SUFFIX ".pd_darwin") 
  else() # linux
    set(O2PD_CFLAGS "${LINUXCFLAGS}") 
    set(DYLIB_SUFFIX ".pd_linux") 
  endif()
else() # windows
  set(O2PD_CFLAGS "${WIN32CFLAGS}")
  set(DYLIB_SUFFIX ".dll")  
 endif()


# Create libo2pd shared library
add_library(libo2pd SHARED o2pd.c)
target_include_directories(libo2pd PUBLIC ${INCLUDEPATHS})
target_compile_options(libo2pd PUBLIC ${O2PD_CFLAGS})

if (APPLE)
  target_link_options(libo2pd PRIVATE -undefined suppress -flat_namespace)  
  
  # Linking libo2pd to o2 using WHOLE_ARCHIVE causes errors on Windows 
  # when trying to link to PD_LIB after; hence, the conditional statement
  # For context, the linking on APPLE was done with a static library 
  target_link_whole_library(libo2pd ${O2_OPT_LIB} ${O2_DBG_LIB})

  # EXTRA_LIBS are needed for APPLE but not Windows and Linux
  target_link_libraries(libo2pd PUBLIC ${EXTRA_LIBS})
  message(STATUS "libo2pd will be linked with EXTRA_LIBS ${EXTRA_LIBS}")
else()
  # The below should work for WINDOWS and LINUX since we use shared libraries
  target_link_libraries(libo2pd ${PD_LIB}) # This line has not been tested on Linux!!
  target_link_libraries(libo2pd ${O2_OPT_LIB} ${O2_DBG_LIB})
endif()

# To avoid the prefix of lib twice to the name:
set_target_properties(libo2pd PROPERTIES OUTPUT_NAME "o2pd")

# Create o2_ensemble, o2_receive, o2_send and o2_property libraries
## o2ensemble ##
add_library(o2ensemble MODULE o2ensemble.c)
target_include_directories(o2ensemble PUBLIC ${INCLUDEPATHS})
target_compile_options(o2ensemble PUBLIC ${O2PD_CFLAGS})
if(UNIX)
  if(APPLE)
    # This fails on WINDOWS but is required for APPLE
    target_link_options(o2ensemble PRIVATE -undefined suppress -flat_namespace)   
  endif()
endif()
target_link_libraries(o2ensemble libo2pd)
set_target_properties(o2ensemble PROPERTIES OUTPUT_NAME "o2ensemble"
                                            SUFFIX "${DYLIB_SUFFIX}"
                                            PREFIX "")

## o2receive ##
add_library(o2receive MODULE o2receive.c)
target_include_directories(o2receive PUBLIC ${INCLUDEPATHS})
target_compile_options(o2receive PUBLIC ${O2PD_CFLAGS})
if(UNIX)
  if(APPLE)
    # This fails on WINDOWS but is required for APPLE
    target_link_options(o2receive PRIVATE -undefined suppress -flat_namespace)   
  endif()
endif()
target_link_libraries(o2receive libo2pd)
set_target_properties(o2receive PROPERTIES OUTPUT_NAME "o2receive"
                                           SUFFIX "${DYLIB_SUFFIX}"
                                           PREFIX "")

## o2send ##
add_library(o2send MODULE o2send.c)
target_include_directories(o2send PUBLIC ${INCLUDEPATHS})
target_compile_options(o2send PUBLIC ${O2PD_CFLAGS})
if(UNIX)
  if(APPLE)
    # This fails on WINDOWS but is required for APPLE
    target_link_options(o2send PRIVATE -undefined suppress -flat_namespace)    
  endif()
endif()
target_link_libraries(o2send libo2pd)
set_target_properties(o2send PROPERTIES OUTPUT_NAME "o2send"
                                        SUFFIX "${DYLIB_SUFFIX}"
                                        PREFIX "")

## o2property ##
add_library(o2property MODULE o2property.c)
target_include_directories(o2property PUBLIC ${INCLUDEPATHS})
target_compile_options(o2property PUBLIC ${O2PD_CFLAGS})
if(UNIX)
  if(APPLE)
  # This fails on WINDOWS but is required for APPLE
  target_link_options(o2property PRIVATE -undefined suppress -flat_namespace)    
  endif()
endif()
target_link_libraries(o2property libo2pd)
set_target_properties(o2property PROPERTIES OUTPUT_NAME "o2property"
                                            SUFFIX "${DYLIB_SUFFIX}"
                                            PREFIX "")
