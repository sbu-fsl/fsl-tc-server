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
  struct ObjectId base;

  const char* original = "original";
  db_store_t* db;
  const string fsroot = "/tmp/executorfs";
  const string bkproot = "/tmp/executorbkp";
  const string dbfile = "/tmp/executordb";

  virtual void SetUp() {
    srand(time(NULL));

    // cleanup, incase we crashed last time
    if (fs::exists(fsroot)) {
      fs::remove_all(fsroot);
    }
    if (fs::exists(bkproot)) {
      fs::remove_all(bkproot);
    }
    if (fs::exists(dbfile)) {
      fs::remove_all(dbfile);
    }

    // setup base
    base.file_type = ft_Directory;

    // open database for file_handle lookup
    db = init_db_store(dbfile.c_str(), true);
    ASSERT_TRUE(db);

    // setup dummy paths and entries in lwrapper
    init_test_fs(fsroot, db);

    fs::create_directory(bkproot);
  }

  virtual void TearDown() {
    destroy_db_store(db);
    fs::remove_all(fsroot);
  }

  // add 10 writes and file creates using absolute paths
  //
  void txn_write_absolute(struct TxnLog* txn) {
    txn->num_files = create_paths.size() + write_paths.size();
    txn->created_file_ids = (struct CreatedObject*)malloc(
        sizeof(struct CreatedObject) * txn->num_files);
    int i = 0;
    // populate 10 write ops, and 10 create ops
    // get uuid from leveldb for each file in write_paths
    for (; i < (int)write_paths.size(); i++) {
      struct CreatedObject* oid = &txn->created_file_ids[i];
      uuid_t uuid = write_uuids[i];
      fs::path bkppath = bkproot;
      bkppath = bkppath / id_to_string(uuid_to_buf(uuid));
      // snapshot file
      fs::copy_file(write_paths[i], bkppath);

      // copy original path
      memset(&oid->base_id, 0, sizeof(struct ObjectId));
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
      // this would be a generated uuid
      uuid_t tuuid = buf_to_uuid(generate_file_id(db));
      memcpy(&oid->allocated_id, &tuuid, sizeof(struct ObjectId));
      memset(&oid->base_id, 0, sizeof(struct ObjectId));
    }
  }

  // add 10 writes and file creates using absolute paths
  //
  void txn_write_base(struct TxnLog* txn) {
    txn->num_files = create_paths.size() + write_paths.size();
    txn->created_file_ids = (struct CreatedObject*)malloc(
        sizeof(struct CreatedObject) * txn->num_files);
    int i = 0;
    // populate 10 write ops, and 10 create ops
    // get uuid from leveldb for each file in write_paths
    for (; i < (int)write_paths.size(); i++) {
      struct CreatedObject* oid = &txn->created_file_ids[i];
      uuid_t uuid = write_uuids[i];
      fs::path bkppath = bkproot;
      bkppath = bkppath / id_to_string(uuid_to_buf(uuid));
      // snapshot file
      fs::copy_file(write_paths[i], bkppath);

      // copy original path
      memset(&oid->base_id, 0, sizeof(struct ObjectId));
      strcpy(oid->path, write_paths[i].filename().c_str());
      memcpy(&oid->base_id, &base, sizeof(struct ObjectId));
      oid->allocated_id.file_type = ft_File;
      oid->allocated_id.id_low = uuid.lo;
      oid->allocated_id.id_high = uuid.hi;
    }

    for (; i < txn->num_files; i++) {
      struct CreatedObject* oid = &txn->created_file_ids[i];
      fs::path bkppath = bkproot;
      // no snapshot

      // copy original path
      strcpy(oid->path,
             create_paths[i - write_paths.size()].filename().c_str());
      // this would be a generated uuid
      uuid_t tuuid = buf_to_uuid(generate_file_id(db));
      memcpy(&oid->allocated_id, &tuuid, sizeof(struct ObjectId));
      memcpy(&oid->base_id, &base, sizeof(struct ObjectId));
    }
  }

  struct file_handle* path_to_fhandle(const char* path) {
    struct file_handle* handle;
    int mount_id;
    int fhsize = 0;
    int flags = 0;
    fhsize = sizeof(*handle);
    handle = (struct file_handle*)malloc(fhsize);
    handle->handle_bytes = 0;

    EXPECT_EQ(name_to_handle_at(AT_FDCWD, path, handle, &mount_id, flags), -1);

    fhsize = sizeof(struct file_handle) + handle->handle_bytes;
    handle = (struct file_handle*)realloc(
        handle, fhsize); /* Copies handle->handle_bytes */
    EXPECT_EQ(name_to_handle_at(AT_FDCWD, path, handle, &mount_id, flags), 0);

    return handle;
  }

  void insert_handle(char* key, struct file_handle* handle) {
    db_kvpair_t* record = (db_kvpair_t*)malloc(sizeof(db_kvpair_t));
    write_uuids.push_back(buf_to_uuid(key));
    const char* value = (char*)handle;
    size_t key_len = strlen(key);
    size_t val_len = sizeof(struct file_handle) + handle->handle_bytes;
    record->key = key;
    record->val = value;
    record->key_len = key_len;
    record->val_len = val_len;

    // commit first transaction
    int ret = put_id_handle(record, 1, db);
    EXPECT_EQ(ret, 0);
  }

  void init_test_fs(const string& path, db_store_t* db) {
    struct file_handle* handle;
    char* key;

    ASSERT_EQ(initialize_id_manager(db), 0);
    // root for dummy files
    fs::path fsroot = path;
    fs::create_directory(fsroot);
    // insert handle
    handle = path_to_fhandle(fsroot.c_str());
    key = generate_file_id(db);
    insert_handle(key, handle);

    uuid_t baseuuid = buf_to_uuid(key);
    base.id_low = baseuuid.lo;
    base.id_high = baseuuid.hi;

    // create 10 files
    for (int i = 0; i < 10; i++) {
      // insert entry in write_paths
      write_paths.emplace_back(fsroot / ("f" + to_string(i)));
    }

    for (auto& path : write_paths) {
      struct file_handle* handle;

      // create a file
      int fd = open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY);
      ASSERT_EQ(write(fd, original, strlen(original)), strlen(original));
      close(fd);

      // put handle in leveldb
      handle = path_to_fhandle(path.c_str());
      key = generate_file_id(db);
      insert_handle(key, handle);
    }

    // create 10 files
    for (int i = 0; i < 10; i++) {
      // insert entry in write_paths
      create_paths.emplace_back(fsroot / ("c" + to_string(i)));
    }
  }
};

