/*TXNFS methods for handles
 */
#include "fsal_api.h"
//#include "id_manager.h"
#include <uuid/uuid.h>
#include "lock_manager.h"
#include "lwrapper.h"

#define UUID_ALLOC_LIMIT 64
#define TXN_UUID_LEN 16

struct txnfs_fsal_module {
  struct fsal_module module;
  struct fsal_obj_ops handle_ops;

  /** Config - database path */
  char *db_path;
};

extern struct txnfs_fsal_module TXNFS;

const db_store_t *db;
lock_manager_t *lm;

long fileid;

struct txnfs_fsal_obj_handle;

struct next_ops {
  struct export_ops exp_ops;           /*< Vector of operations */
  struct fsal_obj_ops obj_ops;         /*< Shared handle methods vector */
  struct fsal_dsh_ops dsh_ops;         /*< Shared handle methods vector */
  const struct fsal_up_vector *up_ops; /*< Upcall operations */
};

#define UDBG                                                                   \
  LogDebug(COMPONENT_FSAL, "TXDEBUG")
/**
 * Structure used to store data for read_dirents callback.
 *
 * Before executing the upper level callback (it might be another
 * stackable fsal or the inode cache), the context has to be restored.
 */
struct txnfs_readdir_state {
  fsal_readdir_cb cb;            /*< Callback to the upper layer. */
  struct txnfs_fsal_export *exp; /*< Export of the current txnfsal. */
  void *dir_state;               /*< State to be sent to the next callback. */
};

extern struct fsal_up_vector fsal_up_top;
void txnfs_handle_ops_init(struct fsal_obj_ops *ops);

/*
 * TXNFS internal export
 */
struct txnfs_fsal_export {
  struct fsal_export export;
  /* Other private export data goes here */
};

fsal_status_t txnfs_lookup_path(struct fsal_export *exp_hdl, const char *path,
                                struct fsal_obj_handle **handle,
                                struct attrlist *attrs_out);

fsal_status_t txnfs_create_handle(struct fsal_export *exp_hdl,
                                  struct gsh_buffdesc *hdl_desc,
                                  struct fsal_obj_handle **handle,
                                  struct attrlist *attrs_out);

fsal_status_t txnfs_alloc_and_check_handle(struct txnfs_fsal_export *export,
                                           struct fsal_obj_handle *sub_handle,
                                           struct fsal_filesystem *fs,
                                           struct fsal_obj_handle **new_handle,
                                           fsal_status_t subfsal_status,
                                           bool is_creation);

/*
 * TXNFS internal object handle
 *
 * It contains a pointer to the fsal_obj_handle used by the subfsal.
 *
 * AF_UNIX sockets are strange ducks.  I personally cannot see why they
 * are here except for the ability of a client to see such an animal with
 * an 'ls' or get rid of one with an 'rm'.  You can't open them in the
 * usual file way so open_by_handle_at leads to a deadend.  To work around
 * this, we save the args that were used to mknod or lookup the socket.
 */

struct txnfs_fsal_obj_handle {
  struct fsal_obj_handle obj_handle;  /*< Handle containing txnfs data.*/
  struct fsal_obj_handle *sub_handle; /*< Handle of the sub fsal.*/
  int32_t refcnt; /*< Reference count.  This is signed to make
                     mistakes easy to see. */
  uuid_t uuid;    /*< owned by txnfs_fsal_obj_handle */
};

int txnfs_fsal_open(struct txnfs_fsal_obj_handle *, int, fsal_errors_t *);
int txnfs_fsal_readlink(struct txnfs_fsal_obj_handle *, fsal_errors_t *);

static inline bool txnfs_unopenable_type(object_file_type_t type) {
  if ((type == SOCKET_FILE) || (type == CHARACTER_FILE) ||
      (type == BLOCK_FILE)) {
    return true;
  } else {
    return false;
  }
}

/* I/O management */
fsal_status_t txnfs_open(struct fsal_obj_handle *obj_hdl,
                         fsal_openflags_t openflags);
fsal_openflags_t txnfs_status(struct fsal_obj_handle *obj_hdl);
fsal_status_t txnfs_read(struct fsal_obj_handle *obj_hdl, uint64_t offset,
                         size_t buffer_size, void *buffer, size_t *read_amount,
                         bool *end_of_file);
fsal_status_t txnfs_write(struct fsal_obj_handle *obj_hdl, uint64_t offset,
                          size_t buffer_size, void *buffer,
                          size_t *write_amount, bool *fsal_stable);
fsal_status_t txnfs_commit(struct fsal_obj_handle *obj_hdl, /* sync */
                           off_t offset, size_t len);
fsal_status_t txnfs_lock_op(struct fsal_obj_handle *obj_hdl, void *p_owner,
                            fsal_lock_op_t lock_op,
                            fsal_lock_param_t *request_lock,
                            fsal_lock_param_t *conflicting_lock);
fsal_status_t txnfs_share_op(struct fsal_obj_handle *obj_hdl, void *p_owner,
                             fsal_share_param_t request_share);
