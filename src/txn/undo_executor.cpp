#include <iostream>
#include "txn_logger.h"
#include "undo_executor.h"
#include "lwrapper.h"
#include "id_manager.h"
#include <fcntl.h>
//#include <experimental/filesystem>
#ifndef F_GETPATH
#define F_GETPATH (1024 + 7)
#endif
// namespace fs = std::experimental::filesystem;
using namespace std;
/*
 * Undo write files
 */
void undo_txn_write_execute(struct TxnLog* txn, db_store_t* db) {
  cout << "undo count:" << txn->num_files << endl;
  for (int i = 0; i < txn->num_files; i++) {
    struct FileId* fid = &txn->created_file_ids[i];
    cout << "undo" << fid->data << endl;

    // Reverse lookup record by handle.
    db_kvpair_t rev_record = {
        .key = fid->data,
        .val = NULL,
        .key_len = strlen(fid->data),
        .val_len = 0,
    };
    int ret = get_id_handle(&rev_record, 1, db, false);
    struct file_handle* handle = (struct file_handle*)rev_record.val;
    cout << "key:" << id_to_string(fid->data) << endl;
    cout << "handle len: " << rev_record.val_len << endl;
    cout << "handle bytes: " << handle->handle_bytes << endl;

    int fd = open_by_handle_at(AT_FDCWD, handle, O_RDONLY);
    // cout << "fd:" << fd << " " << errno << endl;
    char buf[20];
    char path[PATH_MAX];
    memset(buf, 0, 20);
    read(fd, buf, 20);
    cout << buf << endl;
    if (fcntl(fd, F_GETPATH, path) != -1) {
      cout << path << endl;
    }
    close(fd);
    // cout << ret << endl;
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
