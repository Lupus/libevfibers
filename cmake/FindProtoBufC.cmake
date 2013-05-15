# - Try to find ProtoBufC
# Once done this will define
#
#  PROTOBUFC_FOUND - system has ProtoBufC
#  PROTOBUFC_INCLUDE_DIR - the ProtoBufC include directory
#  PROTOBUFC_LIBRARIES - Link these to use ProtoBufC
#  PROTOBUFC_DEFINITIONS - Compiler switches required for using ProtoBufC
# Redistribution and use is allowed according to the terms of the BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.
#


if ( PROTOBUFC_INCLUDE_DIR AND PROTOBUFC_LIBRARIES )
   # in cache already
   SET(ProtoBufC_FIND_QUIETLY TRUE)
endif ( PROTOBUFC_INCLUDE_DIR AND PROTOBUFC_LIBRARIES )

if ( WIN32 )
   SET (PROTOBUFC_INCLUDE_DIRS C:/Development/precompiled-protobuf-c/include)
   SET (PROTOBUFC_LIBRARY_DIRS C:/Development/precompiled-protobuf-c/lib)
endif ( WIN32 )

# use pkg-config to get the directories and then use these values
# in the FIND_PATH() and FIND_LIBRARY() calls
if( NOT WIN32 )
  find_package(PkgConfig)

  pkg_check_modules(PROTOBUFC protobuf-c-0.15)

  set(PROTOBUFC_DEFINITIONS ${PROTOBUFC_CFLAGS})
endif( NOT WIN32 )

FIND_PATH(PROTOBUFC_INCLUDE_DIR 
  NAMES google/protobuf-c/protobuf-c.h
  PATHS
  ${PROTOBUFC_INCLUDE_DIRS}
)

FIND_LIBRARY(PROTOBUFC_LIBRARIES 
  NAMES protobuf-c
  PATHS
  ${PROTOBUFC_LIBRARY_DIRS}
)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(ProtoBufC DEFAULT_MSG PROTOBUFC_INCLUDE_DIR PROTOBUFC_LIBRARIES )

# show the PROTOBUFC_INCLUDE_DIR and PROTOBUFC_LIBRARIES variables only in the advanced view
MARK_AS_ADVANCED(PROTOBUFC_INCLUDE_DIR PROTOBUFC_LIBRARIES )

