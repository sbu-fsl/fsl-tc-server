#include <dirent.h>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>
#include <functional>
#include <gtest/gtest.h>
#include <iostream>
#include <set>

#include "txn_logger.h"
#include "txn.pb.h"

using namespace std;

class TxnTest : public ::testing::Test {
 protected:
  TxnLog txn_log;
  proto::TransactionLog txnpb;
  TxnLog deserialized_txn_log;
  int result;
  string id = "file_id_hardcode";  // TODO:generate_file_id();
  string original_path_str = "temp_original_path";
  string backup_name_str = "temp_backup_name";
  string backup_dir_path = "/tmp/log/";
  string src_path_str = "temp_src_path";
  string dst_path_str = "temp_dst_path";
  struct CreatedObject created_file[1];
  struct UnlinkId created_unlinks[1];
  struct SymlinkId created_symlinks[1];
  struct RenameId created_renames[1];
  virtual void SetUp() {
    // create
    created_file[0].base_id.id_low = 123;
    created_file[0].base_id.id_high = 456;
    created_file[0].base_id.file_type = ft_File;

    created_file[0].allocated_id.id_low = 123;
    created_file[0].allocated_id.id_high = 456;
    created_file[0].allocated_id.file_type = ft_File;
    strcpy(created_file[0].path, "/dummy/path");
    txn_log.created_file_ids = created_file;
    txn_log.num_files = 1;

    // unlinks
    created_unlinks[0].parent_id.id_low = 123;
    created_unlinks[0].parent_id.id_high = 456;
    created_unlinks[0].parent_id.file_type = ft_File;
    strcpy(created_unlinks[0].name, "file_to_unlink");
    txn_log.created_unlink_ids = created_unlinks;
    txn_log.num_unlinks = 1;

    // symlinks
    created_symlinks[0].parent_id.id_low = 123;
    created_symlinks[0].parent_id.id_high = 456;
    created_symlinks[0].parent_id.file_type = ft_File;
    strcpy(created_symlinks[0].name, "file_to_symlink");
    txn_log.created_symlink_ids = created_symlinks;
    txn_log.num_symlinks = 1;

    // renames
    created_renames[0].is_directory = true;
    created_renames[0].src_path = src_path_str.c_str();
    created_renames[0].dst_path = dst_path_str.c_str();
    created_renames[0].src_fileid.data = (char *)id.c_str();
    created_renames[0].src_fileid.file_type = ft_Directory;
    created_renames[0].src_fileid.flags = 1;
    created_renames[0].dst_fileid.data = (char *)id.c_str();
    created_renames[0].dst_fileid.file_type = ft_Directory;
    created_renames[0].dst_fileid.flags = 1;
    txn_log.created_rename_ids = created_renames;
    txn_log.num_renames = 1;
  }
  virtual void TearDown() { txn_log_free(&deserialized_txn_log); }

  int compare(TxnLog *txn1, TxnLog *txn2) {
    int ret;
    EXPECT_EQ(txn1->txn_id, txn2->txn_id);
    EXPECT_EQ(txn1->compound_type, txn2->compound_type);

    switch (txn1->compound_type) {
      case txn_VNone:
        ret = 0;
        break;
      case txn_VWrite:
      case txn_VMkdir:
      case txn_VCreate:
        EXPECT_EQ(txn1->num_files, txn2->num_files);
        ret = compare(txn1->created_file_ids, txn2->created_file_ids);
        break;
      case txn_VRename:
        EXPECT_EQ(txn1->num_renames, txn2->num_renames);
        ret = compare(txn1->created_rename_ids, txn2->created_rename_ids);
        break;
      case txn_VUnlink:
        EXPECT_EQ(txn1->num_unlinks, txn2->num_unlinks);
        ret = compare(txn1->created_unlink_ids, txn2->created_unlink_ids);
        break;
      case txn_VSymlink:
        EXPECT_EQ(txn1->num_symlinks, txn2->num_symlinks);
        ret = compare(txn1->created_symlink_ids, txn2->created_symlink_ids);
        break;
    }
    return ret;
  }

  int compare(struct FileId *obj1, struct FileId *obj2) {
    EXPECT_STREQ(obj1->data, obj2->data);
    EXPECT_EQ(obj1->file_type, obj2->file_type);
    return 0;
  }

  int compare(struct ObjectId *obj1, struct ObjectId *obj2) {
    EXPECT_EQ(obj1->id_low, obj2->id_low);
    EXPECT_EQ(obj1->id_high, obj2->id_high);
    EXPECT_EQ(obj1->file_type, obj2->file_type);

    return 0;
  }

