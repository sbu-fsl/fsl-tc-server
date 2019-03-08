#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>

#include "id_manager.h"
#include "txn_logger.h"
#include "txn.pb.h"
#define MAX_LEN 64

using namespace std;
using namespace txnfs;
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
char* bytes_to_hex(const char* uuid_str) {
  int k;
  char* hex_uuid = (char*)calloc(2 * TXN_UUID_LEN + 1, sizeof(char));
  for (k = 0; k < TXN_UUID_LEN; k++) {
    sprintf(&hex_uuid[2 * k], "%02X", (unsigned int)uuid_str[k]);
  }
  return hex_uuid;
}

/**
 * @brief helper function to serialize_txn_log
 * This function fills proto::TransactionLog for transaction involving VCreate
 */
void serialize_create_txn(struct TxnLog* txn_log, proto::TransactionLog* txn_log_obj) {
  txn_log_obj->set_type(proto::TransactionType::VCREATE);
  for (int i = 0; i < txn_log->num_files; i++) {
    proto::VCreateTxn* create_txn = txn_log_obj->mutable_creates();
    if (txn_log->created_file_ids[i].flags) {
      proto::FileID* file_id = create_txn->add_created_files();
      file_id->set_id(txn_log->created_file_ids[i].data);
      if (ENABLE_NORMAL_TEXT_LOGGING &&
          txn_log->created_file_ids[i].data != NULL) {
        char* hex_uuid = bytes_to_hex(txn_log->created_file_ids[i].data);
        file_id->set_id((const char*)hex_uuid);
      }
      file_id->set_type(
          get_file_type_txn(txn_log->created_file_ids[i].file_type));
    }
  }
}

/**
 * @brief helper function to serialize_txn_log
 * This function fills proto::TransactionLog for transaction involving VMkdir
 */
void serialize_mkdir_txn(struct TxnLog* txn_log, proto::TransactionLog* txn_log_obj) {
  txn_log_obj->set_type(proto::TransactionType::VMKDIR);
  for (int i = 0; i < txn_log->num_files; i++) {
    proto::VMkdirTxn* mkdir_txn = txn_log_obj->mutable_mkdirs();
    if (txn_log->created_file_ids[i].flags) {
      proto::FileID* file_id = mkdir_txn->add_created_dirs();
      file_id->set_id(txn_log->created_file_ids[i].data);
      if (ENABLE_NORMAL_TEXT_LOGGING &&
          txn_log->created_file_ids[i].data != NULL) {
        char* hex_uuid = bytes_to_hex(txn_log->created_file_ids[i].data);
        file_id->set_id((const char*)hex_uuid);
      }
      file_id->set_type(
          get_file_type_txn(txn_log->created_file_ids[i].file_type));
    }
  }
}

/**
 * @brief helper function to serialize_txn_log
 * This function fills proto::TransactionLog for transaction involving VMkdir
 */
void serialize_write_txn(struct TxnLog* txn_log, proto::TransactionLog* txn_log_obj) {
  txn_log_obj->set_type(proto::TransactionType::VWRITE);
  for (int i = 0; i < txn_log->num_files; i++) {
    proto::VWriteTxn* write_txn = txn_log_obj->mutable_writes();
    write_txn->set_backup_dir_path(txn_log->backup_dir_path);
    if (txn_log->created_file_ids[i].flags) {
      proto::FileID* file_id = write_txn->add_created_files();
      file_id->set_id(txn_log->created_file_ids[i].data);
      if (ENABLE_NORMAL_TEXT_LOGGING &&
          txn_log->created_file_ids[i].data != NULL) {
        char* hex_uuid = bytes_to_hex(txn_log->created_file_ids[i].data);
        file_id->set_id((const char*)hex_uuid);
      }
      file_id->set_type(
          get_file_type_txn(txn_log->created_file_ids[i].file_type));
    }
  }
}

/**
 * @brief helper function to serialize_txn_log
 * This function fills proto::TransactionLog for transaction involving Unlink
 */
void serialize_unlink_txn(struct TxnLog* txn_log, proto::TransactionLog* txn_log_obj) {
  txn_log_obj->set_type(proto::TransactionType::VUNLINK);
  proto::VUnlinkTxn* unlink_txn = txn_log_obj->mutable_unlinks();
  unlink_txn->set_backup_dir_path(txn_log->backup_dir_path);
  for (int i = 0; i < txn_log->num_unlinks; i++) {
    proto::VUnlinkTxn_Unlink* unlink_obj = unlink_txn->add_unlinked_objs();
    unlink_obj->set_original_path(txn_log->created_unlink_ids[i].original_path);
    unlink_obj->set_backup_name(txn_log->created_unlink_ids[i].backup_name);
  }
}

