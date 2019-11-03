add_definitions(
  -D__USE_GNU
  -D_GNU_SOURCE
)

set( LIB_PREFIX 64)

########### next target ###############

SET(fsaltxn_LIB_SRCS
   handle.c
   file.c
   xattrs.c
   txnfs_methods.h
   main.c
   helper.c
   export.c
   backup.c
   undo.c
   opvec.c
   cleanup.c
   locking.c
)

add_library(fsaltxnfs SHARED ${fsaltxn_LIB_SRCS})
add_sanitizers(fsaltxnfs)

link_directories(${PROJECT_SOURCE_DIR}/leveldb/out-static/)
include_directories(${PROJECT_SOURCE_DIR}/leveldb/include/)

target_link_libraries(fsaltxnfs
  gos
  leveldb
  lwrapper
  txn_logger
  lock_manager
  path_utils
)

set_target_properties(fsaltxnfs PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsaltxnfs COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )


########### install files ###############