  int compare(struct CreatedObject *cobj1, struct CreatedObject *cobj2) {
    EXPECT_STREQ(cobj1->path, cobj2->path);
    EXPECT_EQ(compare(&cobj1->base_id, &cobj2->base_id), 0);
    EXPECT_EQ(compare(&cobj1->allocated_id, &cobj2->allocated_id), 0);
    return 0;
  }

  int compare(struct UnlinkId *uobj1, struct UnlinkId *uobj2) {
    EXPECT_EQ(compare(&uobj1->parent_id, &uobj2->parent_id), 0);
    EXPECT_STREQ(uobj1->name, uobj2->name);
    return 0;
  }

  int compare(struct SymlinkId *sobj1, struct SymlinkId *sobj2) {
    EXPECT_STREQ(sobj1->src_path, sobj2->src_path);
    EXPECT_STREQ(sobj1->name, sobj2->name);
    EXPECT_EQ(compare(&sobj1->parent_id, &sobj2->parent_id), 0);
    return 0;
  }

  int compare(struct RenameId *robj1, struct RenameId *robj2) {
    EXPECT_STREQ(robj1->src_path, robj2->src_path);
    EXPECT_STREQ(robj1->dst_path, robj2->dst_path);

    EXPECT_EQ(0, compare(&robj1->src_fileid, &robj2->src_fileid));
    EXPECT_EQ(0, compare(&robj1->dst_fileid, &robj2->dst_fileid));
    EXPECT_EQ(robj1->is_directory, robj2->is_directory);
    return 0;
  }
};

TEST_F(TxnTest, SimpleTest) {
  proto::TransactionLog txnpb;
  txn_log.txn_id = 9990;
  txn_log.compound_type = txn_VNone;

  txn_log_to_pb(&txn_log, &txnpb);

  EXPECT_EQ(txn_log.txn_id, txnpb.id());
  EXPECT_EQ(txn_log.compound_type, txnpb.type());
}

TEST_F(TxnTest, CreateTest) {
  txn_log.txn_id = 9992;
  txn_log.compound_type = txn_VCreate;

  txn_log_to_pb(&txn_log, &txnpb);
  txn_log_from_pb(&txnpb, &deserialized_txn_log);
  EXPECT_EQ(0, compare(&txn_log, &deserialized_txn_log));
}

TEST_F(TxnTest, MkdirTest) {
  txn_log.txn_id = 9993;
  txn_log.compound_type = txn_VMkdir;

  txn_log_to_pb(&txn_log, &txnpb);
  txn_log_from_pb(&txnpb, &deserialized_txn_log);
  EXPECT_EQ(0, compare(&txn_log, &deserialized_txn_log));
}

TEST_F(TxnTest, WriteTest) {
  txn_log.txn_id = 9994;
  txn_log.compound_type = txn_VWrite;
  txn_log.backup_dir_path = backup_dir_path.c_str();

  txn_log_to_pb(&txn_log, &txnpb);
  txn_log_from_pb(&txnpb, &deserialized_txn_log);
  EXPECT_EQ(0, compare(&txn_log, &deserialized_txn_log));
}

TEST_F(TxnTest, UnlinkTest) {
  txn_log.txn_id = 9995;
  txn_log.compound_type = txn_VUnlink;
  txn_log.backup_dir_path = backup_dir_path.c_str();

  txn_log_to_pb(&txn_log, &txnpb);
  txn_log_from_pb(&txnpb, &deserialized_txn_log);
  EXPECT_EQ(0, compare(&txn_log, &deserialized_txn_log));
}

TEST_F(TxnTest, SymlinkTest) {
  txn_log.txn_id = 9996;
  txn_log.compound_type = txn_VSymlink;

  TxnLog deserialized_txn_log;
  txn_log_to_pb(&txn_log, &txnpb);
  txn_log_from_pb(&txnpb, &deserialized_txn_log);
  EXPECT_EQ(0, compare(&txn_log, &deserialized_txn_log));
}

TEST_F(TxnTest, RenameTest) {
  txn_log.txn_id = 9997;
  txn_log.compound_type = txn_VRename;
  txn_log.backup_dir_path = backup_dir_path.c_str();

  txn_log_to_pb(&txn_log, &txnpb);
  txn_log_from_pb(&txnpb, &deserialized_txn_log);
  EXPECT_EQ(0, compare(&txn_log, &deserialized_txn_log));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
