#include <sys/stat.h>
#include <stdlib.h>
#include <set>
#include <gtest/gtest.h>
#include "txn_logger.h"
#include <iostream>
#include <fstream>
#include <string>
#include <dirent.h>
#include <unistd.h>
#include <functional>

using namespace std;

class TxnTest : public ::testing::Test {
       protected:
	TxnLog txn_log;
	int result;
	string id = "file_id_hardcode";  // TODO:generate_file_id();
	string original_path_str = "temp_original_path";
	string backup_name_str = "temp_backup_name";
	string backup_dir_path = "/var/log/";
	string src_path_str = "temp_src_path";
	string dst_path_str = "temp_dst_path";
	struct FileId created_file[1];
	struct UnlinkId created_unlinks[1];
	struct SymlinkId created_symlinks[1];
	struct RenameId created_renames[1];
	virtual void SetUp() {
		created_file[0].data = id.c_str();
		created_file[0].file_type = ft_File;
		created_file[0].flags = 1;
		txn_log.created_file_ids = created_file;
		txn_log.num_files = 1;
		created_unlinks[0].original_path = original_path_str.c_str();
		created_unlinks[0].backup_name = backup_name_str.c_str();
		txn_log.created_unlink_ids = created_unlinks;
		txn_log.num_unlinks = 1;
		created_symlinks[0].src_path = src_path_str.c_str();
		created_symlinks[0].dst_path = dst_path_str.c_str();
		txn_log.created_symlink_ids = created_symlinks;
		txn_log.num_symlinks = 1;
		created_renames[0].is_directory = true;
		created_renames[0].src_path = src_path_str.c_str();
		created_renames[0].dst_path = dst_path_str.c_str();
		created_renames[0].src_fileid.data = id.c_str();
		created_renames[0].src_fileid.file_type = ft_Directory;
		created_renames[0].src_fileid.flags = 1;
		created_renames[0].dst_fileid.data = id.c_str();
		created_renames[0].dst_fileid.file_type = ft_Directory;
		created_renames[0].dst_fileid.flags = 1;
		txn_log.created_rename_ids = created_renames;
		txn_log.num_renames = 1;
	}
	int compareStructsCommon(TxnLog txn_log, TxnLog *txn_log_ret) {
		if (txn_log_ret->txn_id != txn_log.txn_id) return 1;
		if (txn_log_ret->compound_type != txn_log.compound_type)
			return 1;
		return 0;
	}
	int compareStructsFileID(TxnLog txn_log, TxnLog *txn_log_ret) {
		if (txn_log_ret->num_files != txn_log.num_files) return 1;
		struct FileId *created_files, *created_files_ret;
		created_files = txn_log.created_file_ids;
		created_files_ret = txn_log_ret->created_file_ids;
		if (created_files_ret->file_type != created_files->file_type)
			return 1;
		if (created_files_ret->flags != created_files->flags) return 1;
		if (id.compare(created_files_ret->data) != 0) return 1;
		return 0;
	}
	int compareStructsUnlinkId(TxnLog txn_log, TxnLog *txn_log_ret) {
		if (txn_log_ret->num_unlinks != txn_log.num_unlinks) return 1;
		struct UnlinkId *created_unlinks, *created_unlinks_ret;
		created_unlinks = txn_log.created_unlink_ids;
		created_unlinks_ret = txn_log_ret->created_unlink_ids;
		if (original_path_str.compare(
			created_unlinks_ret->original_path) != 0)
			return 1;
		if (backup_name_str.compare(created_unlinks_ret->backup_name) !=
		    0)
			return 1;
		return 0;
	}
	int compareStructsSymlinkId(TxnLog txn_log, TxnLog *txn_log_ret) {
		if (txn_log_ret->num_symlinks != txn_log.num_symlinks) return 1;
		struct SymlinkId *created_symlinks, *created_symlinks_ret;
		created_symlinks = txn_log.created_symlink_ids;
		created_symlinks_ret = txn_log_ret->created_symlink_ids;
		if (src_path_str.compare(created_symlinks_ret->src_path) != 0)
			return 1;
		if (dst_path_str.compare(created_symlinks_ret->dst_path) != 0)
			return 1;
		return 0;
	}
	int compareStructsRenameId(TxnLog txn_log, TxnLog *txn_log_ret) {
		if (txn_log_ret->num_renames != txn_log.num_renames) return 1;
		struct RenameId *created_renames, *created_renames_ret;
		created_renames = txn_log.created_rename_ids;
		created_renames_ret = txn_log_ret->created_rename_ids;
		if (src_path_str.compare(created_renames_ret->src_path) != 0)
			return 1;
		if (dst_path_str.compare(created_renames_ret->dst_path) != 0)
			return 1;
		if (id.compare(created_renames_ret->src_fileid.data) != 0)
			return 1;
		if (id.compare(created_renames_ret->dst_fileid.data) != 0)
			return 1;
		if (created_renames_ret->src_fileid.file_type !=
		    created_renames->src_fileid.file_type)
			return 1;
		if (created_renames_ret->dst_fileid.file_type !=
		    created_renames->dst_fileid.file_type)
			return 1;
		if (created_renames->is_directory !=
		    created_renames_ret->is_directory)
			return 1;
		return 0;
	}
};

