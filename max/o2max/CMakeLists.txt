cmake_minimum_required(VERSION 3.25)

add_definitions("-DO2_NO_O2DISCOVERY")

project(o2 DESCRIPTION "o2 for max" VERSION "1.0")

# LIB_TYPE is SHARED or STATIC
set(LIB_TYPE STATIC)

if(LIB_TYPE EQUAL SHARED)
	add_definitions("-DBUILD_SHARED_O2MAXLIB")
endif()

# Important in setting O2_EXPORT in o2base.h
add_definitions("-DBUILD_SHARED_LIBS=1")

set(O2_LIB "${CMAKE_CURRENT_SOURCE_DIR}/o2/o2.lib" CACHE PATH "The O2 library")

set(INCLUDEPATH "o2" "o2ensemble")

set(APPLECFLAGS -g -Wall -Wshadow -Wstrict-prototypes -Wno-unused -Wno-parentheses -Wno-switch)

set(WIN32CFLAGS -Wall -W0)

if(UNIX)
	if(APPLE)
		set(O2MAX_CFLAGS "${APPLECFLAGS}")
	else()
		# Max does not support linux
	endif()
else()
	set(O2MAX_CFLAGS "${WIN32CFLAGS}")
endif()

## o2max shared library ##
add_library(o2max ${LIB_TYPE} "o2lib/o2max.c" "o2lib/o2max.h")
target_include_directories(o2max PUBLIC ${INCLUDEPATH})
target_compile_options(o2max PUBLIC ${O2MAX_CFLAGS})
target_link_libraries(o2max PRIVATE Max::Max)
target_link_libraries(o2max PUBLIC ${O2_LIB})

## externals ##
add_subdirectory(o2ensemble)
add_subdirectory(o2send)
add_subdirectory(o2receive)
add_subdirectory(o2property)