TEST_F(UndoExecutor, WriteTxnWithAbsolutePath) {
  struct TxnLog txn;
  // write txnlog entry
  // with dummy files and populate file handles in leveldb
  txn.compound_type = txn_VWrite;
  txn.txn_id = rand();
  txn.backup_dir_path = bkproot.c_str();
  txn_write_absolute(&txn);

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

  // assert the created paths were removed
  for (auto& path : create_paths) {
    ASSERT_FALSE(fs::exists(path));
  }
}

TEST_F(UndoExecutor, WriteTxnWithBase) {
  struct TxnLog txn;
  // write txnlog entry
  // with dummy files and populate file handles in leveldb
  txn.compound_type = txn_VWrite;
  txn.txn_id = rand();
  txn.backup_dir_path = bkproot.c_str();
  txn_write_base(&txn);

  undo_txn_execute(&txn, db);

  // enumerate backup directory
  // all the backups should have been renamed to original
  ASSERT_EQ(
      std::distance(fs::directory_iterator(bkproot), fs::directory_iterator{}),
      0);

  // check file contents of dummy_file_path should be original
  for (auto& path : write_paths) {
    int fd;
    char buf[80];
    fd = open(path.c_str(), O_RDONLY);
    ASSERT_GT(fd, 2);
    ASSERT_EQ(read(fd, buf, 80), strlen(original));
    ASSERT_STREQ(buf, original);
    close(fd);
  }

  // assert the created paths were removed
  for (auto& path : create_paths) {
    ASSERT_FALSE(fs::exists(path));
  }
}
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
