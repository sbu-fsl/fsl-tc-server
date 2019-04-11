#include <iostream>
#include <experimental/filesystem>
#include "txn_logger.h"
#include "undo_executor.h"
#include "lwrapper.h"
#include "id_manager.h"
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <cassert>

namespace fs = std::experimental::filesystem;
using namespace std;

void get_dir_path(int dfd, char* path) {
  DIR* save = opendir(".");
  fchdir(dfd);
  getcwd(path, PATH_MAX);
  fchdir(dirfd(save));
  closedir(save);
}

struct file_handle* check_txn_path(int dirfd, const char* path) {
  struct file_handle* handle;
  int mount_id;
  int fhsize = 0;
  int flags = 0;
  int ret;
  fhsize = sizeof(*handle);
  handle = (struct file_handle*)malloc(fhsize);
  handle->handle_bytes = 0;

  assert(name_to_handle_at(dirfd, path, handle, &mount_id, flags) == -1);

  fhsize = sizeof(struct file_handle) + handle->handle_bytes;
  handle = (struct file_handle*)realloc(
      handle, fhsize); /* Copies handle->handle_bytes */
  ret = name_to_handle_at(dirfd, path, handle, &mount_id, flags);

  if (ret < 0) {
    // only valid error
    assert(errno == ENOENT);
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
      .key_len = strlen(uuidbuf),
      .val_len = 0,
  };
  int ret = get_id_handle(&rev_record, 1, db, false);
  assert(ret == 0);

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
  cout << ret << rev_record.val_len << " " << rev_record.val << endl;
  return ret;
}

/*
 * Undo write transaction
 */
void undo_txn_write_execute(struct TxnLog* txn, db_store_t* db) {
  cout << "undo count:" << txn->num_files << endl;
  int base_fd;
  uuid_t null_uuid = uuid_null();

  // backup directory for this txn
  fs::path bkproot = txn->backup_dir_path;

  for (int i = 0; i < txn->num_files; i++) {
    struct file_handle *base_handle = NULL, *allocated_handle = NULL;
    struct CreatedObject* oid = &txn->created_file_ids[i];
    cout << "undo txn path" << oid->path << endl;
    assert(oid->allocated_id.file_type == ft_File);
    fs::path original_path;
    uuid_t base_id = {.lo = oid->base_id.id_low, .hi = oid->base_id.id_high};
    uuid_t allocated_id = {.lo = oid->allocated_id.id_low,
                           .hi = oid->allocated_id.id_high};
    if (memcmp(&base_id, &null_uuid, sizeof(uuid_t)) != 0) {
      assert(oid->base_id.file_type == ft_Directory);
      base_handle = uuid_to_handle(db, base_id);
      base_fd = open_by_handle_at(AT_FDCWD, base_handle, O_RDONLY);
    }

    if (base_handle) {
      cout << "file with base path" << endl;
      char base_path[PATH_MAX];
      get_dir_path(base_fd, base_path);
      original_path = base_path;
      original_path /= oid->path;
      struct file_handle* handle = check_txn_path(base_fd, oid->path);
      if (handle) {
        if (handle_exists(db, handle) == 0) {
          // restore backup
          cout << "rename " << bkproot / id_to_string(uuid_to_buf(allocated_id))
               << original_path << endl;
          fs::rename(bkproot / id_to_string(uuid_to_buf(allocated_id)),
                     original_path);
        } else {
          cout << "remove " << original_path << endl;
          if (fs::exists(original_path)) {
            fs::remove(original_path);
          }
        }
      }
      close(base_fd);
    } else {
      cout << "file with absolute path" << endl;
      original_path = oid->path;
      allocated_handle = uuid_to_handle(db, allocated_id);
      // this must be create, failed to create file in txn
      if (allocated_handle) {
        // restore backup
        cout << "rename " << bkproot / id_to_string(uuid_to_buf(allocated_id))
             << original_path << endl;
        fs::rename(bkproot / id_to_string(uuid_to_buf(allocated_id)),
                   original_path);
      } else {
        cout << "remove " << original_path << endl;
        if (fs::exists(original_path)) {
          fs::remove(original_path);
        }
      }
    }
  }
}

/*
 * Undo create transaction
 */
void undo_txn_create_execute(struct TxnLog* txn, db_store_t* db) {
  cout << "undo count:" << txn->num_files << endl;
  int base_fd;
  uuid_t null_uuid = uuid_null();

  for (int i = 0; i < txn->num_files; i++) {
    struct file_handle *base_handle = NULL, *allocated_handle = NULL;
    struct CreatedObject* oid = &txn->created_file_ids[i];
    cout << "undo txn path" << oid->path << endl;
    assert(oid->allocated_id.file_type == ft_Directory);
    fs::path original_path;
    uuid_t base_id = {.lo = oid->base_id.id_low, .hi = oid->base_id.id_high};
    uuid_t allocated_id = {.lo = oid->allocated_id.id_low,
                           .hi = oid->allocated_id.id_high};
    allocated_handle = uuid_to_handle(db, base_id);
    assert(handle_exists(db, allocated_handle) != 0);
    return;
    if (memcmp(&base_id, &null_uuid, sizeof(uuid_t)) != 0) {
      assert(oid->base_id.file_type == ft_Directory);
      base_handle = uuid_to_handle(db, base_id);
      base_fd = open_by_handle_at(AT_FDCWD, base_handle, O_RDONLY);
      // assert(base_fd != -1);
      // assert(base_handle != NULL);
    }

    if (base_handle) {
      cout << "dir with base path" << endl;
      char base_path[PATH_MAX];
      get_dir_path(base_fd, base_path);
      original_path = base_path;
      original_path /= oid->path;
      struct file_handle* handle = check_txn_path(base_fd, oid->path);
      if (handle) {
        // if the txn failed, we should not have the handle in db
        assert(handle_exists(db, handle) != 0);
        cout << "remove dir" << original_path << endl;
        if (fs::exists(original_path)) {
          fs::remove(original_path);
        }
      }
      close(base_fd);
    } else {
      cout << "dir with absolute path" << endl;
      original_path = oid->path;
      if (allocated_handle) {
        // if the txn failed, we should not have the handle in db
        cout << "remove dir" << original_path << endl;
        if (fs::exists(original_path)) {
          fs::remove_all(original_path);
        }
      }
    }
  }
}

void undo_txn_execute(struct TxnLog* txn, db_store_t* db) {
  switch (txn->compound_type) {
    case txn_VWrite:
      undo_txn_write_execute(txn, db);
      break;
    case txn_VCreate:
      undo_txn_create_execute(txn, db);
      break;
    case txn_VNone:
    case txn_VMkdir:
    case txn_VRename:
    case txn_VUnlink:
    case txn_VSymlink:
      break;
  }
}
