find_path(LIBEIO_INCLUDE_DIR eio.h
	HINTS $ENV{LIBEIO_DIR}
	PATH_SUFFIXES include
	PATHS /usr/local /usr
)
find_library(LIBEIO_LIBRARY
  NAMES eio
  HINTS $ENV{LIBEIO_DIR}
  PATH_SUFFIXES lib
  PATHS /usr/local /usr
)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibEio DEFAULT_MSG LIBEIO_LIBRARY LIBEIO_INCLUDE_DIR)
mark_as_advanced(LIBEIO_INCLUDE_DIR LIBEIO_LIBRARY)
