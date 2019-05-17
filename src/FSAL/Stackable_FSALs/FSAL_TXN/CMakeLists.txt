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
)

add_library(fsaltxnfs SHARED ${fsaltxn_LIB_SRCS})
   
link_directories(${PROJECT_SOURCE_DIR}/leveldb/out-static/)
include_directories(${PROJECT_SOURCE_DIR}/leveldb/include/)

target_link_libraries(fsaltxnfs
  gos
  leveldb
  lwrapper
  id_manager
  lock_manager
)

set_target_properties(fsaltxnfs PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsaltxnfs COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )


########### install files ###############