fsal_status_t txnfs_close(struct fsal_obj_handle *obj_hdl);

/* Multi-FD */
fsal_status_t txnfs_open2(struct fsal_obj_handle *obj_hdl,
                          struct state_t *state, fsal_openflags_t openflags,
                          enum fsal_create_mode createmode, const char *name,
                          struct attrlist *attrs_in, fsal_verifier_t verifier,
                          struct fsal_obj_handle **new_obj,
                          struct attrlist *attrs_out, bool *caller_perm_check);
bool txnfs_check_verifier(struct fsal_obj_handle *obj_hdl,
                          fsal_verifier_t verifier);
fsal_openflags_t txnfs_status2(struct fsal_obj_handle *obj_hdl,
                               struct state_t *state);
fsal_status_t txnfs_reopen2(struct fsal_obj_handle *obj_hdl,
                            struct state_t *state, fsal_openflags_t openflags);
void txnfs_read2(struct fsal_obj_handle *obj_hdl, bool bypass,
                 fsal_async_cb done_cb, struct fsal_io_arg *read_arg,
                 void *caller_arg);
void txnfs_write2(struct fsal_obj_handle *obj_hdl, bool bypass,
                  fsal_async_cb done_cb, struct fsal_io_arg *write_arg,
                  void *caller_arg);
fsal_status_t txnfs_seek2(struct fsal_obj_handle *obj_hdl,
                          struct state_t *state, struct io_info *info);
fsal_status_t txnfs_io_advise2(struct fsal_obj_handle *obj_hdl,
                               struct state_t *state, struct io_hints *hints);
fsal_status_t txnfs_commit2(struct fsal_obj_handle *obj_hdl, off_t offset,
                            size_t len);
fsal_status_t txnfs_lock_op2(struct fsal_obj_handle *obj_hdl,
                             struct state_t *state, void *p_owner,
                             fsal_lock_op_t lock_op,
                             fsal_lock_param_t *req_lock,
                             fsal_lock_param_t *conflicting_lock);
fsal_status_t txnfs_close2(struct fsal_obj_handle *obj_hdl,
                           struct state_t *state);
fsal_status_t txnfs_fallocate(struct fsal_obj_handle *obj_hdl,
                              struct state_t *state, uint64_t offset,
                              uint64_t length, bool allocate);
fsal_status_t txnfs_copy(struct fsal_obj_handle *src_hdl, uint64_t src_offset,
                         struct fsal_obj_handle *dst_hdl, uint64_t dst_offset,
                         uint64_t count, uint64_t *copied);
fsal_status_t txnfs_start_compound(struct fsal_obj_handle *root_backup_hdl,
                                   void *data);
fsal_status_t txnfs_end_compound(struct fsal_obj_handle *root_backup_hdl,
                                 void *data);
fsal_status_t txnfs_clone(struct fsal_obj_handle *src_hdl, char **dst_name,
                          struct fsal_obj_handle *dir_hdl, char *uuid);

/* extended attributes management */
fsal_status_t
txnfs_list_ext_attrs(struct fsal_obj_handle *obj_hdl, unsigned int cookie,
                     fsal_xattrent_t *xattrs_tab, unsigned int xattrs_tabsize,
                     unsigned int *p_nb_returned, int *end_of_list);
fsal_status_t txnfs_getextattr_id_by_name(struct fsal_obj_handle *obj_hdl,
                                          const char *xattr_name,
                                          unsigned int *pxattr_id);
fsal_status_t txnfs_getextattr_value_by_name(struct fsal_obj_handle *obj_hdl,
                                             const char *xattr_name,
                                             void *buffer_addr,
                                             size_t buffer_size,
                                             size_t *p_output_size);
fsal_status_t txnfs_getextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
                                           unsigned int xattr_id,
                                           void *buffer_addr,
                                           size_t buffer_size,
                                           size_t *p_output_size);
fsal_status_t txnfs_setextattr_value(struct fsal_obj_handle *obj_hdl,
                                     const char *xattr_name, void *buffer_addr,
                                     size_t buffer_size, int create);
fsal_status_t txnfs_setextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
                                           unsigned int xattr_id,
                                           void *buffer_addr,
                                           size_t buffer_size);
fsal_status_t txnfs_remove_extattr_by_id(struct fsal_obj_handle *obj_hdl,
                                         unsigned int xattr_id);
fsal_status_t txnfs_remove_extattr_by_name(struct fsal_obj_handle *obj_hdl,
                                           const char *xattr_name);

/* helpers */
//uuid_t txnfs_get_uuid();

bool txnfs_db_handle_exists(struct gsh_buffdesc *hdl_desc);

int txnfs_db_insert_handle(struct gsh_buffdesc *hdl_desc, uuid_t uuid);
int txnfs_db_get_uuid(struct gsh_buffdesc *hdl_desc, uuid_t uuid);
int txnfs_db_delete_uuid(uuid_t uuid);
