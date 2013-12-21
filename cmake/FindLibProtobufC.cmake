find_path(LIBPROTOBUFC_INCLUDE_DIR protobuf-c.h
	HINTS $ENV{LIBPROTOBUFC_DIR}
	PATH_SUFFIXES google/protobuf-c
	PATHS /usr/local /usr
)
find_library(LIBPROTOBUFC_LIBRARY
  NAMES protobuf-c
  HINTS $ENV{LIBPROTOBUFC_DIR}
  PATH_SUFFIXES lib
  PATHS /usr/local /usr
)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibProtobufC DEFAULT_MSG LIBPROTOBUFC_LIBRARY LIBPROTOBUFC_INCLUDE_DIR)
mark_as_advanced(LIBPROTOBUFC_INCLUDE_DIR LIBPROTOBUFC_LIBRARY)

find_program(PROTOCC_EXECUTABLE protoc-c
  HINTS $ENV{LIBPROTOBUFC_DIR}
  PATH_SUFFIXES bin
  PATHS /usr/local /usr
)
mark_as_advanced(PROTOCC_EXECUTABLE)
