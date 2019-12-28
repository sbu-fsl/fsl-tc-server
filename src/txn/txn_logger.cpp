#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <experimental/filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>

#include <absl/strings/str_cat.h>
#include <absl/strings/str_join.h>

#include "nfs_fh.h"
#include "txn.pb.h"
#include "txn_logger.h"
#include "txn_logger_internal.h"
#define MAX_LEN 64

using namespace std;
using namespace txnfs;
namespace fs = std::experimental::filesystem;

constexpr uint64_t kInvalidTxnId = 0;
const uuid_t kRootUuid = {1, 2,  3,  4,  5,  6,  7,  8,
                          9, 10, 11, 12, 13, 14, 15, 16};

/**
 * @brief returns FileType from txn FSObjectType
 *
 * @param[in] FSObjectType from txn_logger.h
 *
 * @retval -1 FileType
 */
proto::FileType get_file_type_txn(FSObjectType newType) {
  using FileType = proto::FileType;
  switch (newType) {
  case ft_None:
    return FileType::FT_NONE;
  case ft_File:
    return FileType::FT_FILE;
  case ft_Directory:
    return FileType::FT_DIRECTORY;
  case ft_Symlink:
    return FileType::FT_SYMLINK;
  case ft_Hardlink:
    return FileType::FT_HARDLINK;
  default:
    return FileType::FT_NONE;
  }
}

/**
 * @brief returns txn FSObjectType from FileType
 *
 * @param[in] FileType
 *
 * @retval -1 FSObjectType from txn_logger.h
 */
FSObjectType get_file_type(proto::FileType newType) {
  using FileType = proto::FileType;
  switch (newType) {
  case FileType::FT_NONE:
    return ft_None;
  case FileType::FT_FILE:
    return ft_File;
  case FileType::FT_DIRECTORY:
    return ft_Directory;
  case FileType::FT_SYMLINK:
    return ft_Symlink;
  case FileType::FT_HARDLINK:
    return ft_Hardlink;
  default:
    return ft_None;
  }
}

/**
 * @brief helper function to convert array of bytes to hex format
 *
 *  @param[in]: const char *
 *  @retval: char *
 */
char *bytes_to_hex(const char *uuid_str) {
  uint k;
  char *hex_uuid = (char *)calloc(2 * TXN_UUID_LEN + 1, sizeof(char));
  for (k = 0; k < TXN_UUID_LEN; k++) {
    sprintf(&hex_uuid[2 * k], "%02X", (unsigned int)uuid_str[k]);
  }
  return hex_uuid;
}

/**
 * @brief helper function to serialize_txn_log
 * This function fills proto::TransactionLog for transaction involving VCreate
 */
void serialize_create_txn(struct TxnLog *txn_log,
                          proto::TransactionLog *txn_log_obj) {
  txn_log_obj->set_type(proto::TransactionType::VCREATE);
  for (int i = 0; i < txn_log->num_files; i++) {
    proto::VCreateTxn *create_txn = txn_log_obj->mutable_creates();
    proto::CreatedObject *object = create_txn->add_objects();
    struct CreatedObject *txnobj = &txn_log->created_file_ids[i];

    // set FileId
    object->mutable_base()->set_uuid((const char *)txnobj->base_id.id);

    // set FileType
    object->mutable_base()->set_type(
        get_file_type_txn(txnobj->base_id.file_type));

    // set FileId
    object->mutable_allocated_id()->set_uuid(
        (const char *)txnobj->allocated_id.id);

    // set FileType
    object->mutable_allocated_id()->set_type(
        get_file_type_txn(txnobj->allocated_id.file_type));

    object->set_path(txnobj->path);
  }
}

/**
 * @brief helper function to serialize_txn_log
 * This function fills proto::TransactionLog for transaction involving VMkdir
 */
