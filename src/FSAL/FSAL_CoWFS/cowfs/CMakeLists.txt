add_definitions(
  -D__USE_GNU
  -D_GNU_SOURCE
)

SET(fsalcowfs_LIB_SRCS
   main.c
   ../export.c
   ../handle.c
   ../handle_syscalls.c
   ../file.c
   ../xattrs.c
   ../cowfs_methods.h
   ../splice_copy.c
   ../state.c
   subfsal_cowfs.c
  )

if(ENABLE_CoWFS_DEBUG_ACL)
  set(fsalcowfs_LIB_SRCS ${fsalcowfs_LIB_SRCS} attrs.c)
endif(ENABLE_CoWFS_DEBUG_ACL)

add_library(fsalcowfs SHARED ${fsalcowfs_LIB_SRCS})

target_link_libraries(fsalcowfs
  gos
  fsal_cowfs_os
  ${SYSTEM_LIBRARIES}
)

set_target_properties(fsalcowfs PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalcowfs COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )
