set( CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -std=c++14" )

find_package(Boost COMPONENTS filesystem system REQUIRED)
if(Boost_FOUND)
  message(STATUS "Found Boost headers in: " ${Boost_INCLUDE_DIRS})
  include_directories(${Boost_INCLUDE_DIRS})
endif()

include_directories(${GTEST_INCLUDE_DIR})

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../abseil-cpp")
link_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../abseil-cpp/absl/algorithm"
  "${CMAKE_CURRENT_SOURCE_DIR}/../abseil-cpp/absl/base"
  "${CMAKE_CURRENT_SOURCE_DIR}/../abseil-cpp/absl/container"
  "${CMAKE_CURRENT_SOURCE_DIR}/../abseil-cpp/absl/debugging"
  "${CMAKE_CURRENT_SOURCE_DIR}/../abseil-cpp/absl/hash"
  "${CMAKE_CURRENT_SOURCE_DIR}/../abseil-cpp/absl/memory"
  "${CMAKE_CURRENT_SOURCE_DIR}/../abseil-cpp/absl/meta"
  "${CMAKE_CURRENT_SOURCE_DIR}/../abseil-cpp/absl/numeric"
  "${CMAKE_CURRENT_SOURCE_DIR}/../abseil-cpp/absl/strings"
  "${CMAKE_CURRENT_SOURCE_DIR}/../abseil-cpp/absl/synchronization"
  "${CMAKE_CURRENT_SOURCE_DIR}/../abseil-cpp/absl/time"
  "${CMAKE_CURRENT_SOURCE_DIR}/../abseil-cpp/absl/types"
  "${CMAKE_CURRENT_SOURCE_DIR}/../abseil-cpp/absl/utilities"
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
message(STATUS "GLIB INC: " ${GLIB_INCLUDE_DIRS})
message(STATUS "GLIB LIBS: " ${GLIB_LIBRARIES})

find_package(benchmark REQUIRED)
include_directories(${benchmark_INCLUDE_DIRS})
include_directories(${GLIB_INCLUDE_DIRS})

function(add_cpplib libname)
  add_library(${libname} STATIC "${libname}.cpp" util/path_utils.cpp)
  foreach(dep ${ARGN})
    target_link_libraries(${libname} ${dep})
  endforeach()
  # Add unit test.
  if(USE_GTEST)
    set(testname "${libname}_test")
    add_executable(${testname} "${testname}.cpp")
    # set_target_properties(${testname} PROPERTIES COMPILE_FLAGS "${UNITTEST_CXX_FLAGS}")
    add_sanitizers(${testname})
    target_link_libraries(${testname}
      ${libname}
      ${Boost_FILESYSTEM_LIBRARY}
      ${Boost_SYSTEM_LIBRARY}
      ${GPERFTOOLS_LIBRARIES}
      pthread
      ${GTEST}
      ${GMOCK_LIBRARIES}
      pthread
      leveldb
      )
    add_test(${testname} ${testname})
    # Add benchmark if exists.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${libname}_bench.cpp")
      set(benchname "${libname}_bench")
      add_executable(${benchname} "${benchname}.cpp")
      target_link_libraries(${benchname}
        ${libname}
        ${benchmark_LIBRARIES}
        pthread)
    endif()
  endif(USE_GTEST)
endfunction()

add_cpplib(lwrapper leveldb ${GLIB_LIBRARIES})

# add_cpplib(id_manager absl_numeric lwrapper leveldb)

add_cpplib(txn_context uuid)

add_cpplib(lock_manager lwrapper)

add_library(path_utils STATIC "util/path_utils.cpp")

# add_cpplib(pre_generate_uuid protobuf)

find_package(Protobuf REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIRS})

file(GLOB ProtoFiles "${CMAKE_CURRENT_SOURCE_DIR}/*.proto")
include_directories(${CMAKE_CURRENT_BINARY_DIR})
PROTOBUF_GENERATE_CPP(ProtoSources ProtoHeaders ${ProtoFiles})
message(STATUS "protocol sources: " ${ProtoSources})
message(STATUS "protocol headers: " ${ProtoHeaders})

add_library(secnfs_proto STATIC ${ProtoSources})
target_link_libraries(secnfs_proto ${PROTOBUF_LIBRARIES})

add_cpplib(txn_logger
  secnfs_proto protobuf absl_strings uuid lwrapper)