void serialize_mkdir_txn(struct TxnLog *txn_log,
                         proto::TransactionLog *txn_log_obj) {
  txn_log_obj->set_type(proto::TransactionType::VMKDIR);
  for (int i = 0; i < txn_log->num_files; i++) {
    proto::VMkdirTxn *create_txn = txn_log_obj->mutable_mkdirs();
    proto::CreatedObject *object = create_txn->add_dirs();
    struct CreatedObject *txnobj = &txn_log->created_file_ids[i];

    // set FileId
    object->mutable_base()->set_uuid((const char *)txnobj->base_id.id);

    // set FileType
    object->mutable_base()->set_type(
        get_file_type_txn(txnobj->base_id.file_type));

    // set FileId
    object->mutable_allocated_id()->set_uuid(
        (const char *)txnobj->allocated_id.id);

    // set FileType
    object->mutable_allocated_id()->set_type(
        get_file_type_txn(txnobj->allocated_id.file_type));

    object->set_path(txnobj->path);
  }
}

/**
 * @brief helper function to serialize_txn_log
 * This function fills proto::TransactionLog for transaction involving VMkdir
 */
void serialize_write_txn(struct TxnLog *txn_log,
                         proto::TransactionLog *txn_log_obj) {
  txn_log_obj->set_type(proto::TransactionType::VWRITE);
  for (int i = 0; i < txn_log->num_files; i++) {
    proto::VWriteTxn *write_txn = txn_log_obj->mutable_writes();
    write_txn->set_backup_dir_path(txn_log->backup_dir_path);
    proto::CreatedObject *object = write_txn->add_files();
    struct CreatedObject *txnobj = &txn_log->created_file_ids[i];

    // set FileId
    object->mutable_base()->set_uuid((const char *)txnobj->base_id.id);

    // set FileType
    object->mutable_base()->set_type(
        get_file_type_txn(txnobj->base_id.file_type));

    // set FileId
    object->mutable_allocated_id()->set_uuid(
        (const char *)txnobj->allocated_id.id);

    // set FileType
    object->mutable_allocated_id()->set_type(
        get_file_type_txn(txnobj->allocated_id.file_type));

    object->set_path(txnobj->path);
  }
}

/**
 * @brief helper function to serialize_txn_log
 * This function fills proto::TransactionLog for transaction involving Unlink
 */
void serialize_unlink_txn(struct TxnLog *txn_log,
                          proto::TransactionLog *txn_log_obj) {
  txn_log_obj->set_type(proto::TransactionType::VUNLINK);
  proto::VUnlinkTxn *unlink_txn = txn_log_obj->mutable_unlinks();
  unlink_txn->set_backup_dir_path(txn_log->backup_dir_path);
  for (int i = 0; i < txn_log->num_unlinks; i++) {
    proto::VUnlinkTxn_Unlink *unlink_obj = unlink_txn->add_unlinked_objs();
    struct UnlinkId *txnobj = &txn_log->created_unlink_ids[i];

    // set FileId
    unlink_obj->mutable_parent_id()->set_uuid(
        (const char *)txnobj->parent_id.id);

    // set FileType
    unlink_obj->mutable_parent_id()->set_type(
        get_file_type_txn(txnobj->parent_id.file_type));
    unlink_obj->set_name(txnobj->name);
  }
}

/**
 * @brief helper function to serialize_txn_log
 * This function fills proto::TransactionLog for transaction involving Symlink
 */
void serialize_symlink_txn(struct TxnLog *txn_log,
                           proto::TransactionLog *txn_log_obj) {
  txn_log_obj->set_type(proto::TransactionType::VSYMLINK);
  proto::VSymlinkTxn *symlink_txn = txn_log_obj->mutable_symlinks();
  for (int i = 0; i < txn_log->num_unlinks; i++) {
    proto::VSymlinkTxn_Symlink *symlink_obj = symlink_txn->add_linked_objs();
    struct SymlinkId *txnobj = &txn_log->created_symlink_ids[i];
    symlink_obj->set_src_path(txnobj->src_path);

    // set destination
    // set FileId
    symlink_obj->mutable_parent_id()->set_uuid(
        (const char *)txnobj->parent_id.id);

    // set FileType
    symlink_obj->mutable_parent_id()->set_type(
        get_file_type_txn(txnobj->parent_id.file_type));
    symlink_obj->set_name(txnobj->name);
  }
}

