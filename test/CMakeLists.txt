find_package(Threads REQUIRED)

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} TEST_SOURCES)
set (FBR_TEST_LIBS evfibers check m)
if (APPLE)
	# don't want to test mknod - requires super-user privileges
	add_definitions(-DFBR_TEST_NO_MKNOD)
else(APPLE)
	# OSX doesn't use librt, while linux boxes do
	list(APPEND FBR_TEST_LIBS rt)
	# Dependency of libcheck on Deabin/Ubuntu
	list(APPEND FBR_TEST_LIBS subunit)
endif(APPLE)

add_executable(evfibers_test ${TEST_SOURCES})
if(THREADS_HAVE_PTHREAD_ARG)
	target_compile_options(evfibers_test PUBLIC "-pthread")
endif()
if(CMAKE_THREAD_LIBS_INIT)
	list(APPEND FBR_TEST_LIBS ${CMAKE_THREAD_LIBS_INIT})
endif()

target_link_libraries(evfibers_test ${FBR_TEST_LIBS})
enable_testing()
add_test(evfibers_test ${CMAKE_CURRENT_BINARY_DIR}/evfibers_test)
add_custom_target(tests COMMAND ${CMAKE_CTEST_COMMAND}
	DEPENDS evfibers_test)
