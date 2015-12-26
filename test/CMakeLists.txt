aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} TEST_SOURCES)
if (APPLE)
	# don't want to test mknod - requires super-user privileges
	add_definitions(-DFBR_TEST_NO_MKNOD)
else(APPLE)
	# OSX doesn't use librt, while linux boxes do
	set(FBR_TEST_RTLIB rt)
endif(APPLE)

add_executable(evfibers_test ${TEST_SOURCES})
target_link_libraries(evfibers_test evfibers check m ${FBR_TEST_RTLIB})
enable_testing()
add_test(evfibers_test ${CMAKE_CURRENT_BINARY_DIR}/evfibers_test)
add_custom_target(tests COMMAND ${CMAKE_CTEST_COMMAND}
	DEPENDS evfibers_test)
