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
   export.c
   ../../../lwrapper/lwrapper.c
)

add_library(fsaltxn SHARED ${fsaltxn_LIB_SRCS})
   
link_directories(${PROJECT_SOURCE_DIR}/leveldb/out-static/)
include_directories(${PROJECT_SOURCE_DIR}/leveldb/include/)

target_link_libraries(fsaltxn
  gos
  leveldb
  idmanager
  lockmanager
)

set_target_properties(fsaltxn PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsaltxn COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )


########### install files ###############
