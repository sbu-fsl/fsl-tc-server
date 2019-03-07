// vim:noexpandtab:shiftwidth=8:tabstop=8:
#ifndef _TXN_LOGGER_H
#define _TXN_LOGGER_H

#include "nfsv41.h"

namespace txnfs
{
  namespace proto 
  {
    class TransactionLog;
  }
}

using namespace txnfs;
#define ENABLE_NORMAL_TEXT_LOGGING 0

#ifdef __cplusplus
extern "C" {
#endif

enum FSObjectType {
	ft_None,
	ft_File,
	ft_Directory,
	ft_Symlink,
	ft_Hardlink
};

/**
 * This struct assumes |data| is NULL-ended.
 */
struct FileId {
	char* data;
	enum FSObjectType file_type; /* File or Directory */
	int flags;		     /* created or not */
};

struct UnlinkId {
	const char* original_path;
	const char* backup_name;
};

struct SymlinkId {
	const char* src_path;
	const char* dst_path;
};

enum CompoundType {
	txn_VNone,
	txn_VCreate,
	txn_VMkdir,
	txn_VWrite,
	txn_VRename,
	txn_VUnlink,
	txn_VSymlink
};

typedef struct FileId FileId;
typedef struct UnlinkId UnlinkId;
typedef struct SymlinkId SymlinkId;

struct RenameId {
	const char* src_path;
	const char* dst_path;
	FileId src_fileid;
	FileId dst_fileid;
	bool is_directory;
};

typedef struct RenameId RenameId;

struct TxnLog {
	uint64_t txn_id;
	FileId* created_file_ids;
	UnlinkId* created_unlink_ids;
	SymlinkId* created_symlink_ids;
	RenameId* created_rename_ids;
	int num_files;
	int num_unlinks;
	int num_symlinks;
	int num_renames;
	enum CompoundType compound_type;
	const char* backup_dir_path;
};

typedef struct TxnLog TxnLog;
struct TxnLog* read_txn_log(uint64_t txn_id, const char* inputdir);
int write_txn_log(struct TxnLog* txn_log, const char* inputdir);
int log_processor(struct TxnLog* log);
int remove_txn_log(uint64_t txn_id, const char* inputdir);
int iterate_txn_logs(const char* log_dir,
		     int (*log_processor)(struct TxnLog* log));
char *bytes_to_hex(const char *uuid_str);

int txn_log_from_pb(proto::TransactionLog* txnpb, struct TxnLog* txn_log);
int txn_log_to_pb(struct TxnLog* txn_log, proto::TransactionLog* txnpb);
#ifdef __cplusplus
}
#endif

#endif
