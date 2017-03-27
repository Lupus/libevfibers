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
set(VERSION_MINOR 4)
set(VERSION_PATCH 1)
set(VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")

include(CheckIncludeFiles)
include(CheckCCompilerFlag)

get_property(LIB64 GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS)

if ("${LIB64}" STREQUAL "TRUE")
    set(LIBSUFFIX 64)
else("${LIB64}" STREQUAL "TRUE")
    set(LIBSUFFIX "")
endif("${LIB64}" STREQUAL "TRUE")

set(INSTALL_LIB_DIR lib${LIBSUFFIX} CACHE PATH "Installation directory for libraries")
mark_as_advanced(INSTALL_LIB_DIR)

if(NOT DEFINED WANT_EIO)
	message(STATUS "WANT_EIO flag not specified, defaulting to TRUE")
	set(WANT_EIO TRUE)
endif(NOT DEFINED WANT_EIO)

aux_source_directory("${CMAKE_CURRENT_SOURCE_DIR}/src" EVFIBERS_SOURCES)
aux_source_directory("${CMAKE_CURRENT_SOURCE_DIR}/coro" CORO_SOURCES)

# OSX-related checks - linking, using ucontext, etc. is slightly different than
# on linux
if(APPLE)
	# def needed to get ucontext support
	set(CMAKE_REQUIRED_DEFINITIONS "${CMAKE_REQUIRED_DEFINITIONS} -D_XOPEN_SOURCE")
	add_definitions(-D_XOPEN_SOURCE)
	# ucontext is labeled deprecated, so make sure -Werror doesn't kill the
	# compile
	set(CMAKE_C_FLAGS "-Wno-error=deprecated-declarations ${CMAKE_C_FLAGS}")
	# OSX uses MAP_ANON for mmap while linux uses MAP_ANONYMOUS
	set(FBR_MAP_ANON_FLAG MAP_ANON)
	set(EIO_LD_WHOLE_ARCHIVE -Wl,-all_load)
	set(EIO_LD_NOWHOLE_ARCHIVE )
	# use of swapcontext hits false positives on OSX when using either
	# address-sanitizer or stack protector (stack protector is on by default in
	# clang). Disable them.
	if(WANT_ASAN)
		message(WARNING "Address sanitizer causes false positive errors for makecontext/swapcontext on OSX, disabling...")
		set(WANT_ASAN FALSE)
	endif(WANT_ASAN)
	CHECK_C_COMPILER_FLAG("-fno-stack-protector" FBR_HAS_NO_STACK_PROTECT)
	if(FBR_HAS_NO_STACK_PROTECT)
		set(CMAKE_C_FLAGS "-fno-stack-protector ${CMAKE_C_FLAGS}")
	endif(FBR_HAS_NO_STACK_PROTECT)
else(APPLE)
	set(FBR_MAP_ANON_FLAG MAP_ANONYMOUS)
	set(EIO_LD_WHOLE_ARCHIVE -Wl,-whole-archive)
	set(EIO_LD_NOWHOLE_ARCHIVE -Wl,-no-whole-archive)
endif(APPLE)

# need to provide this define on the command line instead of config.h so
# we don't have to modify the embedded libcoro.
check_include_files(ucontext.h HAVE_UCONTEXT_H)
if(HAVE_UCONTEXT_H)
	add_definitions(-DHAVE_UCONTEXT_H)
endif(HAVE_UCONTEXT_H)

include(ExternalProject)
ExternalProject_Add(
	libuv
	PREFIX "${CMAKE_CURRENT_BINARY_DIR}/libuv"
	GIT_REPOSITORY https://github.com/libuv/libuv.git
	GIT_TAG v1.11.0
	UPDATE_COMMAND ""
	CONFIGURE_COMMAND CFLAGS=-fpic ./configure --enable-static --disable-shared --prefix=${CMAKE_CURRENT_BINARY_DIR}/libuv
	BUILD_IN_SOURCE 1
	)
ExternalProject_Add_Step(libuv autogen
	COMMAND "./autogen.sh"
		DEPENDEES update
		DEPENDERS configure
	WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/libuv/src/libuv"
)
ExternalProject_Add_Step(libuv mvembedheader
        COMMAND mv ${CMAKE_CURRENT_BINARY_DIR}/libuv/include/uv.h ${CMAKE_CURRENT_BINARY_DIR}/include/evfibers/
	COMMAND mv ${CMAKE_CURRENT_BINARY_DIR}/libuv/include/uv-errno.h ${CMAKE_CURRENT_BINARY_DIR}/include/evfibers/
	COMMAND mv ${CMAKE_CURRENT_BINARY_DIR}/libuv/include/uv-linux.h ${CMAKE_CURRENT_BINARY_DIR}/include/evfibers/
	COMMAND mv ${CMAKE_CURRENT_BINARY_DIR}/libuv/include/uv-threadpool.h ${CMAKE_CURRENT_BINARY_DIR}/include/evfibers/
	COMMAND mv ${CMAKE_CURRENT_BINARY_DIR}/libuv/include/uv-unix.h ${CMAKE_CURRENT_BINARY_DIR}/include/evfibers/
	COMMAND mv ${CMAKE_CURRENT_BINARY_DIR}/libuv/include/uv-version.h ${CMAKE_CURRENT_BINARY_DIR}/include/evfibers/
	DEPENDEES install
)
set(LIBUV_LIBRARY
	"-L${CMAKE_CURRENT_BINARY_DIR}/libuv/lib"
	"${EIO_LD_WHOLE_ARCHIVE}" "-luv" "${EIO_LD_NOWHOLE_ARCHIVE}"
	-lrt -lpthread -lnsl -ldl
	"${CMAKE_THREAD_LIBS_INIT}")
set(LIBUV_FOUND TRUE)

if(WANT_ASAN)
	# Force ucontext.h as libcoro backend so as to better interact with ASan
	add_definitions(-DCORO_UCONTEXT)
	set(ASAN_FLAGS "-fsanitize=address")
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
	)

