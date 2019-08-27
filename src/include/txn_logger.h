// vim:noexpandtab:shiftwidth=8:tabstop=8:
#ifndef _TXN_LOGGER_H
#define _TXN_LOGGER_H

#include "lwrapper.h"
#include "nfsv41.h"
#include "txn_context.h"
#include "txnfs.h"
#include "uuid/uuid.h"

#define ENABLE_NORMAL_TEXT_LOGGING 0

#ifdef __cplusplus
extern "C" {
#endif

enum FSObjectType { ft_None, ft_File, ft_Directory, ft_Symlink, ft_Hardlink };

/**
 * This struct assumes |data| is NULL-ended.
 */
struct FileId {
	const char *data;
	enum FSObjectType file_type; /* File or Directory */
	int flags;
};

struct ObjectId {
	uuid_t id;
	enum FSObjectType file_type; /* File or Directory */
};

struct CreatedObject {
	struct ObjectId base_id;
	char path[PATH_MAX];
	struct ObjectId allocated_id;
};

struct UnlinkId {
	struct ObjectId parent_id;
	char name[NAME_MAX];
};

struct SymlinkId {
	char src_path[PATH_MAX];
	struct ObjectId parent_id;
	char name[NAME_MAX];
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
	const char *src_path;
	const char *dst_path;
	struct FileId src_fileid;
	struct FileId dst_fileid;
	bool is_directory;
};

typedef struct RenameId RenameId;

struct TxnLog {
	uint64_t txn_id;
	struct CreatedObject *created_file_ids;
	struct UnlinkId *created_unlink_ids;
	struct SymlinkId *created_symlink_ids;
	struct RenameId *created_rename_ids;
	int num_files;
	int num_unlinks;
	int num_symlinks;
	int num_renames;
	enum CompoundType compound_type;
	const char *backup_dir_path;
};

typedef struct TxnLog TxnLog;
uint64_t create_txn_log(const db_store_t *db, const COMPOUND4args *arg,
			enum txnfs_txn_type *txn_type);

#ifdef __cplusplus
}
#endif

#endif
