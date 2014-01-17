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
	find_package(LibEio)
	find_package(Threads)
endif(WANT_EIO)
check_include_files(ucontext.h HAVE_UCONTEXT_H)
if(HAVE_UCONTEXT_H)
	add_definitions("-DHAVE_UCONTEXT_H")
endif(HAVE_UCONTEXT_H)

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
set(CMAKE_C_FLAGS "-W -Wall -Werror -fno-strict-aliasing ${LTO_FLAGS} ${CMAKE_C_FLAGS}")
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

set(EVFIBERS_EMBED_LIBS evfibers_static
	${LIBEV_LIBRARY}
	${LIBEIO_LIBRARY}
	${CMAKE_THREAD_LIBS_INIT}
	PARENT_SCOPE)

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