if(WANT_LTO)
	set(LTO_FLAGS "-flto")
endif(WANT_LTO)
set(CMAKE_C_FLAGS "-W -Wall -Werror -fno-strict-aliasing ${LTO_FLAGS} ${ASAN_FLAGS} ${CMAKE_C_FLAGS}")
set(SOURCES ${EVFIBERS_SOURCES} ${CORO_SOURCES})

add_library(evfibers SHARED ${SOURCES})
set_target_properties(evfibers
	PROPERTIES
	SOVERSION ${VERSION_MAJOR}
	VERSION ${VERSION_STRING}
	)
target_link_libraries(evfibers
	${LIBUV_LIBRARY}
	${CMAKE_THREAD_LIBS_INIT})
add_dependencies(evfibers libuv)

add_library(evfibers_static STATIC ${SOURCES})
set_target_properties(evfibers_static  PROPERTIES
	OUTPUT_NAME evfibers
	COMPILE_FLAGS "-fPIC")

if(EVFIBERS_EMBED)
	set(EVFIBERS_EMBED_LIBS evfibers_static
		${LIBUV_LIBRARY}
		${CMAKE_THREAD_LIBS_INIT}
		PARENT_SCOPE)
endif(EVFIBERS_EMBED)

add_executable(fiber_bench_condvar "${CMAKE_CURRENT_SOURCE_DIR}/bench/condvar.c")
target_link_libraries(fiber_bench_condvar evfibers ${CMAKE_THREAD_LIBS_INIT})

# Variables for config.h
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/include/evfibers/config.h.in"
	"${CMAKE_CURRENT_BINARY_DIR}/include/evfibers/config.h")

# provide pkg-config file
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/libevfibers.pc.in
  ${CMAKE_CURRENT_BINARY_DIR}/libevfibers.pc @ONLY
)
install(
  FILES
    ${CMAKE_CURRENT_BINARY_DIR}/libevfibers.pc
  DESTINATION
    "${INSTALL_LIB_DIR}/pkgconfig"
)

subdirs(test)

if(NOT EVFIBERS_EMBED)
	install(TARGETS evfibers
		RUNTIME DESTINATION bin
		LIBRARY DESTINATION ${INSTALL_LIB_DIR}
		ARCHIVE DESTINATION ${INSTALL_LIB_DIR}
		)
	install(TARGETS evfibers_static
		RUNTIME DESTINATION bin
		LIBRARY DESTINATION ${INSTALL_LIB_DIR}
		ARCHIVE DESTINATION ${INSTALL_LIB_DIR}
		)
	file(GLOB DEV_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/evfibers/*.h")
	file(GLOB DEV_HEADERS2 "${CMAKE_CURRENT_BINARY_DIR}/include/evfibers/*.h")
	install(FILES ${DEV_HEADERS} ${DEV_HEADERS2}
		DESTINATION "include/evfibers")
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/evfibers/uv.h
                DESTINATION "include/evfibers")
endif(NOT EVFIBERS_EMBED)
