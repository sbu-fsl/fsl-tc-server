#include <iostream>
#include <experimental/filesystem>
#include <glog/logging.h>
#include "txn_logger.h"
#include "undo_executor.h"
#include "lwrapper.h"
#include "id_manager.h"
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

namespace fs = std::experimental::filesystem;
using namespace std;

struct file_handle* check_txn_path(int dirfd, const char* path) {
  struct file_handle* handle;
  int mount_id;
  int fhsize = 0;
  int flags = 0;
  int ret;
  fhsize = sizeof(*handle);
  handle = (struct file_handle*)malloc(fhsize);
  handle->handle_bytes = 0;

  LOG_ASSERT(name_to_handle_at(dirfd, path, handle, &mount_id, flags) == -1);

  fhsize = sizeof(struct file_handle) + handle->handle_bytes;
  handle = (struct file_handle*)realloc(
      handle, fhsize); /* Copies handle->handle_bytes */
  ret = name_to_handle_at(dirfd, path, handle, &mount_id, flags);

  if (ret < 0) {
    // only valid error
    LOG_ASSERT(errno == ENOENT);
    free(handle);
    handle = NULL;
  }

  return handle;
}
struct file_handle* uuid_to_handle(db_store_t* db, uuid_t uuid) {
  // Reverse lookup record by handle.
  char* uuidbuf = uuid_to_buf(uuid);
  db_kvpair_t rev_record;

  rev_record = {
      .key = uuidbuf,
      .val = NULL,
      .key_len = TXN_UUID_LEN,
      .val_len = 0,
  };
  int ret = get_id_handle(&rev_record, 1, db, false);
  LOG_ASSERT(ret == 0);

  struct file_handle* handle = (struct file_handle*)rev_record.val;
  return handle;
}

int handle_exists(db_store_t* db, struct file_handle* handle) {
  // Reverse lookup record by handle.
  db_kvpair_t rev_record;

  rev_record = {
      .key = (char*)handle,
      .val = NULL,
      .key_len = sizeof(struct file_handle) + handle->handle_bytes,
      .val_len = 0,
  };
  int ret = get_id_handle(&rev_record, 1, db, true);
  return rev_record.val_len > 0 ? ret : -1;
}

/*
 * Undo write transaction
 */
void undo_txn_write_execute(struct TxnLog* txn, db_store_t* db) {
  int base_fd;
  uuid_t null_uuid = uuid_null();

  // backup directory for this txn
  fs::path bkproot = txn->backup_dir_path;

  for (int i = 0; i < txn->num_files; i++) {
    struct file_handle *base_handle = NULL, *allocated_handle = NULL;
    struct CreatedObject* oid = &txn->created_file_ids[i];
    LOG(INFO) << "undo txn path" << oid->path << endl;
    LOG_ASSERT(oid->allocated_id.file_type == ft_File);
    uuid_t base_id = {.lo = oid->base_id.id_low, .hi = oid->base_id.id_high};
    uuid_t allocated_id = {.lo = oid->allocated_id.id_low,
                           .hi = oid->allocated_id.id_high};
    if (memcmp(&base_id, &null_uuid, sizeof(uuid_t)) != 0) {
      LOG_ASSERT(oid->base_id.file_type == ft_Directory);
      base_handle = uuid_to_handle(db, base_id);
      base_fd = open_by_handle_at(AT_FDCWD, base_handle, O_RDONLY);
    }

    if (base_handle) {
      LOG(INFO) << "file with base path" << endl;
      struct file_handle* handle = check_txn_path(base_fd, oid->path);
      if (handle) {
        if (handle_exists(db, handle) == 0) {
          // restore backup
          LOG(INFO) << "rename "
                    << bkproot / id_to_string(uuid_to_buf(allocated_id))
                    << "{base}" << oid->path << endl;
          fs::path path = bkproot / id_to_string(uuid_to_buf(allocated_id));
          renameat(AT_FDCWD, path.c_str(), base_fd, oid->path);
        } else {
          LOG(INFO) << "remove {base} " << oid->path << endl;
          LOG_ASSERT(unlinkat(base_fd, oid->path, 0) == 0 || errno == ENOENT);
        }
      }
      close(base_fd);
    } else {
      LOG(INFO) << "file with absolute path" << endl;
      allocated_handle = uuid_to_handle(db, allocated_id);
      // this must be create, failed to create file in txn
      if (allocated_handle) {
        // restore backup
        LOG(INFO) << "rename "
                  << bkproot / id_to_string(uuid_to_buf(allocated_id))
                  << oid->path << endl;
        fs::path path = bkproot / id_to_string(uuid_to_buf(allocated_id));
        renameat(AT_FDCWD, path.c_str(), AT_FDCWD, oid->path);
      } else {
        LOG(INFO) << "remove " << oid->path << endl;
        LOG_ASSERT(unlinkat(AT_FDCWD, oid->path, 0) == 0 || errno == ENOENT);
      }
    }
  }
}

/*
 * Undo create transaction
 */
