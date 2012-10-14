cmake_minimum_required(VERSION 2.6)
project(libevfibers C)

set(VERSION_MAJOR 0)
set(VERSION_MINOR 1)
set(VERSION_PATCH 0)
set(VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")

aux_source_directory("${CMAKE_CURRENT_SOURCE_DIR}/src" EVFIBERS_SOURCES)
aux_source_directory("${CMAKE_CURRENT_SOURCE_DIR}/coro" CORO_SOURCES)

add_definitions("-DHAVE_UCONTEXT_H")

include_directories(
	"${CMAKE_CURRENT_SOURCE_DIR}/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/coro"
	)

set(CMAKE_C_FLAGS "-W -Wall -Werror -fno-strict-aliasing ${CMAKE_C_FLAGS}")
set(SOURCES ${EVFIBERS_SOURCES} ${CORO_SOURCES})

add_library(evfibers SHARED ${SOURCES})
set_target_properties(evfibers
	PROPERTIES
	SOVERSION ${VERSION_MAJOR}
	VERSION ${VERSION_STRING}
	)
target_link_libraries(evfibers ev)

add_library(evfibers_static STATIC ${SOURCES})
set_target_properties(evfibers_static  PROPERTIES OUTPUT_NAME evfibers)

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
file(GLOB DEV_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/evfibers/*.h")
install(FILES ${DEV_HEADERS}
	DESTINATION "include/evfibers")