TEST_F(TxnTest, SimpleTest) {
	txn_log.txn_id = 9990;
	txn_log.compound_type = txn_VNone;
	EXPECT_EQ(0, write_txn_log(&txn_log, "/var/log"));
	EXPECT_EQ(0, remove_txn_log(9990, "/var/log"));
}

#if 0
TEST_F(TxnTest, CreateTest) {
	txn_log.txn_id = 9992;
	txn_log.compound_type = txn_VCreate;
	string txn_log_file = "/var/log/txn_" + to_string(9992);
	FILE *fp = fopen(txn_log_file.c_str(), "r");
	if (fp) {
		remove(txn_log_file.c_str());
	}
	EXPECT_EQ(0, write_txn_log(&txn_log, "/var/log"));
	result = 0;
	TxnLog *txn_log_ret = read_txn_log(9992, "/var/log");
	result = compareStructsCommon(txn_log, txn_log_ret);
	EXPECT_EQ(0, result);
	result = compareStructsFileID(txn_log, txn_log_ret);
	EXPECT_EQ(0, result);
	remove(txn_log_file.c_str());
}

TEST_F(TxnTest, MkdirTest) {
	txn_log.txn_id = 9993;
	txn_log.compound_type = txn_VMkdir;
	string txn_log_file = "/var/log/txn_" + to_string(9993);
	FILE *fp = fopen(txn_log_file.c_str(), "r");
	if (fp) {
		remove(txn_log_file.c_str());
	}
	EXPECT_EQ(0, write_txn_log(&txn_log, "/var/log"));
	result = 0;
	TxnLog *txn_log_ret = read_txn_log(9993, "/var/log");
	result = compareStructsCommon(txn_log, txn_log_ret);
	EXPECT_EQ(0, result);
	result = compareStructsFileID(txn_log, txn_log_ret);
	EXPECT_EQ(0, result);
	remove(txn_log_file.c_str());
}

TEST_F(TxnTest, WriteTest) {
	txn_log.txn_id = 9994;
	txn_log.compound_type = txn_VWrite;
	txn_log.backup_dir_path = backup_dir_path.c_str();
	string txn_log_file = "/var/log/txn_" + to_string(9994);
	FILE *fp = fopen(txn_log_file.c_str(), "r");
	if (fp) {
		remove(txn_log_file.c_str());
	}
	EXPECT_EQ(0, write_txn_log(&txn_log, "/var/log"));
	int result = 0;
	TxnLog *txn_log_ret = read_txn_log(9994, "/var/log");
	if (backup_dir_path.compare(txn_log_ret->backup_dir_path) != 0)
		result = 1;
	result = compareStructsCommon(txn_log, txn_log_ret);
	EXPECT_EQ(0, result);
	result = compareStructsFileID(txn_log, txn_log_ret);
	EXPECT_EQ(0, result);
	remove(txn_log_file.c_str());
}

TEST_F(TxnTest, UnlinkTest) {
	txn_log.txn_id = 9995;
	txn_log.compound_type = txn_VUnlink;
	txn_log.backup_dir_path = backup_dir_path.c_str();
	string txn_log_file = "/var/log/txn_" + to_string(9995);
	FILE *fp = fopen(txn_log_file.c_str(), "r");
	if (fp) {
		remove(txn_log_file.c_str());
	}
	EXPECT_EQ(0, write_txn_log(&txn_log, "/var/log"));
	int result = 0;
	TxnLog *txn_log_ret = read_txn_log(9995, "/var/log");
	if (backup_dir_path.compare(txn_log_ret->backup_dir_path) != 0)
		result = 1;
	result = compareStructsUnlinkId(txn_log, txn_log_ret);
	EXPECT_EQ(0, result);
	remove(txn_log_file.c_str());
}

