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

set(test_LIB
  pthread
  ${GTEST_LIBRARIES}
)

find_package(benchmark REQUIRED)
include_directories(${benchmark_INCLUDE_DIRS})

SET(lock_manager-test_SRCS
   lock_manager-test.cpp
   lock_manager.cpp
   util/path_utils.cpp
)

add_executable(lock_manager-test
  ${lock_manager-test_SRCS}
)

SET(lock_manager-bench_SRCS
   lock_manager-bench.cpp
   lock_manager.cpp
   util/path_utils.cpp
)

add_executable(lock_manager-bench
  ${lock_manager-bench_SRCS}
)

SET(id_manager-bench_SRCS
  ../lwrapper/lwrapper.c
  id_manager.cpp
  id_manager-bench.cpp
)

add_executable(id_manager-bench
  ${id_manager-bench_SRCS}
)

SET(id_manager-lib_SRCS
  ../lwrapper/lwrapper.c
  id_manager.cpp
)

add_library(idmanager STATIC ${id_manager-lib_SRCS})

SET(id_manager-test_SRCS
  ../lwrapper/lwrapper.c
  id_manager.cpp
  id_manager-test.cpp
)

add_executable(id_manager-test
  ${id_manager-test_SRCS}
)


SET(lock_manager-lib_SRCS
  lock_manager.cpp
  util/path_utils.cpp
)

add_library(lockmanager STATIC ${lock_manager-lib_SRCS})

target_link_libraries(lock_manager-test ${test_LIB} pthread )
target_link_libraries(id_manager-bench absl_numeric leveldb ${benchmark_LIBRARIES} pthread)
target_link_libraries(idmanager absl_numeric leveldb pthread)
target_link_libraries(id_manager-test absl_numeric leveldb ${test_LIB} pthread
  ${Boost_FILESYSTEM_LIBRARY}
  ${Boost_SYSTEM_LIBRARY})
target_link_libraries(lock_manager-bench ${benchmark_LIBRARIES} pthread)
target_link_libraries(lock_manager-test ${test_LIB} pthread)

set(pre_generate_uuid_SRC pre_generate_uuid.cpp)
add_library(pre_generate_uuid STATIC ${pre_generate_uuid_SRC})
target_link_libraries(pre_generate_uuid secnfs_proto protobuf pthread)

add_executable(pre_generate_uuid-test pre_generate_uuid-test.cpp)
target_link_libraries(pre_generate_uuid-test pre_generate_uuid protobuf pthread ${test_LIB})

install(TARGETS idmanager DESTINATION ${LIB_INSTALL_DIR})
install(TARGETS lockmanager DESTINATION ${LIB_INSTALL_DIR})

find_package(Protobuf REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIRS})

file(GLOB ProtoFiles "${CMAKE_CURRENT_SOURCE_DIR}/*.proto")
include_directories(${CMAKE_CURRENT_BINARY_DIR})
PROTOBUF_GENERATE_CPP(ProtoSources ProtoHeaders ${ProtoFiles})
message(STATUS "protocol sources: " ${ProtoSources})
message(STATUS "protocol headers: " ${ProtoHeaders})

add_library(secnfs_proto STATIC ${ProtoSources})

target_link_libraries(secnfs_proto ${PROTOBUF_LIBRARIES})

set(txn_logger_SRC txn_logger.cpp)
add_library(txn_logger STATIC ${txn_logger_SRC})
target_link_libraries(txn_logger secnfs_proto protobuf pthread)

add_executable(txn_logger_test txn_logger_test.cpp)
target_link_libraries(txn_logger_test txn_logger protobuf pthread ${test_LIB})
