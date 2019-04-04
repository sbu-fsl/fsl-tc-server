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

class UndoExecutor : public ::testing::Test {
 protected:
  // writes
  vector<fs::path> write_paths;
  vector<uuid_t> write_uuids;
  // create files
  vector<fs::path> create_paths;

  const char* original = "original";
  db_store_t* db;
  const string fsroot = "/tmp/executorfs";
  const string bkproot = "/tmp/executorbkp";
  const string dbfile = "/tmp/executordb";

  virtual void SetUp() {
    srand(time(NULL));

    // open database for file_handle lookup
    db = init_db_store(dbfile.c_str(), true);
    ASSERT_TRUE(db);

    // setup dummy paths and entries in lwrapper
    init_test_fs(fsroot, db);

    fs::create_directory(bkproot);
  }

  virtual void TearDown() {
    destroy_db_store(db);
    // fs::remove_all(fsroot);
  }

  void txn_write_build(struct TxnLog* txn) {
    txn->num_files = create_paths.size() + write_paths.size();
    txn->created_file_ids = (struct CreatedObject*)malloc(
        sizeof(struct CreatedObject) * txn->num_files);
    int i = 0;
    // populate 10 write ops, and 10 create ops
    // get uuid from leveldb for each file in write_paths
    for (; i < write_paths.size(); i++) {
      struct CreatedObject* oid = &txn->created_file_ids[i];
      uuid_t uuid = write_uuids[i];
      fs::path bkppath = bkproot;
      bkppath = bkppath / id_to_string(uuid_to_buf(uuid));
      // snapshot file
      fs::copy_file(write_paths[i], bkppath);

      // copy original path
      memset(&oid->base, 0, sizeof(struct ObjectId));
      strcpy(oid->path, write_paths[i].c_str());
      oid->allocated_id.file_type = ft_File;
      oid->allocated_id.id_low = uuid.lo;
      oid->allocated_id.id_high = uuid.hi;
    }

    for (; i < txn->num_files; i++) {
      struct CreatedObject* oid = &txn->created_file_ids[i];
      fs::path bkppath = bkproot;
      // no snapshot

      // copy original path
      strcpy(oid->path, create_paths[i - write_paths.size()].c_str());
      memset(&oid->allocated_id, 0, sizeof(struct ObjectId));
      memset(&oid->base, 0, sizeof(struct ObjectId));
    }
  }

  void init_test_fs(const string& path, db_store_t* db) {

    ASSERT_EQ(initialize_id_manager(db), 0);
    // root for dummy files
    fs::path fsroot = path;
    fs::create_directory(fsroot);

    // create 10 files
    for (int i = 0; i < 10; i++) {
      // insert entry in write_paths
      write_paths.emplace_back(fsroot / ("f" + to_string(i)));
    }

    for (auto& path : write_paths) {
      struct file_handle* handle;
      int mount_id;
      int fhsize = 0;
      int flags = 0;

      int fd = open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY);
      ASSERT_EQ(write(fd, original, strlen(original)), strlen(original));
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
      ASSERT_EQ(
          name_to_handle_at(AT_FDCWD, path.c_str(), handle, &mount_id, flags),
          0);

      db_kvpair_t* record = (db_kvpair_t*)malloc(sizeof(db_kvpair_t));
      const char* key = generate_file_id(db);
      write_uuids.push_back(buf_to_uuid(key));
      const char* value = (char*)handle;
      size_t key_len = strlen(key);
      size_t val_len = fhsize;
      record->key = key;
      record->val = value;
      record->key_len = key_len;
      record->val_len = val_len;

      // commit first transaction
      int ret = put_id_handle(record, 1, db);
      ASSERT_FALSE(ret);
    }

    // create 10 files
    for (int i = 0; i < 10; i++) {
      // insert entry in write_paths
      create_paths.emplace_back(fsroot / ("c" + to_string(i)));
    }
  }

  // build txnlog with dummy file paths
  //
  void txn_build(enum CompoundType compound_type, struct TxnLog* txn) {
    txn->compound_type = compound_type;
    txn->txn_id = rand();
    txn->backup_dir_path = bkproot.c_str();
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
};

TEST_F(UndoExecutor, SuccessTxn) {
  struct TxnLog txn;
  // write txnlog entry
  // with dummy files and populate file handles in leveldb
  txn_build(txn_VWrite, &txn);

  undo_txn_execute(&txn, db);

  // enumerate backup directory
  // all the backups should have been renamed to original
  ASSERT_EQ(
      std::distance(fs::directory_iterator(bkproot), fs::directory_iterator{}),
      0);

  // check file contents of dummy_file_path
  // should be original
  for (auto& path : write_paths) {
    int fd;
    char buf[80];
    fd = open(path.c_str(), O_RDONLY);
    ASSERT_GT(fd, 2);
    ASSERT_EQ(read(fd, buf, 80), strlen(original));
    ASSERT_STREQ(buf, original);
    close(fd);
  }

  // check database, uuid entries should disappear
  for (auto& uuid : write_uuids) {
    auto buf = uuid_to_buf(uuid);
    db_kvpair_t rev_record = {
        .key = buf,
        .val = NULL,
        .key_len = strlen(buf),
        .val_len = 0,
    };
    ASSERT_EQ(0, get_id_handle(&rev_record, 1, db, false));
  }
}

TEST_F(UndoExecutor, DISABLED_PartialTxn) {
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