TEST_F(TxnTest, SymlinkTest) {
	txn_log.txn_id = 9996;
	txn_log.compound_type = txn_VSymlink;
	string txn_log_file = "/var/log/txn_" + to_string(9996);
	FILE *fp = fopen(txn_log_file.c_str(), "r");
	if (fp) {
		remove(txn_log_file.c_str());
	}
	EXPECT_EQ(0, write_txn_log(&txn_log, "/var/log"));
	int result = 0;
	TxnLog *txn_log_ret = read_txn_log(9996, "/var/log");
	result = compareStructsCommon(txn_log, txn_log_ret);
	EXPECT_EQ(0, result);
	result = compareStructsSymlinkId(txn_log, txn_log_ret);
	EXPECT_EQ(0, result);
	remove(txn_log_file.c_str());
}

TEST_F(TxnTest, RenameTest) {
	txn_log.txn_id = 9997;
	txn_log.compound_type = txn_VRename;
	txn_log.backup_dir_path = backup_dir_path.c_str();
	string txn_log_file = "/var/log/txn_" + to_string(9997);
	FILE *fp = fopen(txn_log_file.c_str(), "r");
	if (fp) {
		remove(txn_log_file.c_str());
	}
	EXPECT_EQ(0, write_txn_log(&txn_log, "/var/log"));
	int result = 0;
	TxnLog *txn_log_ret = read_txn_log(9997, "/var/log");
	if (backup_dir_path.compare(txn_log_ret->backup_dir_path) != 0)
		result = 1;
	result = compareStructsCommon(txn_log, txn_log_ret);
	EXPECT_EQ(0, result);
	result = compareStructsRenameId(txn_log, txn_log_ret);
	EXPECT_EQ(0, result);
	remove_txn_log(9997, "/var/log");
}