/**
 * @brief helper function to serialize_txn_log
 * This function fills proto::TransactionLog for transaction involving VRename
 */
void serialize_rename_txn(struct TxnLog *txn_log,
                          proto::TransactionLog *txn_log_obj) {
  txn_log_obj->set_type(proto::TransactionType::VRENAME);
  proto::VRenameTxn *rename_txn = txn_log_obj->mutable_renames();
  for (int i = 0; i < txn_log->num_renames; i++) {
    proto::VRenameTxn_Rename *rename_obj = rename_txn->add_renamed_objs();
    rename_obj->set_src_path(txn_log->created_rename_ids[i].src_path);
    rename_obj->set_dst_path(txn_log->created_rename_ids[i].dst_path);
    rename_obj->set_is_directory(txn_log->created_rename_ids[i].is_directory);
    proto::FileID *src_fileid = rename_obj->mutable_src_fileid();
    src_fileid->set_id(txn_log->created_rename_ids[i].src_fileid.data);
    src_fileid->set_type(
        get_file_type_txn(txn_log->created_rename_ids[i].src_fileid.file_type));
    proto::FileID *dst_fileid = rename_obj->mutable_dst_fileid();
    dst_fileid->set_id(txn_log->created_rename_ids[i].dst_fileid.data);
    dst_fileid->set_type(
        get_file_type_txn(txn_log->created_rename_ids[i].dst_fileid.file_type));
  }
}

/**
 * @brief helper function to deserialize_txn_log
 * This function fills struct TxnLog from proto::TransactionLog for transaction
 * involving VCreate
 */
void deserialize_create_txn(proto::TransactionLog *txn_log_obj,
                            struct TxnLog *txn_log) {
  txn_log->compound_type = txn_VCreate;
  if (txn_log_obj->has_creates()) {
    const proto::VCreateTxn &create_txn = txn_log_obj->creates();
    txn_log->num_files = create_txn.objects_size();
    txn_log->created_file_ids = (struct CreatedObject *)malloc(
        sizeof(struct CreatedObject) * txn_log->num_files);

    for (int i = 0; i < create_txn.objects_size(); i++) {
      const proto::CreatedObject &object = create_txn.objects(i);
      struct CreatedObject *txnobj = &txn_log->created_file_ids[i];

      // copy base
      memcpy(txnobj->base_id.id, object.base().uuid().c_str(), TXN_UUID_LEN);
      txnobj->base_id.file_type = get_file_type(object.base().type());

      // copy allocated_id
      memcpy(txnobj->allocated_id.id, object.allocated_id().uuid().c_str(),
             TXN_UUID_LEN);
      txnobj->allocated_id.file_type =
          get_file_type(object.allocated_id().type());

      // copy path
      strcpy(txnobj->path, object.path().c_str());
    }
  }
}

/**
 * @brief helper function to deserialize_txn_log
 * This function fills struct TxnLog from proto::TransactionLog for transaction
 * involving VMkdir
 */
void deserialize_mkdir_txn(proto::TransactionLog *txn_log_obj,
                           struct TxnLog *txn_log) {
  txn_log->compound_type = txn_VMkdir;

  if (txn_log_obj->has_mkdirs()) {
    const proto::VMkdirTxn &mkdir_txn = txn_log_obj->mkdirs();
    txn_log->num_files = mkdir_txn.dirs_size();
    txn_log->created_file_ids = (struct CreatedObject *)malloc(
        sizeof(struct CreatedObject) * txn_log->num_files);

    for (int i = 0; i < mkdir_txn.dirs_size(); i++) {
      const proto::CreatedObject &object = mkdir_txn.dirs(i);
      struct CreatedObject *txnobj = &txn_log->created_file_ids[i];

      // copy base
      memcpy(txnobj->base_id.id, object.base().uuid().c_str(), TXN_UUID_LEN);
      txnobj->base_id.file_type = get_file_type(object.base().type());

      // copy allocated_id
      memcpy(txnobj->allocated_id.id, object.allocated_id().uuid().c_str(),
             TXN_UUID_LEN);
      txnobj->allocated_id.file_type =
          get_file_type(object.allocated_id().type());

      // copy path
      strcpy(txnobj->path, object.path().c_str());
    }
  }
}

