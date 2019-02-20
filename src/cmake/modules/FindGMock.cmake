# Tries to find GMock.
#
# Usage of this module as follows:
#
#     find_package(GMock)
#
# Variables used by this module, they can change the default behaviour and need
# to be set before calling find_package:
#
#  GMOCK_PREFIX  Set this variable to the root installation of
#                       GMock if the module has problems finding
#                       the proper installation path.
#
# Variables defined by this module:
#
#  GMOCK_FOUND              System has GMock libs/headers
#  GMOCK_LIBRARIES          The GMock libraries (tcmalloc & profiler)
#  GMOCK_INCLUDE_DIR        The location of GMock headers

find_library(GMOCK NAMES gmock PATHS "${GMOCK_PREFIX}")
find_library(GMOCK_MAIN NAMES gmock_main PATHS "${GMOCK_PREFIX}")

find_path(GMOCK_INCLUDE_DIR NAMES gmock/gmock.h HINTS ${GMOCK_PREFIX}/include)

set(GMOCK_LIBRARIES ${GMOCK} ${GMOCK_MAIN})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  GMock
  DEFAULT_MSG
  GMOCK_LIBRARIES
  GMOCK_INCLUDE_DIR)

mark_as_advanced(
  GMOCK_PREFIX
  GMOCK_LIBRARIES
  GMOCK_INCLUDE_DIR)
