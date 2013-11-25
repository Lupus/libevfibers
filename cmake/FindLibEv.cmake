find_path(LIBEV_INCLUDE_DIR ev.h
	HINTS $ENV{LIBEV_DIR}
	PATH_SUFFIXES include
	PATHS /usr/local /usr
)
find_library(LIBEV_LIBRARY
  NAMES ev
  HINTS $ENV{LIBEV_DIR}
  PATH_SUFFIXES lib
  PATHS /usr/local /usr
)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibEv DEFAULT_MSG LIBEV_LIBRARY LIBEV_INCLUDE_DIR)
mark_as_advanced(LIBEV_INCLUDE_DIR LIBEV_LIBRARY)
