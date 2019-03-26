#include <gtest/gtest.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <experimental/filesystem>

#include "id_manager.h"
#include "lwrapper.h"
#include "txn_backend.h"
#include "txn_logger.h"
#include "undo_executor.h"

namespace fs = std::experimental::filesystem;
using namespace std;
vector<fs::path> dummy_paths;
vector<string> dummy_uuids;

void txn_processor(struct TxnLog* txn) {
  std::cout << "enumerating txn_id=" << txn->txn_id << std::endl;
}

void txn_write_execute(struct TxnLog* txn) {}

void txn_execute(struct TxnLog* txn) {
  switch (txn->compound_type) {
    case txn_VWrite:
      txn_write_execute(txn);
      break;
    case txn_VNone:
    case txn_VCreate:
    case txn_VMkdir:
    case txn_VRename:
    case txn_VUnlink:
    case txn_VSymlink:
      break;
  }
}

void txn_write_build(struct TxnLog* txn) {
  txn->num_files = dummy_paths.size();
  txn->created_file_ids =
      (struct FileId*)malloc(sizeof(struct FileId) * txn->num_files);

  // get uuid from leveldb for each file in dummy_paths
  for (int i = 0; i < txn->num_files; i++) {
    struct FileId* fid = &txn->created_file_ids[i];
    fid->data = dummy_uuids[i].c_str();
    fid->file_type = ft_File;
    fid->flags = 1;
  }
}

// build txnlog with dummy file paths
//
void txn_build(enum CompoundType compound_type, struct TxnLog* txn) {
  txn->compound_type = compound_type;
  txn->txn_id = rand();
  txn->backup_dir_path = "/tmp/foo";
  switch (compound_type) {
    case txn_VWrite:
      txn_write_build(txn);
      break;
    case txn_VNone:
    case txn_VCreate:
    case txn_VMkdir:
    case txn_VRename:
    case txn_VUnlink:
    case txn_VSymlink:
      break;
  }
}

void init_test_fs() {
  // create entry in lwrapper
  db_store_t* db = init_db_store("/tmp/test_db", true);
  const char* contents = "original";
  ASSERT_TRUE(db);

  ASSERT_EQ(initialize_id_manager(db), 0);
  // root for dummy files
  fs::path fsroot = "/tmp/executorfs";
  fs::create_directory(fsroot);

  // create 10 files
  for (int i = 0; i < 10; i++) {
    // insert entry in dummy_paths
    dummy_paths.emplace_back(fsroot / ("f" + to_string(i)));
  }

  for (auto& path : dummy_paths) {
    struct file_handle* handle;
    int mount_id;
    int fhsize = 0;
    int flags = 0;

    cout << path << endl;
    int fd = open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY);
    ASSERT_EQ(write(fd, contents, strlen(contents)), strlen(contents));
    close(fd);

    fhsize = sizeof(*handle);
    handle = (struct file_handle*)malloc(fhsize);
    handle->handle_bytes = 0;

    ASSERT_EQ(
        name_to_handle_at(AT_FDCWD, path.c_str(), handle, &mount_id, flags),
        -1);

    fhsize = sizeof(struct file_handle) + handle->handle_bytes;
    handle = (struct file_handle*)realloc(
        handle, fhsize); /* Copies handle->handle_bytes */
    // ASSERT_NE((int)handle, 0);
    ASSERT_EQ(
        name_to_handle_at(AT_FDCWD, path.c_str(), handle, &mount_id, flags), 0);

    db_kvpair_t* record = (db_kvpair_t*)malloc(sizeof(db_kvpair_t));
    const char* key = generate_file_id(db);
    dummy_uuids.emplace_back(key);
    const char* value = (char*)&handle;
    size_t key_len = strlen(key);
    size_t val_len = sizeof(struct file_handle) + handle->handle_bytes;
    record->key = key;
    record->val = value;
    record->key_len = key_len;
    record->val_len = val_len;

    // commit first transaction
    int ret = commit_transaction(record, 1, db);
    ASSERT_FALSE(ret);
  }
}

TEST(UndoExecutor, SuccessTxn) {
  srand(time(NULL));
  init_test_fs();

  struct TxnLog txn;
  txn_build(txn_VWrite, &txn);

  undo_txn_execute(&txn);
  // struct txn_backend* backend;
  // string dbroot = "/tmp/executordb";
  // init_ldbtxn_backend(dbroot.c_str(), "txn_", &backend);

  // backend->backend_init();
  // build TxnLog entry
  //

  // execute complete transaction
  //

  // check no undo operations
  //
  // fs::remove_all(dbroot);
  fs::remove_all("/tmp/executorfs");
}

TEST(DISABLED_UndoExecutor, PartialTxn) {
  srand(time(NULL));
  struct txn_backend* backend;
  string dbroot = "/tmp/executordb";
  init_ldbtxn_backend(dbroot.c_str(), "txn_", &backend);

  backend->backend_init();
  // build TxnLog entry
  //

  // execute complete transaction
  //

  // check no undo operations
  //
  fs::remove_all(dbroot);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
