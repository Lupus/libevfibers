cmake_minimum_required(VERSION 2.8.7)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	# We are building as stand-alone project
	project(libevfibers C)
else()
	# We are building as part of other project
	set(EVFIBERS_EMBED TRUE)
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

if(NOT CMAKE_BUILD_TYPE)
	message(STATUS "No build type selected, defaulting to RelWithDebInfo")
	set(CMAKE_BUILD_TYPE "RelWithDebInfo")
endif(NOT CMAKE_BUILD_TYPE)

set(VERSION_MAJOR 0)
set(VERSION_MINOR 2)
set(VERSION_PATCH 0)
set(VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")

include(CheckIncludeFiles)

if(NOT DEFINED WANT_EIO)
	message(STATUS "WANT_EIO flag not specified, defaulting to TRUE")
	set(WANT_EIO TRUE)
endif(NOT DEFINED WANT_EIO)

aux_source_directory("${CMAKE_CURRENT_SOURCE_DIR}/src" EVFIBERS_SOURCES)
aux_source_directory("${CMAKE_CURRENT_SOURCE_DIR}/coro" CORO_SOURCES)

find_package(LibEv REQUIRED)
if(WANT_EIO)
	if(WANT_EMBEDDED_EIO)
		include(ExternalProject)
		ExternalProject_Add(
			libeio
			PREFIX "${CMAKE_CURRENT_BINARY_DIR}/eio"
			CVS_REPOSITORY :pserver:anonymous@cvs.schmorp.de/schmorpforge
			CVS_MODULE libeio
			CVS_TAG -r rel-4_19
			UPDATE_COMMAND ""
			CONFIGURE_COMMAND ./configure --prefix=${CMAKE_CURRENT_BINARY_DIR}/eio
			BUILD_IN_SOURCE 1
			)
		ExternalProject_Add_Step(libeio autogen
			COMMAND "./autogen.sh"
				DEPENDEES update
				DEPENDERS configure
			WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/eio/src/libeio"
		)
		ExternalProject_Add_Step(libeio autoconf
			COMMAND "autoconf"
				DEPENDEES autogen
				DEPENDERS configure
			WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/eio/src/libeio"
		)
		set(LIBEIO_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/eio/include")
		set(LIBEIO_LIBRARY "-L${CMAKE_CURRENT_BINARY_DIR}/eio/lib -leio")
	else(WANT_EMBEDDED_EIO)
		find_package(LibEio)
	endif(WANT_EMBEDDED_EIO)
	find_package(Threads)
endif(WANT_EIO)

if(WANT_ASAN)
	# Force ucontext.h as libcoro backend so as to better interact with ASan
	add_definitions(-DCORO_UCONTEXT)
endif(WANT_ASAN)

if(WANT_VALGRIND)
	check_include_files(valgrind/valgrind.h HAVE_VALGRIND_H)
	if (NOT HAVE_VALGRIND_H)
		message(FATAL_ERROR "cannot find valgrind.h")
	endif(NOT HAVE_VALGRIND_H)
endif(WANT_VALGRIND)

include_directories(
	"${CMAKE_CURRENT_SOURCE_DIR}/include"
	"${CMAKE_CURRENT_BINARY_DIR}/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/coro"
	${LIBEV_INCLUDE_DIR}
	${LIBEIO_INCLUDE_DIR}
	)

if(WANT_LTO)
	set(LTO_FLAGS "-flto")
endif(WANT_LTO)
if(WANT_ASAN)
	set(ASAN_FLAGS "-fsanitize=address")
endif(WANT_ASAN)
set(CMAKE_C_FLAGS "-W -Wall -Werror -fno-strict-aliasing ${LTO_FLAGS} ${ASAN_FLAGS} ${CMAKE_C_FLAGS}")
set(SOURCES ${EVFIBERS_SOURCES} ${CORO_SOURCES})

add_library(evfibers SHARED ${SOURCES})
set_target_properties(evfibers
	PROPERTIES
	SOVERSION ${VERSION_MAJOR}
	VERSION ${VERSION_STRING}
	)
target_link_libraries(evfibers
	${LIBEV_LIBRARY}
	${LIBEIO_LIBRARY}
	${CMAKE_THREAD_LIBS_INIT})

add_library(evfibers_static STATIC ${SOURCES})
set_target_properties(evfibers_static  PROPERTIES
	OUTPUT_NAME evfibers
	COMPILE_FLAGS "-fPIC")

if (EVFIBERS_EMBED)
	set(EVFIBERS_EMBED_LIBS evfibers_static
		${LIBEV_LIBRARY}
		${LIBEIO_LIBRARY}
		${CMAKE_THREAD_LIBS_INIT}
		PARENT_SCOPE)
endif(EVFIBERS_EMBED)

add_executable(fiber_bench_buffer "${CMAKE_CURRENT_SOURCE_DIR}/bench/buffer.c")
target_link_libraries(fiber_bench_buffer evfibers ${CMAKE_THREAD_LIBS_INIT})

# Variables for config.h
if(WANT_EIO AND THREADS_FOUND AND LIBEIO_FOUND)
	set(FBR_EIO_ENABLED TRUE)
	message(STATUS "libeio support has been ENABLED")
else(WANT_EIO AND THREADS_FOUND AND LIBEIO_FOUND)
	message(STATUS "libeio support has been DISABLED")
endif(WANT_EIO AND THREADS_FOUND AND LIBEIO_FOUND)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/include/evfibers/config.h.in"
	"${CMAKE_CURRENT_BINARY_DIR}/include/evfibers/config.h")

subdirs(test)

if(NOT EVFIBERS_EMBED)
	install(TARGETS evfibers
		RUNTIME DESTINATION bin
		LIBRARY DESTINATION lib
		ARCHIVE DESTINATION lib
		)
	install(TARGETS evfibers_static
		RUNTIME DESTINATION bin
		LIBRARY DESTINATION lib
		ARCHIVE DESTINATION lib
		)
	file(GLOB DEV_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/evfibers/*.h")
	file(GLOB DEV_HEADERS2 "${CMAKE_CURRENT_BINARY_DIR}/include/evfibers/*.h")
	install(FILES ${DEV_HEADERS} ${DEV_HEADERS2}
		DESTINATION "include/evfibers")
endif(NOT EVFIBERS_EMBED)