void undo_txn_create_execute(struct TxnLog* txn, db_store_t* db) {
  LOG(INFO) << "undo count:" << txn->num_files << endl;
  int base_fd;
  uuid_t null_uuid = uuid_null();

  for (int i = 0; i < txn->num_files; i++) {
    struct file_handle* base_handle = NULL;
    struct CreatedObject* oid = &txn->created_file_ids[i];
    LOG(INFO) << i << ": undo txn path" << oid->path << endl;
    LOG_ASSERT(oid->allocated_id.file_type == ft_Directory);
    uuid_t base_id = {.lo = oid->base_id.id_low, .hi = oid->base_id.id_high};
    if (memcmp(&base_id, &null_uuid, sizeof(uuid_t)) != 0) {
      LOG_ASSERT(oid->base_id.file_type == ft_Directory);
      base_handle = uuid_to_handle(db, base_id);
      base_fd = open_by_handle_at(AT_FDCWD, base_handle, O_RDONLY);
      LOG_ASSERT(base_fd != -1);
      LOG_ASSERT(base_handle != NULL);
    }

    if (base_handle) {
      LOG(INFO) << "dir with base path" << endl;
      struct file_handle* handle = check_txn_path(base_fd, oid->path);
      if (handle) {
        // if the txn failed, we should not have the handle in db
        LOG_ASSERT(handle_exists(db, handle) != 0);
        LOG(INFO) << "remove dir" << oid->path << endl;
        LOG_ASSERT(unlinkat(base_fd, oid->path, AT_REMOVEDIR) == 0 ||
                   errno == ENOENT);
      }
      close(base_fd);
    } else {
      LOG(INFO) << "dir with absolute path" << endl;
      LOG(INFO) << "remove dir" << oid->path << endl;
      LOG_ASSERT(unlinkat(AT_FDCWD, oid->path, AT_REMOVEDIR) == 0 ||
                 errno == ENOENT);
    }
  }
}

/*
 * Undo unlink transaction
 */
void undo_txn_unlink_execute(struct TxnLog* txn, db_store_t* db) {
  uuid_t null_uuid = uuid_null();

  // backup directory for this txn
  fs::path bkproot = txn->backup_dir_path;

  for (int i = 0; i < txn->num_unlinks; i++) {
    int parent_fd = -1;
    struct file_handle* parent_handle = NULL;
    struct UnlinkId* oid = &txn->created_unlink_ids[i];
    LOG(INFO) << "undo txn name " << oid->name << endl;
    uuid_t parent_id = {.lo = oid->parent_id.id_low,
                        .hi = oid->parent_id.id_high};

    // parent id cannot be null uuid
    LOG_ASSERT(memcmp(&parent_id, &null_uuid, sizeof(uuid_t)) != 0);

    // parent object should be a directory
    LOG_ASSERT(oid->parent_id.file_type == ft_Directory);

    // parent handle should exist in database
    parent_handle = uuid_to_handle(db, parent_id);
    LOG_ASSERT(parent_handle);

    // handle is valid
    parent_fd = open_by_handle_at(AT_FDCWD, parent_handle, O_RDONLY);
    LOG_ASSERT(parent_fd > 0);

    // rename backup file to original
    // bkproot / index of the object
    fs::path path = bkproot / to_string(i);

    // note: even if the file wasn't unlinked, we'll restore from backup
    LOG(INFO) << "restoring unlinked file" << endl;
    LOG_ASSERT(fs::exists(path));
    LOG_ASSERT(renameat(AT_FDCWD, path.c_str(), parent_fd, oid->name) == 0);
    close(parent_fd);
  }
}

/*
 * Undo symlink transaction
 */
void undo_txn_symlink_execute(struct TxnLog* txn, db_store_t* db) {
  LOG(INFO) << "undo count:" << txn->num_files << endl;
  int parent_fd;

  for (int i = 0; i < txn->num_symlinks; i++) {
    struct file_handle* parent_handle = NULL;
    struct SymlinkId* oid = &txn->created_symlink_ids[i];
    LOG(INFO) << i << ": undo txn path" << oid->name << endl;
    LOG_ASSERT(oid->parent_id.file_type == ft_Directory);
    uuid_t parent_id = {.lo = oid->parent_id.id_low,
                        .hi = oid->parent_id.id_high};
    LOG_ASSERT(oid->parent_id.file_type == ft_Directory);
    parent_handle = uuid_to_handle(db, parent_id);
    parent_fd = open_by_handle_at(AT_FDCWD, parent_handle, O_RDONLY);
    LOG_ASSERT(parent_fd != -1);
    LOG_ASSERT(parent_handle != NULL);

    int ret = unlinkat(parent_fd, oid->name, 0);
    LOG_IF(INFO, ret == 0) << "removed symlink " << oid->name << endl;
    LOG_ASSERT(ret == 0 || (ret < 0 && errno == ENOENT));
    close(parent_fd);
  }
}
void undo_txn_execute(struct TxnLog* txn, db_store_t* db) {
  switch (txn->compound_type) {
    case txn_VNone:
      break;
    case txn_VWrite:
      undo_txn_write_execute(txn, db);
      break;
    case txn_VCreate:
      undo_txn_create_execute(txn, db);
      break;
    case txn_VUnlink:
      undo_txn_unlink_execute(txn, db);
      break;
    case txn_VSymlink:
      undo_txn_symlink_execute(txn, db);
      break;
    case txn_VRename:
    case txn_VMkdir:
      LOG_ASSERT(!"not implemented");
      break;
  }
}
