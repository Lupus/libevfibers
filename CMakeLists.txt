cmake_minimum_required(VERSION 2.6)
project(libevfibers C)

aux_source_directory("${CMAKE_CURRENT_SOURCE_DIR}/src" EVFIBERS_SOURCES)
aux_source_directory("${CMAKE_CURRENT_SOURCE_DIR}/include/evfibers" EVFIBERS_HEADERS)
aux_source_directory("${CMAKE_CURRENT_SOURCE_DIR}/include/evfibers_private" EVFIBERS_PRIVATE_HEADERS)
aux_source_directory("${CMAKE_CURRENT_SOURCE_DIR}/coro" CORO_SOURCES)

include_directories(
	"${CMAKE_CURRENT_SOURCE_DIR}/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/coro"
	)

add_library(evfibers SHARED
	${EVFIBERS_SOURCES}
	${EVFIBERS_HEADERS}
	${EVFIBERS_PRIVATE_HEADERS}
	${CORO_SOURCES}
	)