/**
 * @brief helper function to deserialize_txn_log
 * This function fills struct TxnLog from proto::TransactionLog for transaction
 * involving VWrite
 */
void deserialize_write_txn(proto::TransactionLog *txn_log_obj,
                           struct TxnLog *txn_log) {
  txn_log->compound_type = txn_VWrite;

  if (txn_log_obj->has_writes()) {
    const proto::VWriteTxn &write_txn = txn_log_obj->writes();
    txn_log->backup_dir_path = write_txn.backup_dir_path().c_str();
    txn_log->num_files = write_txn.files_size();
    txn_log->created_file_ids = (struct CreatedObject *)malloc(
        sizeof(struct CreatedObject) * txn_log->num_files);

    for (int i = 0; i < write_txn.files().size(); i++) {
      const proto::CreatedObject &object = write_txn.files(i);
      struct CreatedObject *txnobj = &txn_log->created_file_ids[i];

      // copy base
      memcpy(txnobj->base_id.id, object.base().uuid().c_str(), TXN_UUID_LEN);
      txnobj->base_id.file_type = get_file_type(object.base().type());

      // copy allocated_id
      memcpy(txnobj->allocated_id.id, object.allocated_id().uuid().c_str(),
             TXN_UUID_LEN);
      txnobj->allocated_id.file_type =
          get_file_type(object.allocated_id().type());

      // copy path
      strcpy(txnobj->path, object.path().c_str());
    }
  }
}

/**
 * @brief helper function to deserialize_txn_log
 * This function fills struct TxnLog from proto::TransactionLog for
 * transaction involving VUnlink
 */
void deserialize_unlink_txn(proto::TransactionLog *txn_log_obj,
                            struct TxnLog *txn_log) {
  txn_log->compound_type = txn_VUnlink;

  if (txn_log_obj->has_unlinks()) {
    const proto::VUnlinkTxn &unlink_txn = txn_log_obj->unlinks();
    txn_log->backup_dir_path = unlink_txn.backup_dir_path().c_str();
    txn_log->num_unlinks = unlink_txn.unlinked_objs_size();
    txn_log->created_unlink_ids = (struct UnlinkId *)malloc(
        sizeof(struct UnlinkId) * txn_log->num_unlinks);

    for (int i = 0; i < unlink_txn.unlinked_objs_size(); i++) {
      const proto::VUnlinkTxn_Unlink &object = unlink_txn.unlinked_objs(i);
      struct UnlinkId *txnobj = &txn_log->created_unlink_ids[i];

      // copy parent
      memcpy(txnobj->parent_id.id, object.parent_id().uuid().c_str(),
             TXN_UUID_LEN);
      txnobj->parent_id.file_type = get_file_type(object.parent_id().type());

      // copy name
      strcpy(txnobj->name, object.name().c_str());
    }
  }
}

/**
 * @brief helper function to deserialize_txn_log
 * This function fills struct TxnLog from proto::TransactionLog for
 * transaction involving VSymlink
 */
