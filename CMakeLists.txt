cmake_minimum_required(VERSION 2.8.7)
project(libevfibers C)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

set(VERSION_MAJOR 0)
set(VERSION_MINOR 2)
set(VERSION_PATCH 0)
set(VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")

include(CheckIncludeFiles)
include(FFIWrap)

aux_source_directory("${CMAKE_CURRENT_SOURCE_DIR}/src" EVFIBERS_SOURCES)
aux_source_directory("${CMAKE_CURRENT_SOURCE_DIR}/coro" CORO_SOURCES)

check_include_files(ev.h HAVE_EV_H)
if(NOT HAVE_EV_H)
	message(FATAL_ERROR "ev.h is not found")
endif(NOT HAVE_EV_H)

check_include_files(ucontext.h HAVE_UCONTEXT_H)
if(HAVE_UCONTEXT_H)
	add_definitions("-DHAVE_UCONTEXT_H")
endif(HAVE_UCONTEXT_H)

find_package(ProtoBufC REQUIRED)

include_directories(
	"${CMAKE_CURRENT_SOURCE_DIR}/include"
	"${CMAKE_CURRENT_BINARY_DIR}/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/coro"
	)

set(WORKER_PROTO "src/worker.proto")
add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/src/worker.pb-c.c"
	       "${CMAKE_CURRENT_BINARY_DIR}/include/evfibers_private/worker.pb-c.h"
	COMMAND mkdir -p "${CMAKE_CURRENT_BINARY_DIR}/include/evfibers_private"
	COMMAND mkdir -p "${CMAKE_CURRENT_BINARY_DIR}/src"
	COMMAND protoc-c -I "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/${WORKER_PROTO}" --c_out "${CMAKE_CURRENT_BINARY_DIR}/src"
	COMMAND cp "${CMAKE_CURRENT_BINARY_DIR}/src/worker.pb-c.h" "${CMAKE_CURRENT_BINARY_DIR}/include/evfibers_private/worker.pb-c.h"
	DEPENDS ${WORKER_PROTO}
	COMMENT "Generating Protobuf-C from ${WORKER_PROTO}"
	)

set(CMAKE_C_FLAGS "-W -Wall -Werror -fno-strict-aliasing ${CMAKE_C_FLAGS}")
set(SOURCES ${EVFIBERS_SOURCES} ${CORO_SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/src/worker.pb-c.c")

add_library(evfibers SHARED ${SOURCES})
set_target_properties(evfibers
	PROPERTIES
	SOVERSION ${VERSION_MAJOR}
	VERSION ${VERSION_STRING}
	)
target_link_libraries(evfibers ev ${PROTOBUFC_LIBRARIES})

add_library(evfibers_static STATIC ${SOURCES})
set_target_properties(evfibers_static  PROPERTIES OUTPUT_NAME evfibers)

aux_source_directory("${CMAKE_CURRENT_SOURCE_DIR}/worker_src" WORKER_SOURCES)
add_executable(fiber_worker "${WORKER_SOURCES}")
target_link_libraries(fiber_worker evfibers)

get_target_property(WORKER_TARGET_PATH fiber_worker LOCATION_Debug)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(WORKER_BIN_PATH "${WORKER_TARGET_PATH}")
else(CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(WORKER_BIN_PATH "/usr/bin/fiber_worker")
endif(CMAKE_BUILD_TYPE STREQUAL "Debug")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/include/evfibers_private/config.h.in"
	"${CMAKE_CURRENT_BINARY_DIR}/include/evfibers_private/config.h")

file(GLOB DEV_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/evfibers/*.h")
set(lualib_gen "${CMAKE_CURRENT_BINARY_DIR}/lualib_gen")
file(MAKE_DIRECTORY ${lualib_gen})
set(lualib_gen_dest "share/lua/5.1/evfibers")
ffi_lua_gen(evfibers_ffi_headers "${DEV_HEADERS}" "${lualib_gen}" "" "" "${lualib_gen_dest}")

subdirs(test)

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
install(TARGETS fiber_worker RUNTIME DESTINATION bin)
install(FILES ${DEV_HEADERS}
	DESTINATION "include/evfibers")
