// vim:noexpandtab:shiftwidth=8:tabstop=8:
#ifndef _TXN_LOGGER_H
#define _TXN_LOGGER_H

#include "nfsv41.h"
#include "lwrapper.h"

namespace txnfs {
namespace proto {
class TransactionLog;
}
}  // namespace txnfs

using namespace txnfs;
#define ENABLE_NORMAL_TEXT_LOGGING 0

#ifdef __cplusplus
extern "C" {
#endif

enum FSObjectType { ft_None, ft_File, ft_Directory, ft_Symlink, ft_Hardlink };

/**
 * This struct assumes |data| is NULL-ended.
 */
struct FileId {
  const char* data;
  enum FSObjectType file_type; /* File or Directory */
  int flags;
};

struct ObjectId {
  uint64_t id_low;
  uint64_t id_high;
  enum FSObjectType file_type; /* File or Directory */
};

struct CreatedObject {
  ObjectId base_id;
  char path[PATH_MAX];
  ObjectId allocated_id;
};

struct UnlinkId {
  ObjectId parent_id;
  char name[NAME_MAX];
};

struct SymlinkId {
  char src_path[PATH_MAX];
  ObjectId parent_id;
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
  const char* src_path;
  const char* dst_path;
  FileId src_fileid;
  FileId dst_fileid;
  bool is_directory;
};

typedef struct RenameId RenameId;

struct TxnLog {
  uint64_t txn_id;
  CreatedObject* created_file_ids;
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

int txn_log_from_pb(proto::TransactionLog* txnpb, struct TxnLog* txn_log);
int txn_log_to_pb(struct TxnLog* txn_log, proto::TransactionLog* txnpb);
void txn_log_free(struct TxnLog* txn_log);
uint64_t create_txn_log(leveldb_t* db, const COMPOUND4args* arg);

#ifdef __cplusplus
}
#endif

#endif