void deserialize_symlink_txn(proto::TransactionLog *txn_log_obj,
                             struct TxnLog *txn_log) {
  txn_log->compound_type = txn_VSymlink;

  if (txn_log_obj->has_symlinks()) {
    const proto::VSymlinkTxn &symlink_txn = txn_log_obj->symlinks();
    txn_log->num_symlinks = symlink_txn.linked_objs_size();
    txn_log->created_symlink_ids = (struct SymlinkId *)malloc(
        sizeof(struct SymlinkId) * txn_log->num_symlinks);

    for (int i = 0; i < symlink_txn.linked_objs_size(); i++) {
      const proto::VSymlinkTxn_Symlink &object = symlink_txn.linked_objs(i);
      struct SymlinkId *txnobj = &txn_log->created_symlink_ids[i];
      strcpy(txn_log->created_symlink_ids[i].src_path,
             object.src_path().c_str());

      // copy parent
      memcpy(txnobj->parent_id.id, object.parent_id().uuid().c_str(),
             TXN_UUID_LEN);
      txnobj->parent_id.file_type = get_file_type(object.parent_id().type());

      // copy name
      strcpy(txnobj->name, object.name().c_str());
    }
  }
}

/**
 * @brief helper function to deserialize_txn_log
 * This function fills struct TxnLog from proto::TransactionLog for
 * transaction involving VRename
 */
void deserialize_rename_txn(proto::TransactionLog *txn_log_obj,
                            struct TxnLog *txn_log) {
  txn_log->compound_type = txn_VRename;

  if (txn_log_obj->has_renames()) {
    const proto::VRenameTxn &rename_txn = txn_log_obj->renames();
    txn_log->num_renames = rename_txn.renamed_objs_size();
    txn_log->created_rename_ids = (struct RenameId *)malloc(
        sizeof(struct RenameId) * txn_log->num_renames);

    txn_log->backup_dir_path = rename_txn.backup_dir().c_str();

    for (int i = 0; i < rename_txn.renamed_objs_size(); i++) {
      const proto::VRenameTxn_Rename &rename_obj = rename_txn.renamed_objs(i);
      txn_log->created_rename_ids[i].src_path = rename_obj.src_path().c_str();
      txn_log->created_rename_ids[i].dst_path = rename_obj.dst_path().c_str();
      if (rename_obj.has_is_directory())
        txn_log->created_rename_ids[i].is_directory = rename_obj.is_directory();
      const proto::FileID &src_fileid = rename_obj.src_fileid();
      txn_log->created_rename_ids[i].src_fileid.data = src_fileid.id().c_str();
      txn_log->created_rename_ids[i].src_fileid.file_type =
          get_file_type(src_fileid.type());
      if (rename_obj.has_dst_fileid()) {
        const proto::FileID &dst_fileid = rename_obj.dst_fileid();
        txn_log->created_rename_ids[i].dst_fileid.data =
            dst_fileid.id().c_str();
        txn_log->created_rename_ids[i].dst_fileid.file_type =
            get_file_type(dst_fileid.type());
      }
    }
  }
}

int txn_log_to_pb(struct TxnLog *txn_log, proto::TransactionLog *txnpb) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  txnpb->set_id(txn_log->txn_id);
  int ret = 0;
  switch (txn_log->compound_type) {
  case txn_VNone:
    txnpb->set_type(proto::TransactionType::NONE);
    break;
  case txn_VCreate:
    serialize_create_txn(txn_log, txnpb);
    break;
  case txn_VMkdir:
    serialize_mkdir_txn(txn_log, txnpb);
    break;
  case txn_VWrite:
    serialize_write_txn(txn_log, txnpb);
    break;
  case txn_VRename:
    serialize_rename_txn(txn_log, txnpb);
    break;
  case txn_VUnlink:
    serialize_unlink_txn(txn_log, txnpb);
    break;
  case txn_VSymlink:
    serialize_symlink_txn(txn_log, txnpb);
    break;
  default:
    txnpb->set_type(proto::TransactionType::NONE);
    ret = -1;
  }

  google::protobuf::ShutdownProtobufLibrary();
  return ret;
}

