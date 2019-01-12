SET( CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -std=c++11" )

include_directories(${GTEST_INCLUDE_DIR})

set(test_LIB
  pthread
  ${GTEST_LIBRARIES}
)

SET(lwrapper_api-test_SRCS
  lwrapper.c
  lwrapper_api-test.cpp
)

add_executable(lwrapper_api-test
  ${lwrapper_api-test_SRCS}
)

add_library(lwrapper STATIC lwrapper.c)

target_link_libraries(lwrapper_api-test leveldb ${test_LIB} pthread)
