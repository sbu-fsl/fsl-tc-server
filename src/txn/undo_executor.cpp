#include <iostream>
#include <experimental/filesystem>
#include "txn_logger.h"
#include "undo_executor.h"
#include "lwrapper.h"
#include "id_manager.h"
#include <fcntl.h>
#include <cassert>

namespace fs = std::experimental::filesystem;
using namespace std;
/*
 * Undo write transaction
 */
void undo_txn_write_execute(struct TxnLog* txn, db_store_t* db) {
  cout << "undo count:" << txn->num_files << endl;
  db_kvpair_t rev_record;
  for (int i = 0; i < txn->num_files; i++) {
    struct CreatedObject* oid = &txn->created_file_ids[i];
    cout << "undo txn path" << oid->path << endl;
    uuid_t uuid = {.lo = oid->allocated_id.id_low,
                   .hi = oid->allocated_id.id_high};
    uuid_t null_uuid = uuid_null();

    // if val_len = 0, no backup file => create operation
    // delete file at path
    if (memcmp(&uuid, &null_uuid, sizeof(uuid_t)) == 0) {
      cout << "create " << i << endl;
      // TODO - check if path exists, if not we can stop here
      // we should have failed at this operation
      if (fs::exists(oid->path)) {
        fs::remove(oid->path);
      }
      continue;
    }
    cout << "write " << i << endl;
    // Reverse lookup record by handle.
    char* uuidbuf = uuid_to_buf(uuid);
    fs::path bkppath = txn->backup_dir_path;
    bkppath = bkppath / id_to_string(uuidbuf);

    rev_record = {
        .key = uuidbuf,
        .val = NULL,
        .key_len = strlen(uuidbuf),
        .val_len = 0,
    };
    int ret = get_id_handle(&rev_record, 1, db, false);
    assert(ret == 0);
    // Don't need the file handle right now
    // struct file_handle* handle = (struct file_handle*)rev_record.val;
    // int fd = open_by_handle_at(AT_FDCWD, handle, O_RDONLY);
    // assert(fd != -1);
    // close(fd);

    // rename backup_dir_path/uuid to path
    fs::rename(bkppath, oid->path);
    // finished restore of files
    // delete the txn in db
    ret = delete_id_handle(&rev_record, 1, db, false);
    assert(ret == 0);
  }
}

void undo_txn_execute(struct TxnLog* txn, db_store_t* db) {
  /*cout << "undo"
       << " " << txn->compound_type << endl;*/
  switch (txn->compound_type) {
    case txn_VWrite:
      undo_txn_write_execute(txn, db);
      break;
    case txn_VCreate:
    case txn_VNone:
    case txn_VMkdir:
    case txn_VRename:
    case txn_VUnlink:
    case txn_VSymlink:
      break;
  }
}