int txn_log_from_pb(proto::TransactionLog *txnpb, struct TxnLog *txn_log) {
  int ret = 0;
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  txn_log->txn_id = txnpb->id();
  switch (txnpb->type()) {
  case proto::TransactionType::NONE:
    txn_log->compound_type = txn_VNone;
    break;
  case proto::TransactionType::VCREATE:
    deserialize_create_txn(txnpb, txn_log);
    break;
  case proto::TransactionType::VMKDIR:
    deserialize_mkdir_txn(txnpb, txn_log);
    break;
  case proto::TransactionType::VWRITE:
    deserialize_write_txn(txnpb, txn_log);
    break;
  case proto::TransactionType::VRENAME:
    deserialize_rename_txn(txnpb, txn_log);
    break;
  case proto::TransactionType::VUNLINK:
    deserialize_unlink_txn(txnpb, txn_log);
    break;
  case proto::TransactionType::VSYMLINK:
    deserialize_symlink_txn(txnpb, txn_log);
    break;
  default:
    ret = -1;
  }

  google::protobuf::ShutdownProtobufLibrary();

  return ret;
}

void txn_log_free(struct TxnLog *txn_log) {
  if (!txn_log) {
    return;
  }

  switch (txn_log->compound_type) {
  case txn_VNone:
    break;
  case txn_VWrite:
  case txn_VMkdir:
  case txn_VCreate:
    free(txn_log->created_file_ids);
    break;
  case txn_VRename:
    free(txn_log->created_rename_ids);
    break;
  case txn_VUnlink:
    free(txn_log->created_unlink_ids);
    break;
  case txn_VSymlink:
    free(txn_log->created_symlink_ids);
    break;
  }
}

