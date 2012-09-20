cmake_minimum_required(VERSION 2.6)
project(libevfibers C)

set(VERSION_MAJOR 0)
set(VERSION_MINOR 1)
set(VERSION_PATCH 0)
set(VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")

set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Small C fiber library that uses libev based event loop and libcoro based coroutine context switching.")
set(CPACK_PACKAGE_VENDOR "Konstantin Olkhovskiy")
set(CPACK_PACKAGE_VERSION_MAJOR ${VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${VERSION_PATCH})
set(CPACK_GENERATOR "TGZ")
include(CPack)

aux_source_directory("${CMAKE_CURRENT_SOURCE_DIR}/src" EVFIBERS_SOURCES)
aux_source_directory("${CMAKE_CURRENT_SOURCE_DIR}/coro" CORO_SOURCES)

include_directories(
	"${CMAKE_CURRENT_SOURCE_DIR}/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/coro"
	)

set(SOURCES ${EVFIBERS_SOURCES} ${CORO_SOURCES})

add_library(evfibers SHARED ${SOURCES})
add_library(evfibers_static STATIC ${SOURCES})
set_target_properties(evfibers_static  PROPERTIES OUTPUT_NAME evfibers)

install(TARGETS evfibers
	RUNTIME DESTINATION usr/bin
	LIBRARY DESTINATION usr/lib
	ARCHIVE DESTINATION usr/lib
	COMPONENT runtime)
install(TARGETS evfibers_static
	RUNTIME DESTINATION usr/bin
	LIBRARY DESTINATION usr/lib
	ARCHIVE DESTINATION usr/lib
	COMPONENT dev)
file(GLOB DEV_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/evfibers/*.h")
install(FILES ${DEV_HEADERS}
	DESTINATION "usr/include/evfibers"
	COMPONENT dev)
