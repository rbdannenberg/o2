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


add_definitions("-DO2_NO_O2DISCOVERY")


# create name of project
project(o2pd DESCRIPTION "o2pd library"
	     VERSION "1.0")

set(PD_INCLUDE_PATH "../../pd/pd-0.54-1/src" CACHE PATH
    "Where to find Pd include files")

set(O2_OPT_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../Release/libo2_static.a" CACHE PATH
    "The O2 optimized (release) library")  
set(O2_DBG_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../Debug/libo2_static.a" CACHE PATH
    "The O2 debug library")  


# Create needed variables
set(COMMON_PD_FLAGS -DPD -g -Wall -W -Wshadow -Wstrict-prototypes
    -Wno-unused -Wno-parentheses -Wno-switch)

set(LINUXCFLAGS ${COMMON_PD_FLAGS})

set(APPLECFLAGS ${COMMON_PD_FLAGS})      
 
set(WIN32CFLAGS ${COMMON_PD_FLAGS})      
 
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
  set(O2PD_FLAGS "${WIN32CFLAGS}")
  set(DYLIB_SUFFIX ".pd_win32")  
 endif()


# Create libo2pd shared library
add_library(libo2pd SHARED o2pd.c)
target_include_directories(libo2pd PUBLIC ${INCLUDEPATHS})
target_compile_options(libo2pd PUBLIC ${O2PD_CFLAGS})
target_link_options(libo2pd PRIVATE -undefined suppress -flat_namespace)  
target_link_whole_library(libo2pd ${O2_OPT_LIB} ${O2_DBG_LIB})
# target_link_libraries(libo2pd PUBLIC ${O2_OPT_LIB})
target_link_libraries(libo2pd PUBLIC ${EXTRA_LIBS})
message(STATUS "libo2pd will be linked with EXTRA_LIBS ${EXTRA_LIBS}")

# To avoid the prefix of lib twice to the name:
set_target_properties(libo2pd PROPERTIES OUTPUT_NAME "o2pd")

# Create o2_ensemble, o2_receive, o2_send and o2_property libraries
## o2ensemble ##
add_library(o2ensemble MODULE o2ensemble.c)
target_include_directories(o2ensemble PUBLIC ${INCLUDEPATHS})
target_compile_options(o2ensemble PUBLIC ${O2PD_CFLAGS})
target_link_options(o2ensemble PRIVATE -undefined suppress -flat_namespace)   
target_link_libraries(o2ensemble libo2pd)
set_target_properties(o2ensemble PROPERTIES OUTPUT_NAME "o2ensemble"
                                            SUFFIX "${DYLIB_SUFFIX}"
                                            PREFIX "")

## o2receive ##
add_library(o2receive MODULE o2receive.c)
target_include_directories(o2receive PUBLIC ${INCLUDEPATHS})
target_compile_options(o2receive PUBLIC ${O2PD_CFLAGS})
target_link_options(o2receive PRIVATE -undefined suppress -flat_namespace)   
target_link_libraries(o2receive libo2pd)
set_target_properties(o2receive PROPERTIES OUTPUT_NAME "o2receive"
                                           SUFFIX "${DYLIB_SUFFIX}"
                                           PREFIX "")

## o2send ##
add_library(o2send MODULE o2send.c)
target_include_directories(o2send PUBLIC ${INCLUDEPATHS})
target_compile_options(o2send PUBLIC ${O2PD_CFLAGS})
target_link_options(o2send PRIVATE -undefined suppress -flat_namespace)    
target_link_libraries(o2send libo2pd)
set_target_properties(o2send PROPERTIES OUTPUT_NAME "o2send"
                                        SUFFIX "${DYLIB_SUFFIX}"
                                        PREFIX "")

## o2property ##
add_library(o2property MODULE o2property.c)
target_include_directories(o2property PUBLIC ${INCLUDEPATHS})
target_compile_options(o2property PUBLIC ${O2PD_CFLAGS})
target_link_options(o2property PRIVATE -undefined suppress -flat_namespace)    
target_link_libraries(o2property libo2pd)
set_target_properties(o2property PROPERTIES OUTPUT_NAME "o2property"
                                            SUFFIX "${DYLIB_SUFFIX}"
                                            PREFIX "")