namespace internal {

proto::TransactionType get_txn_type(const COMPOUND4args *arg) {
  const nfs_argop4 *const ops = arg->argarray.argarray_val;
  const int ops_len = arg->argarray.argarray_len;
  // We assume operations inside a compound are homogeneous, and it is true if
  // it was generated by the vNFS client.
  proto::TransactionType txn_type = proto::TransactionType::NONE;
  for (int i = 0; i < ops_len; ++i) {
    if (ops[i].argop == NFS4_OP_CREATE) {
      txn_type = proto::TransactionType::VCREATE;
      break;
    } else if (ops[i].argop == NFS4_OP_WRITE) {
      txn_type = proto::TransactionType::VWRITE;
      break;
    } else if (ops[i].argop == NFS4_OP_RENAME) {
      txn_type = proto::TransactionType::VRENAME;
      break;
    } else if (ops[i].argop == NFS4_OP_REMOVE) {
      txn_type = proto::TransactionType::VUNLINK;
      break;
    } else if (ops[i].argop == NFS4_OP_LINK) {
      txn_type = proto::TransactionType::VSYMLINK;
      break;
    } else if (ops[i].argop == NFS4_OP_OPEN) {
      if (ops[i].nfs_argop4_u.opopen.openhow.opentype & OPEN4_CREATE) {
        txn_type = proto::TransactionType::VOPEN;
        break;
      }
    }
  }
  return txn_type;
}

uint64_t get_txn_id() {
  // A valid txn id starts from 1. 0 is an invalid Id.
  static std::atomic<uint64_t> next_txn_id{1};
  return next_txn_id++;
}

// TODO: Test this.
void extract_uuid_from_fh(const nfs_fh4 &fh, uuid_t uuid) {
  struct file_handle_v4 *fh4 = (struct file_handle_v4 *)fh.nfs_fh4_val;
  assert(fh4->fs_len == sizeof(uuid_t));
  uuid_copy(uuid, fh4->fsopaque);
}

string uuid_to_string(const uuid_t &uuid) {
  return string(uuid, uuid + sizeof(uuid));
}

string nfs4_string(const utf8string &us) {
  return string(us.utf8string_val, us.utf8string_len);
}

int build_vcreate_txn(const COMPOUND4args *arg, proto::VCreateTxn *create_txn) {
  const nfs_argop4 *const ops = arg->argarray.argarray_val;
  const int ops_len = arg->argarray.argarray_len;
  uuid_t base;
  std::vector<string> path_components;
  for (int i = 0; i < ops_len; ++i) {
    if (ops[i].argop == NFS4_OP_PUTROOTFH) {
      uuid_copy(base, kRootUuid);
      // uuid_copy(context->op_contexts[i].id, base);
      // context->op_contexts[i].is_new = false;
    } else if (ops[i].argop == NFS4_OP_PUTFH) {
      extract_uuid_from_fh(ops[i].nfs_argop4_u.opputfh.object, base);
      // uuid_copy(context->op_contexts[i].id, base);
      // context->op_contexts[i].is_new = false;
    } else if (ops[i].argop == NFS4_OP_LOOKUP) {
      path_components.emplace_back(
          nfs4_string(ops[i].nfs_argop4_u.oplookup.objname));
    } else if (ops[i].argop == NFS4_OP_LOOKUPP) {
      path_components.push_back("..");
    } else if (ops[i].argop == NFS4_OP_CREATE) {
      path_components.emplace_back(
          nfs4_string(ops[i].nfs_argop4_u.opcreate.objname));
      proto::CreatedObject *obj = create_txn->add_objects();
      proto::ObjectId *base_id = obj->mutable_base();
      base_id->set_uuid((const char *)0);
      base_id->set_type(proto::FileType::FT_DIRECTORY);
      *base_id->mutable_uuid() = uuid_to_string(base);
      *obj->mutable_path() = absl::StrJoin(path_components, "/");

      // uuid_generate(context->op_contexts[i].id);
      // context->op_contexts[i].is_new = true;
    } else {
      std::cerr << "Unknow operation for VCreate: " << ops[i].argop;
    }
  }
  return 0;
}

int build_vwrite_txn(const COMPOUND4args *arg, proto::VWriteTxn *write_txn) {
  const nfs_argop4 *const ops = arg->argarray.argarray_val;
  const int ops_len = arg->argarray.argarray_len;
  uuid_t base;
  std::vector<string> path_components;
  for (int i = 0; i < ops_len; ++i) {
    if (ops[i].argop == NFS4_OP_PUTROOTFH) {
      uuid_copy(base, kRootUuid);
      // uuid_copy(context->op_contexts[i].id, base);
      // context->op_contexts[i].is_new = false;
    } else if (ops[i].argop == NFS4_OP_PUTFH) {
      extract_uuid_from_fh(ops[i].nfs_argop4_u.opputfh.object, base);
      // uuid_copy(context->op_contexts[i].id, base);
      // context->op_contexts[i].is_new = false;
    } else if (ops[i].argop == NFS4_OP_LOOKUP) {
      path_components.emplace_back(
          nfs4_string(ops[i].nfs_argop4_u.oplookup.objname));
    } else if (ops[i].argop == NFS4_OP_LOOKUPP) {
      path_components.push_back("..");
    } else if (ops[i].argop == NFS4_OP_OPEN) {
      const OPEN4args *open_args = &ops[i].nfs_argop4_u.opopen;
      path_components.emplace_back(
          nfs4_string(open_args->claim.open_claim4_u.file));
      proto::CreatedObject *file = write_txn->add_files();
      proto::ObjectId *base_id = file->mutable_base();
      base_id->set_uuid((const char *)0);
      base_id->set_type(proto::FileType::FT_DIRECTORY);
      *base_id->mutable_uuid() = uuid_to_string(base);
      *file->mutable_path() = absl::StrJoin(path_components, "/");
      if (open_args->openhow.opentype == OPEN4_CREATE) {
        // uuid_generate(context->op_contexts[i].id);
        // context->op_contexts[i].is_new = true;
      }
    } else {
      std::cerr << "Unknow operation for VWrite: " << ops[i].argop;
    }
  }
  return 0;
}

} // namespace internal

