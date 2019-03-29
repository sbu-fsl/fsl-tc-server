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
    created_file[0].base.id_low = 123;
    created_file[0].base.id_high = 456;
    created_file[0].base.file_type = ft_File;

    created_file[0].allocated_id.id_low = 123;
    created_file[0].allocated_id.id_high = 456;
    created_file[0].allocated_id.file_type = ft_File;

    txn_log.created_file_ids = created_file;
    txn_log.num_files = 1;

    // unlinks
    created_unlinks[0].original_path = original_path_str.c_str();
    created_unlinks[0].backup_name = backup_name_str.c_str();
    txn_log.created_unlink_ids = created_unlinks;
    txn_log.num_unlinks = 1;

    // symlinks
    created_symlinks[0].src_path = src_path_str.c_str();
    created_symlinks[0].dst_path = dst_path_str.c_str();
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
  virtual void TearDown() {}

  int compareStructsCommon(TxnLog txn_log, TxnLog *txn_log_ret) {
    if (txn_log_ret->txn_id != txn_log.txn_id) return 1;
    if (txn_log_ret->compound_type != txn_log.compound_type) return 1;
    return 0;
  }

  int compareStructsFileID(TxnLog txn_log, TxnLog *txn_log_ret) {
    if (txn_log_ret->num_files != txn_log.num_files) return 1;
    struct CreatedObject *created_files, *created_files_ret;
    created_files = txn_log.created_file_ids;
    created_files_ret = txn_log_ret->created_file_ids;
    if (created_files_ret->file_type != created_files->file_type) return 1;
    // if (created_files_ret->flags != created_files->flags) return 1;
    if (id.compare(created_files_ret->data) != 0) return 1;
    return 0;
  }

  int compareStructsUnlinkId(TxnLog txn_log, TxnLog *txn_log_ret) {
    if (txn_log_ret->num_unlinks != txn_log.num_unlinks) return 1;
    struct UnlinkId *created_unlinks_ret;
    created_unlinks_ret = txn_log_ret->created_unlink_ids;
    if (original_path_str.compare(created_unlinks_ret->original_path) != 0)
      return 1;
    if (backup_name_str.compare(created_unlinks_ret->backup_name) != 0)
      return 1;
    return 0;
  }

  int compareStructsSymlinkId(TxnLog txn_log, TxnLog *txn_log_ret) {
    if (txn_log_ret->num_symlinks != txn_log.num_symlinks) return 1;
    struct SymlinkId *created_symlinks_ret;
    created_symlinks_ret = txn_log_ret->created_symlink_ids;
    if (src_path_str.compare(created_symlinks_ret->src_path) != 0) return 1;
    if (dst_path_str.compare(created_symlinks_ret->dst_path) != 0) return 1;
    return 0;
  }

  int compareStructsRenameId(TxnLog txn_log, TxnLog *txn_log_ret) {
    if (txn_log_ret->num_renames != txn_log.num_renames) return 1;
    struct RenameId *created_renames, *created_renames_ret;
    created_renames = txn_log.created_rename_ids;
    created_renames_ret = txn_log_ret->created_rename_ids;
    if (src_path_str.compare(created_renames_ret->src_path) != 0) return 1;
    if (dst_path_str.compare(created_renames_ret->dst_path) != 0) return 1;
    if (id.compare(created_renames_ret->src_fileid.data) != 0) return 1;
    if (id.compare(created_renames_ret->dst_fileid.data) != 0) return 1;
    if (created_renames_ret->src_fileid.file_type !=
        created_renames->src_fileid.file_type)
      return 1;
    if (created_renames_ret->dst_fileid.file_type !=
        created_renames->dst_fileid.file_type)
      return 1;
    if (created_renames->is_directory != created_renames_ret->is_directory)
      return 1;
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

  proto::TransactionLog txnpb;
  TxnLog deserialized_txn_log;
  txn_log_to_pb(&txn_log, &txnpb);
  txn_log_from_pb(&txnpb, &deserialized_txn_log);
  EXPECT_EQ(0, compareStructsCommon(txn_log, &deserialized_txn_log));
  EXPECT_EQ(0, compareStructsFileID(txn_log, &deserialized_txn_log));
  txn_log_free(&deserialized_txn_log);
}

TEST_F(TxnTest, MkdirTest) {
  txn_log.txn_id = 9993;
  txn_log.compound_type = txn_VMkdir;

  proto::TransactionLog txnpb;
  TxnLog deserialized_txn_log;
  txn_log_to_pb(&txn_log, &txnpb);
  txn_log_from_pb(&txnpb, &deserialized_txn_log);
  EXPECT_EQ(0, compareStructsCommon(txn_log, &deserialized_txn_log));
  EXPECT_EQ(0, compareStructsFileID(txn_log, &deserialized_txn_log));
  txn_log_free(&deserialized_txn_log);
}

TEST_F(TxnTest, WriteTest) {
  txn_log.txn_id = 9994;
  txn_log.compound_type = txn_VWrite;
  txn_log.backup_dir_path = backup_dir_path.c_str();

  proto::TransactionLog txnpb;
  TxnLog deserialized_txn_log;
  txn_log_to_pb(&txn_log, &txnpb);
  txn_log_from_pb(&txnpb, &deserialized_txn_log);
  EXPECT_EQ(0, compareStructsCommon(txn_log, &deserialized_txn_log));
  EXPECT_EQ(0, compareStructsFileID(txn_log, &deserialized_txn_log));
  txn_log_free(&deserialized_txn_log);
}

TEST_F(TxnTest, UnlinkTest) {
  txn_log.txn_id = 9995;
  txn_log.compound_type = txn_VUnlink;
  txn_log.backup_dir_path = backup_dir_path.c_str();

  proto::TransactionLog txnpb;
  TxnLog deserialized_txn_log;
  txn_log_to_pb(&txn_log, &txnpb);
  txn_log_from_pb(&txnpb, &deserialized_txn_log);
  EXPECT_EQ(0, compareStructsCommon(txn_log, &deserialized_txn_log));
  EXPECT_EQ(0, compareStructsUnlinkId(txn_log, &deserialized_txn_log));
  txn_log_free(&deserialized_txn_log);
}

TEST_F(TxnTest, SymlinkTest) {
  txn_log.txn_id = 9996;
  txn_log.compound_type = txn_VSymlink;

  proto::TransactionLog txnpb;
  TxnLog deserialized_txn_log;
  txn_log_to_pb(&txn_log, &txnpb);
  txn_log_from_pb(&txnpb, &deserialized_txn_log);
  EXPECT_EQ(0, compareStructsCommon(txn_log, &deserialized_txn_log));
  EXPECT_EQ(0, compareStructsSymlinkId(txn_log, &deserialized_txn_log));
  txn_log_free(&deserialized_txn_log);
}

TEST_F(TxnTest, RenameTest) {
  txn_log.txn_id = 9997;
  txn_log.compound_type = txn_VRename;
  txn_log.backup_dir_path = backup_dir_path.c_str();

  proto::TransactionLog txnpb;
  TxnLog deserialized_txn_log;
  txn_log_to_pb(&txn_log, &txnpb);
  txn_log_from_pb(&txnpb, &deserialized_txn_log);
  EXPECT_EQ(0, compareStructsCommon(txn_log, &deserialized_txn_log));
  EXPECT_EQ(0, compareStructsRenameId(txn_log, &deserialized_txn_log));
  txn_log_free(&deserialized_txn_log);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
