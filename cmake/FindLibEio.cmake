find_path(LIBEIO_INCLUDE_DIR eio.h
	HINTS $ENV{LIBEIO_DIR}
	PATH_SUFFIXES include
	PATHS /usr/local /usr
)
find_library(LIBEIO_LIBRARY
  NAMES ev-eio eio
  HINTS $ENV{LIBEIO_DIR}
  PATH_SUFFIXES lib
  PATHS /usr/local /usr
)
check_library_exists(${LIBEIO_LIBRARY} eio_custom  "" EIO_CUSTOM_IS_PRESENT)
if (NOT EIO_CUSTOM_IS_PRESENT)
	message(FATAL_ERROR "symbol eio_custom is not found in ${LIBEIO_LIBRARY}")
endif()
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibEio DEFAULT_MSG LIBEIO_LIBRARY LIBEIO_INCLUDE_DIR)
mark_as_advanced(LIBEIO_INCLUDE_DIR LIBEIO_LIBRARY)