/**
 * @brief helper function to serialize_txn_log
 * This function fills proto::TransactionLog for transaction involving Symlink
 */
void serialize_symlink_txn(struct TxnLog* txn_log, proto::TransactionLog* txn_log_obj) {
  txn_log_obj->set_type(proto::TransactionType::VSYMLINK);
  proto::VSymlinkTxn* symlink_txn = txn_log_obj->mutable_symlinks();
  for (int i = 0; i < txn_log->num_unlinks; i++) {
    proto::VSymlinkTxn_Symlink* symlink_obj = symlink_txn->add_linked_objs();
    symlink_obj->set_src_path(txn_log->created_symlink_ids[i].src_path);
    symlink_obj->set_dst_path(txn_log->created_symlink_ids[i].dst_path);
  }
}

/**
 * @brief helper function to serialize_txn_log
 * This function fills proto::TransactionLog for transaction involving VRename
 */
void serialize_rename_txn(struct TxnLog* txn_log, proto::TransactionLog* txn_log_obj) {
  txn_log_obj->set_type(proto::TransactionType::VRENAME);
  proto::VRenameTxn* rename_txn = txn_log_obj->mutable_renames();
  for (int i = 0; i < txn_log->num_renames; i++) {
    proto::VRenameTxn_Rename* rename_obj = rename_txn->add_renamed_objs();
    rename_obj->set_src_path(txn_log->created_rename_ids[i].src_path);
    rename_obj->set_dst_path(txn_log->created_rename_ids[i].dst_path);
    rename_obj->set_is_directory(txn_log->created_rename_ids[i].is_directory);
    proto::FileID* src_fileid = rename_obj->mutable_src_fileid();
    src_fileid->set_id(txn_log->created_rename_ids[i].src_fileid.data);
    src_fileid->set_type(
        get_file_type_txn(txn_log->created_rename_ids[i].src_fileid.file_type));
    proto::FileID* dst_fileid = rename_obj->mutable_dst_fileid();
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
void deserialize_create_txn(proto::TransactionLog* txn_log_obj, struct TxnLog* txn_log) {
  txn_log->compound_type = txn_VCreate;
  if (txn_log_obj->has_creates()) {
    const proto::VCreateTxn& create_txn = txn_log_obj->creates();
    txn_log->num_files = create_txn.created_files_size();
    txn_log->created_file_ids = (struct FileId*) malloc(sizeof(struct FileId) * txn_log->num_files);
    
    for (int i = 0; i < create_txn.created_files_size(); i++) {
      const proto::FileID& file_id = create_txn.created_files(i);
      txn_log->created_file_ids[i].data = file_id.id().c_str();
      if (file_id.has_type())
        txn_log->created_file_ids[i].file_type =
            get_file_type(file_id.type());
      txn_log->created_file_ids[i].flags = 1;
    }
  }
}

/**
 * @brief helper function to deserialize_txn_log
 * This function fills struct TxnLog from proto::TransactionLog for transaction
 * involving VMkdir
 */
void deserialize_mkdir_txn(proto::TransactionLog* txn_log_obj, struct TxnLog* txn_log) {
  txn_log->compound_type = txn_VMkdir;

  if (txn_log_obj->has_mkdirs()) {
    const proto::VMkdirTxn& mkdir_txn = txn_log_obj->mkdirs();
    txn_log->num_files = mkdir_txn.created_dirs_size();
    txn_log->created_file_ids = (struct FileId*) malloc(sizeof(struct FileId) * txn_log->num_files);
    
    for (int i = 0; i < mkdir_txn.created_dirs_size(); i++) {
      const proto::FileID& file_id = mkdir_txn.created_dirs(i);
      txn_log->created_file_ids[i].data = file_id.id().c_str();
      if (file_id.has_type())
        txn_log->created_file_ids[i].file_type =
            get_file_type(file_id.type());
      txn_log->created_file_ids[i].flags = 1;
    }
  }
}

/**
 * @brief helper function to deserialize_txn_log
 * This function fills struct TxnLog from proto::TransactionLog for transaction
 * involving VWrite
 */
void deserialize_write_txn(proto::TransactionLog* txn_log_obj, struct TxnLog* txn_log) {
  txn_log->compound_type = txn_VWrite;

  if (txn_log_obj->has_writes()) {
    const proto::VWriteTxn& write_txn = txn_log_obj->writes();
    txn_log->backup_dir_path = write_txn.backup_dir_path().c_str();
    txn_log->num_files = write_txn.created_files_size();
    txn_log->created_file_ids = (struct FileId*) malloc(sizeof(struct FileId) * txn_log->num_files);
    
    for (int i = 0; i < write_txn.created_files_size(); i++) {
      const proto::FileID& file_id = write_txn.created_files(i);
      txn_log->created_file_ids[i].data = file_id.id().c_str();
      if (file_id.has_type())
        txn_log->created_file_ids[i].file_type =
            get_file_type(file_id.type());
      txn_log->created_file_ids[i].flags = 1;
    }
  }
}

/**
 * @brief helper function to deserialize_txn_log
 * This function fills struct TxnLog from proto::TransactionLog for transaction
 * involving VUnlink
 */
void deserialize_unlink_txn(proto::TransactionLog* txn_log_obj, struct TxnLog* txn_log) {
  txn_log->compound_type = txn_VUnlink;

  if (txn_log_obj->has_unlinks()) {
    const proto::VUnlinkTxn& unlink_txn = txn_log_obj->unlinks();
    txn_log->backup_dir_path = unlink_txn.backup_dir_path().c_str();
    txn_log->num_unlinks = unlink_txn.unlinked_objs_size();
    txn_log->created_unlink_ids = (struct UnlinkId*) malloc(sizeof(struct UnlinkId) * txn_log->num_unlinks);
    
    for (int i = 0; i < unlink_txn.unlinked_objs_size(); i++) {
      const proto::VUnlinkTxn_Unlink& unlink_obj = unlink_txn.unlinked_objs(i);
      txn_log->created_unlink_ids[i].original_path =
          unlink_obj.original_path().c_str();
      txn_log->created_unlink_ids[i].backup_name =
          unlink_obj.backup_name().c_str();
    }
  }
}

/**
 * @brief helper function to deserialize_txn_log
 * This function fills struct TxnLog from proto::TransactionLog for transaction
 * involving VSymlink
 */
void deserialize_symlink_txn(proto::TransactionLog* txn_log_obj, struct TxnLog* txn_log) {
  txn_log->compound_type = txn_VSymlink;

  if (txn_log_obj->has_symlinks()) {
    const proto::VSymlinkTxn& symlink_txn = txn_log_obj->symlinks();
    txn_log->num_symlinks = symlink_txn.linked_objs_size();
    txn_log->created_symlink_ids = (struct SymlinkId*) malloc(sizeof(struct SymlinkId) * txn_log->num_symlinks);
    
    for (int i = 0; i < symlink_txn.linked_objs_size(); i++) {
      const proto::VSymlinkTxn_Symlink& link_obj = symlink_txn.linked_objs(i);
      txn_log->created_symlink_ids[i].src_path = link_obj.src_path().c_str();
      txn_log->created_symlink_ids[i].dst_path = link_obj.dst_path().c_str();
    }
  }
}

/**
 * @brief helper function to deserialize_txn_log
 * This function fills struct TxnLog from proto::TransactionLog for transaction
 * involving VRename
 */
void deserialize_rename_txn(proto::TransactionLog* txn_log_obj, struct TxnLog* txn_log) {
  txn_log->compound_type = txn_VRename;

  if (txn_log_obj->has_renames()) {
    const proto::VRenameTxn& rename_txn = txn_log_obj->renames();
    txn_log->num_renames = rename_txn.renamed_objs_size();
    txn_log->created_rename_ids = (struct RenameId*) malloc(sizeof(struct RenameId) * txn_log->num_renames);
  
    txn_log->backup_dir_path = rename_txn.backup_dir().c_str();
    
    for (int i = 0; i < rename_txn.renamed_objs_size(); i++) {
      const proto::VRenameTxn_Rename& rename_obj = rename_txn.renamed_objs(i);
      txn_log->created_rename_ids[i].src_path = rename_obj.src_path().c_str();
      txn_log->created_rename_ids[i].dst_path = rename_obj.dst_path().c_str();
      if (rename_obj.has_is_directory())
        txn_log->created_rename_ids[i].is_directory = rename_obj.is_directory();
      const proto::FileID& src_fileid = rename_obj.src_fileid();
      txn_log->created_rename_ids[i].src_fileid.data = src_fileid.id().c_str();
      txn_log->created_rename_ids[i].src_fileid.file_type = get_file_type(src_fileid.type());
      if (rename_obj.has_dst_fileid()) {
        const proto::FileID& dst_fileid = rename_obj.dst_fileid();
        txn_log->created_rename_ids[i].dst_fileid.data =
            dst_fileid.id().c_str();
        txn_log->created_rename_ids[i].dst_fileid.file_type =
            get_file_type(dst_fileid.type());
      }
    }
  }
}

int txn_log_to_pb(struct TxnLog* txn_log, proto::TransactionLog *txnpb) {
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

int txn_log_from_pb(proto::TransactionLog* txnpb, struct TxnLog* txn_log) {
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


void txn_log_free(struct TxnLog* txn_log)
{
    if (!txn_log)
    {
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