TEST_F(TxnTest, IterateTxnTest) {
	const char *dir_name = "/var/log/testdir1";
	int ret = mkdir(dir_name, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	if (ret == -1) {
		cout << "Error while creating directory" << endl;
		exit(1);
	}
	txn_log.compound_type = txn_VCreate;
	txn_log.txn_id = 9998;
	string txn_log_file(dir_name);
	FILE *fp =
	    fopen((txn_log_file + "/txn_" + to_string(9998)).c_str(), "r");
	if (fp) {
		remove((txn_log_file + "/txn_" + to_string(9998)).c_str());
	}
	EXPECT_EQ(0, write_txn_log(&txn_log, dir_name));
	txn_log.txn_id = 9999;
	fp = fopen((txn_log_file + "/txn_" + to_string(9999)).c_str(), "r");
	if (fp) {
		remove((txn_log_file + "/txn_" + to_string(9999)).c_str());
	}
	EXPECT_EQ(0, write_txn_log(&txn_log, dir_name));
	txn_log.txn_id = 10000;
	fp = fopen((txn_log_file + "/txn_" + to_string(10000)).c_str(), "r");
	if (fp) {
		remove((txn_log_file + "/txn_" + to_string(10000)).c_str());
	}
	EXPECT_EQ(0, write_txn_log(&txn_log, dir_name));
	ret = iterate_txn_logs(dir_name, log_processor);
	EXPECT_EQ(3, ret);
	remove_txn_log(9998, dir_name);
	remove_txn_log(9999, dir_name);
	remove_txn_log(10000, dir_name);
	ret = rmdir(dir_name);
	EXPECT_EQ(0, ret);
}

// Ming: Can you add one more test that create some logs first and verify that
// iterate_txn_logs can sucessfully iterate all those logs. Then, after
// removing one of the logs, the rest but the removed ones can still be
// iteratoed.
//
// Also, add a fake log_processor (say record invocation using a std::map<int,
// int> where the key is txn_id and the value is the number of times the txn_id
// has been used when calling log_processor) and make sure the fake
// log_processor is actually invoked against every log entry.

TEST_F(TxnTest, MixedTest1) {
	txn_log.compound_type = txn_VCreate;
	txn_log.txn_id = 9998;
	string txn_log_file = "/var/log/txn_" + to_string(9998);
	FILE *fp = fopen(txn_log_file.c_str(), "r");
	if (fp) {
		remove(txn_log_file.c_str());
	}
	EXPECT_EQ(0, write_txn_log(&txn_log, "/var/log"));
	txn_log.txn_id = 9999;
	txn_log_file = "/var/log/txn_" + to_string(9999);
	fp = fopen(txn_log_file.c_str(), "r");
	if (fp) {
		remove(txn_log_file.c_str());
	}
	EXPECT_EQ(0, write_txn_log(&txn_log, "/var/log"));
	txn_log.txn_id = 10000;
	txn_log_file = "/var/log/txn_" + to_string(10000);
	fp = fopen(txn_log_file.c_str(), "r");
	if (fp) {
		remove(txn_log_file.c_str());
	}
	EXPECT_EQ(0, write_txn_log(&txn_log, "/var/log"));
	remove_txn_log(9999, "/var/log");
	result = 0;
	txn_log.txn_id = 9998;
	TxnLog *txn_log_ret = read_txn_log(9998, "/var/log");
	result = compareStructsCommon(txn_log, txn_log_ret);
	EXPECT_EQ(0, result);
	result = compareStructsFileID(txn_log, txn_log_ret);
	EXPECT_EQ(0, result);
	remove_txn_log(9998, "/var/log");
	txn_log.txn_id = 10000;
	txn_log_ret = read_txn_log(10000, "/var/log");
	result = compareStructsCommon(txn_log, txn_log_ret);
	EXPECT_EQ(0, result);
	result = compareStructsFileID(txn_log, txn_log_ret);
	EXPECT_EQ(0, result);
	remove_txn_log(10000, "/var/log");
}

TEST_F(TxnTest, MixedTest2) {
	const char *dir_name = "/var/log/testdir2";
	int ret = mkdir(dir_name, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	if (ret == -1) {
		cout << "Error while creating directory" << endl;
		exit(1);
	}
	txn_log.compound_type = txn_VCreate;
	txn_log.txn_id = 10001;
	string txn_log_file(dir_name);
	FILE *fp =
	    fopen((txn_log_file + "/txn_" + to_string(10001)).c_str(), "r");
	if (fp) {
		remove((txn_log_file + "/txn_" + to_string(10001)).c_str());
	}
	EXPECT_EQ(0, write_txn_log(&txn_log, dir_name));
	
	txn_log.txn_id = 10002;
	fp = fopen((txn_log_file + "/txn_" + to_string(10002)).c_str(), "r");
	if (fp) {
		remove((txn_log_file + "/txn_" + to_string(10002)).c_str());
	}
	EXPECT_EQ(0, write_txn_log(&txn_log, dir_name));
	
	txn_log.txn_id = 10003;
	fp = fopen((txn_log_file + "/txn_" + to_string(10003)).c_str(), "r");
	if (fp) {
		remove((txn_log_file + "/txn_" + to_string(10003)).c_str());
	}
	EXPECT_EQ(0, write_txn_log(&txn_log, dir_name));
	
	ret = iterate_txn_logs(dir_name, log_processor);
	EXPECT_EQ(3, ret);
	
	remove_txn_log(10002, dir_name);
	ret = iterate_txn_logs(dir_name, log_processor);
	EXPECT_EQ(2, ret);
	
	remove_txn_log(10001, dir_name);
	remove_txn_log(10003, dir_name);
}

TEST_F(TxnTest, MixedTest3) {
	const char *dir_name = "/var/log/testdir1";
	int ret = mkdir(dir_name, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	if (ret == -1) {
		cout << "Error while creating directory" << endl;
		exit(1);
	}
	txn_log.compound_type = txn_VCreate;
	txn_log.txn_id = 9998;
	string txn_log_file(dir_name);
	FILE *fp =
	    fopen((txn_log_file + "/txn_" + to_string(9998)).c_str(), "r");
	if (fp) {
		remove((txn_log_file + "/txn_" + to_string(9998)).c_str());
	}
	EXPECT_EQ(0, write_txn_log(&txn_log, dir_name));
	txn_log.txn_id = 9999;
	fp = fopen((txn_log_file + "/txn_" + to_string(9999)).c_str(), "r");
	if (fp) {
		remove((txn_log_file + "/txn_" + to_string(9999)).c_str());
	}
	EXPECT_EQ(0, write_txn_log(&txn_log, dir_name));
	txn_log.txn_id = 10000;
	fp = fopen((txn_log_file + "/txn_" + to_string(10000)).c_str(), "r");
	if (fp) {
		remove((txn_log_file + "/txn_" + to_string(10000)).c_str());
	}
	EXPECT_EQ(0, write_txn_log(&txn_log, dir_name));
	ret = iterate_txn_logs(dir_name, log_processor);
	EXPECT_EQ(3, ret);
	remove_txn_log(9999, dir_name);
	ret = iterate_txn_logs(dir_name, log_processor);
	EXPECT_EQ(2, ret);
	remove_txn_log(9998, dir_name);
	remove_txn_log(10000, dir_name);
	ret = rmdir(dir_name);
	EXPECT_EQ(0, ret);
}
#endif

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	DIR *dir = opendir("/var/log");
	if (!dir) {
		std::cerr << "/var/log"
			  << " directory does not exist";
		return -1;
	}
	return RUN_ALL_TESTS();
}
