#include "txn_logger.h"
#include "lwrapper.h"
#include "undo_executor.h"
//#include <experimental/filesystem>

// namespace fs = std::experimental::filesystem;
/*
 * Undo write files
 */
void undo_txn_write_execute(struct TxnLog* txn) {

  // for (int i = 0; i < txn->num_files; i++) {
  // struct FileId* fid = txn->created_file_ids[i];
  // char* path;
  //}
}

void undo_txn_execute(struct TxnLog* txn) {
  switch (txn->compound_type) {
    case txn_VWrite:
      undo_txn_write_execute(txn);
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
