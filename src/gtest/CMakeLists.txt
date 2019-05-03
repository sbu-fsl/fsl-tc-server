# Set CMAKE_CXX_FLAGS, for gtest presently
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=gnu++14 -Wnon-virtual-dtor -Wno-invalid-offsetof")

set(UNITTEST_LIBS ${GTEST_LIBRARIES} boost_program_options boost_system ${PTHREAD_LIBS})
set(UNITTEST_CXX_FLAGS "${CMAKE_CXX_FLAGS} -I${PROJECT_SOURCE_DIR}/gtest -I${GTEST_INCLUDE_DIR} -fno-strict-aliasing")

set(GANESHA_LIBRARIES
  rpcal
  log
  support
  hash
  config_parsing
  MainServices
  ${PROTOCOLS}
  ${GANESHA_CORE}
  # the following double mention of fsalpseudo and FsalCore
  # static libs is NOT a typo
  fsalpseudo
  FsalCore
  fsalpseudo
  FsalCore
  ${LIBTIRPC_LIBRARIES}
  ${SYSTEM_LIBRARIES}
  )

set(ganesha_test_conf
  "${CMAKE_CURRENT_SOURCE_DIR}/../config_samples/txn-test.conf")

function(add_gtest testname)
  add_executable(${testname} "${testname}.cc")
  add_sanitizers(${testname})
  target_link_libraries(${testname}
    ${GANESHA_LIBRARIES}
    ${UNITTEST_LIBS}
    ${LTTNG_LIBRARIES}
    ${LTTNG_CTL_LIBRARIES}
    ${GPERFTOOLS_LIBRARIES}
    )
  set_target_properties(${testname} PROPERTIES COMPILE_FLAGS "${UNITTEST_CXX_FLAGS}")
  # The tests take too long to run; uncomment after speed them up.
   add_test(${testname} ${testname}
   "--logfile=/tmp/${testname}.log"
   "--debug=Debug"
   "--gtest_filter=*SIMPLE*"
   "--config=${ganesha_test_conf}")
endfunction()

add_subdirectory(fsal_api)
add_subdirectory(nfs4)

# generic test
set(test_example_SRCS
  test_example.cc
)
add_executable(test_example EXCLUDE_FROM_ALL
  ${test_example_SRCS})
add_sanitizers(test_example)

target_link_libraries(test_example
  ${CMAKE_THREAD_LIBS_INIT}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  pthread
  )
set_target_properties(test_example PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")

# Test using ganesha internals
add_gtest(test_ci_hash_dist1)

set(test_rbt_SRCS
  test_rbt.cc
  )

add_executable(test_rbt
  ${test_rbt_SRCS})
add_sanitizers(test_rbt)

target_link_libraries(test_rbt
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_rbt PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")

# FSAL_TXN specific tests
add_gtest(test_txn_handle)
