aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} TEST_SOURCES)
add_executable(evfibers_test ${TEST_SOURCES})
target_link_libraries(evfibers_test ev evfibers check)
enable_testing()
add_test(evfibers_test ${CMAKE_CURRENT_BINARY_DIR}/evfibers_test)
add_custom_target(test COMMAND ${CMAKE_CTEST_COMMAND}
	DEPENDS evfibers_test)