bool check_rename(const db_store_t *db, const COMPOUND4args *arg,
    const char *root) {
  fs::path root_path = std::string(root);
  fs::path current_path = std::string(root);
  fs::path saved_path = std::string(root);
  const struct nfs_argop4 *argarray = arg->argarray.argarray_val;
  int n = arg->argarray.argarray_len;

  for (int i = 0; i < n; ++i) {
    int op = argarray[i].argop;
    switch (op) {
      case NFS4_OP_PUTFH: {
        /* PUTFH: query the abs. path associated with the request
         * file handle (UUID) */
        const nfs_fh4 raw_fh = argarray[i].nfs_argop4_u.opputfh.object;
        const file_handle_v4_t *fh = (file_handle_v4_t *)raw_fh.nfs_fh4_val;
        /* key = "path-{UUID}" */
        char *key = new char[fh->fs_len + 5];
        memcpy(key, "path-", 5);
        memcpy(key + 5, fh->fsopaque, fh->fs_len);
        size_t pathlen;
        char *val, *error;
        val = leveldb_get(db->db, db->r_options, key, fh->fs_len + 5, &pathlen,
            &error);
        if (error) {
          delete key;
          /* Database error: return EIO */
          return false;
        } else {
          current_path = std::string(val, pathlen);
          delete key;
        }
        break;
      }

      case NFS4_OP_SAVEFH: {
        /* SAVEFH: assign the content of saved_path to that of current_path */
        saved_path = current_path;
        break;
      }

      case NFS4_OP_RESTOREFH: {
        /* RESTOREFH: assign the content of current_path to that of saved_path*/
        current_path = saved_path;
        break;
      }

      case NFS4_OP_LOOKUP: {
        utf8string u8name = argarray[i].nfs_argop4_u.oplookup.objname;
        std::string name(u8name.utf8string_val, u8name.utf8string_len);
        fs::path new_cur_path(current_path);
        new_cur_path /= name;
        current_path = new_cur_path;
        break;
      }

      case NFS4_OP_LOOKUPP: {
        const fs::path new_cur_path(current_path);
        current_path = new_cur_path.parent_path();
        break;
      }

      case NFS4_OP_RENAME: {
        fs::path src_path(current_path);
        utf8string u8name = argarray[i].nfs_argop4_u.oprename.oldname;
        std::string name(u8name.utf8string_val, u8name.utf8string_len);
        src_path /= name;
        fs::file_status src_stat = fs::status(src_path);
        if (src_stat.type() == fs::file_type::none ||
            src_stat.type() == fs::file_type::not_found ||
            src_stat.type() == fs::file_type::directory ||
            src_stat.type() == fs::file_type::unknown)
          return false;
        break; 
      }   
    } /* switch(op) */
  } /* for */
  /* OK if the loop did not break */
  return true;
}

uint64_t create_txn_log(const db_store_t *db, const COMPOUND4args *arg,
    const char *root_path) {
  proto::TransactionLog txn_log;

  txn_log.set_type(internal::get_txn_type(arg));

  if (txn_log.type() == proto::TransactionType::NONE) {
    /* If the compound is read-only or ineligible for transaction,
     * return immediately without doing expensive leveldb_write */
    return kInvalidTxnId;
  }

  if (txn_log.type() == proto::TransactionType::VRENAME) {
    if (check_rename(db, arg, root_path) == false)
      return kInvalidTxnId;
  }

  txn_log.set_id(internal::get_txn_id());
  if (txn_log.type() == proto::TransactionType::VCREATE) {
     internal::build_vcreate_txn(arg, txn_log.mutable_creates());
  } else if (txn_log.type() == proto::TransactionType::VWRITE) {
     internal::build_vwrite_txn(arg, txn_log.mutable_writes());
  }

  const string key = absl::StrCat("txn-", txn_log.id());

  // Write txn_log to database.
  std::ostringstream output;
  if (!txn_log.SerializeToOstream(&output)) {
    std::cerr << "Failed to serialize txn log.";
    return kInvalidTxnId;
  }
  const string value = output.str();

  char *err_msg = nullptr;
  leveldb_put(db->db, db->w_options, key.data(), key.size(), value.data(),
              value.size(), &err_msg);
  if (err_msg != nullptr) {
    std::cerr << "Failed to write txn log: " << err_msg;
    free(err_msg);
    return kInvalidTxnId;
  }

  return txn_log.id();
}
