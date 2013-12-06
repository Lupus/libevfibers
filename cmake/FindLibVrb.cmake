find_path(LIBVRB_INCLUDE_DIR vrb.h
	HINTS $ENV{LIBVRB_DIR}
	PATH_SUFFIXES include
	PATHS /usr/local /usr
)
find_library(LIBVRB_LIBRARY
  NAMES vrb
  HINTS $ENV{LIBVRB_DIR}
  PATH_SUFFIXES lib
  PATHS /usr/local /usr
)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibVrb DEFAULT_MSG LIBVRB_LIBRARY LIBVRB_INCLUDE_DIR)
mark_as_advanced(LIBVRB_INCLUDE_DIR LIBVRB_LIBRARY)
