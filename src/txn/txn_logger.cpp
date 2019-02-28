#include <cstdlib>
#include <dirent.h>

#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>

#include "id_manager.h"
#include "txn.pb.h"
#include "txn_logger.h"
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
 * @brief helper function to write_txn_log
 * This function fills proto::TransactionLog for transaction involving VCreate
 */
void write_create_txn(struct TxnLog* txn_log, proto::TransactionLog* txn_log_obj) {
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
 * @brief helper function to write_txn_log
 * This function fills proto::TransactionLog for transaction involving VMkdir
 */
void write_mkdir_txn(struct TxnLog* txn_log, proto::TransactionLog* txn_log_obj) {
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
 * @brief helper function to write_txn_log
 * This function fills proto::TransactionLog for transaction involving VMkdir
 */
void write_write_txn(struct TxnLog* txn_log, proto::TransactionLog* txn_log_obj) {
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
 * @brief helper function to write_txn_log
 * This function fills proto::TransactionLog for transaction involving Unlink
 */
void write_unlink_txn(struct TxnLog* txn_log, proto::TransactionLog* txn_log_obj) {
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
 * @brief helper function to write_txn_log
 * This function fills proto::TransactionLog for transaction involving Symlink
 */
void write_symlink_txn(struct TxnLog* txn_log, proto::TransactionLog* txn_log_obj) {
  txn_log_obj->set_type(proto::TransactionType::VSYMLINK);
  proto::VSymlinkTxn* symlink_txn = txn_log_obj->mutable_symlinks();
  for (int i = 0; i < txn_log->num_unlinks; i++) {
    proto::VSymlinkTxn_Symlink* symlink_obj = symlink_txn->add_linked_objs();
    symlink_obj->set_src_path(txn_log->created_symlink_ids[i].src_path);
    symlink_obj->set_dst_path(txn_log->created_symlink_ids[i].dst_path);
  }
}

/**
 * @brief helper function to write_txn_log
 * This function fills proto::TransactionLog for transaction involving VRename
 */
void write_rename_txn(struct TxnLog* txn_log, proto::TransactionLog* txn_log_obj) {
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
 * @brief helper function to read_txn_log
 * This function fills struct TxnLog from proto::TransactionLog for transaction
 * involving VCreate
 */
void read_create_txn(proto::TransactionLog* txn_log_obj, struct TxnLog* txn_log) {
  txn_log->txn_id = txn_log_obj->id();
  struct FileId created_files[MAX_LEN];
  int num_created_files = 0;
  txn_log->compound_type = txn_VCreate;
  if (txn_log_obj->has_creates()) {
    proto::VCreateTxn create_txn = txn_log_obj->creates();
    for (int i = 0; i < create_txn.created_files_size(); i++) {
      proto::FileID file_id = create_txn.created_files(i);
      created_files[num_created_files].data = file_id.id().c_str();
      if (file_id.has_type())
        created_files[num_created_files].file_type =
            get_file_type(file_id.type());
      created_files[num_created_files].flags = 1;
      num_created_files++;
    }
    txn_log->created_file_ids = created_files;
    txn_log->num_files = num_created_files;
  }
}

/**
 * @brief helper function to read_txn_log
 * This function fills struct TxnLog from proto::TransactionLog for transaction
 * involving VMkdir
 */
void read_mkdir_txn(proto::TransactionLog* txn_log_obj, struct TxnLog* txn_log) {
  txn_log->txn_id = txn_log_obj->id();
  struct FileId created_files[MAX_LEN];
  int num_created_files = 0;
  txn_log->compound_type = txn_VMkdir;

  if (txn_log_obj->has_mkdirs()) {
    proto::VMkdirTxn mkdir_txn = txn_log_obj->mkdirs();
    for (int i = 0; i < mkdir_txn.created_dirs_size(); i++) {
      proto::FileID file_id = mkdir_txn.created_dirs(i);
      created_files[num_created_files].data = file_id.id().c_str();
      if (file_id.has_type())
        created_files[num_created_files].file_type =
            get_file_type(file_id.type());
      created_files[num_created_files].flags = 1;
      num_created_files++;
    }
  }
  txn_log->created_file_ids = created_files;
  txn_log->num_files = num_created_files;
}

/**
 * @brief helper function to read_txn_log
 * This function fills struct TxnLog from proto::TransactionLog for transaction
 * involving VWrite
 */
void read_write_txn(proto::TransactionLog* txn_log_obj, struct TxnLog* txn_log) {
  txn_log->txn_id = txn_log_obj->id();
  struct FileId created_files[MAX_LEN];
  int num_created_files = 0;
  txn_log->compound_type = txn_VWrite;

  if (txn_log_obj->has_writes()) {
    proto::VWriteTxn write_txn = txn_log_obj->writes();
    txn_log->backup_dir_path = write_txn.backup_dir_path().c_str();
    for (int i = 0; i < write_txn.created_files_size(); i++) {
      proto::FileID file_id = write_txn.created_files(i);
      created_files[num_created_files].data = file_id.id().c_str();
      if (file_id.has_type())
        created_files[num_created_files].file_type =
            get_file_type(file_id.type());
      created_files[num_created_files].flags = 1;
      num_created_files++;
    }
  }
  txn_log->created_file_ids = created_files;
  txn_log->num_files = num_created_files;
}

/**
 * @brief helper function to read_txn_log
 * This function fills struct TxnLog from proto::TransactionLog for transaction
 * involving VUnlink
 */
void read_unlink_txn(proto::TransactionLog* txn_log_obj, struct TxnLog* txn_log) {
  txn_log->txn_id = txn_log_obj->id();
  struct UnlinkId created_unlinks[MAX_LEN];
  int num_created_unlinks = 0;
  txn_log->compound_type = txn_VUnlink;

  if (txn_log_obj->has_unlinks()) {
    proto::VUnlinkTxn unlink_txn = txn_log_obj->unlinks();
    txn_log->backup_dir_path = unlink_txn.backup_dir_path().c_str();
    for (int i = 0; i < unlink_txn.unlinked_objs_size(); i++) {
      proto::VUnlinkTxn_Unlink unlink_obj = unlink_txn.unlinked_objs(i);
      created_unlinks[num_created_unlinks].original_path =
          unlink_obj.original_path().c_str();
      created_unlinks[num_created_unlinks].backup_name =
          unlink_obj.backup_name().c_str();
      num_created_unlinks++;
    }
  }
  txn_log->created_unlink_ids = created_unlinks;
  txn_log->num_unlinks = num_created_unlinks;
}

/**
 * @brief helper function to read_txn_log
 * This function fills struct TxnLog from proto::TransactionLog for transaction
 * involving VSymlink
 */
void read_symlink_txn(proto::TransactionLog* txn_log_obj, struct TxnLog* txn_log) {
  txn_log->txn_id = txn_log_obj->id();
  struct SymlinkId created_symlinks[MAX_LEN];
  int num_created_symlinks = 0;
  txn_log->compound_type = txn_VSymlink;

  if (txn_log_obj->has_symlinks()) {
    proto::VSymlinkTxn symlink_txn = txn_log_obj->symlinks();
    for (int i = 0; i < symlink_txn.linked_objs_size(); i++) {
      proto::VSymlinkTxn_Symlink link_obj = symlink_txn.linked_objs(i);
      created_symlinks[num_created_symlinks].src_path =
          link_obj.src_path().c_str();
      created_symlinks[num_created_symlinks].dst_path =
          link_obj.dst_path().c_str();
      num_created_symlinks++;
    }
  }
  txn_log->created_symlink_ids = created_symlinks;
  txn_log->num_symlinks = num_created_symlinks;
}

/**
 * @brief helper function to read_txn_log
 * This function fills struct TxnLog from proto::TransactionLog for transaction
 * involving VRename
 */
void read_rename_txn(proto::TransactionLog* txn_log_obj, struct TxnLog* txn_log) {
  txn_log->txn_id = txn_log_obj->id();
  struct RenameId created_renames[MAX_LEN];
  int num_created_renames = 0;
  txn_log->compound_type = txn_VRename;

  if (txn_log_obj->has_renames()) {
    proto::VRenameTxn rename_txn = txn_log_obj->renames();
    txn_log->backup_dir_path = rename_txn.backup_dir().c_str();
    for (int i = 0; i < rename_txn.renamed_objs_size(); i++) {
      proto::VRenameTxn_Rename rename_obj = rename_txn.renamed_objs(i);
      created_renames[num_created_renames].src_path =
          rename_obj.src_path().c_str();
      created_renames[num_created_renames].dst_path =
          rename_obj.dst_path().c_str();
      if (rename_obj.has_is_directory())
        created_renames[num_created_renames].is_directory =
            rename_obj.is_directory();
      proto::FileID src_fileid = rename_obj.src_fileid();
      created_renames[num_created_renames].src_fileid.data =
          src_fileid.id().c_str();
      created_renames[num_created_renames].src_fileid.file_type =
          get_file_type(src_fileid.type());
      if (rename_obj.has_dst_fileid()) {
        proto::FileID dst_fileid = rename_obj.dst_fileid();
        created_renames[num_created_renames].dst_fileid.data =
            dst_fileid.id().c_str();
        created_renames[num_created_renames].dst_fileid.file_type =
            get_file_type(dst_fileid.type());
      }
      num_created_renames++;
    }
  }
  txn_log->created_rename_ids = created_renames;
  txn_log->num_renames = num_created_renames;
}

/**
 * @brief main function to write transaction log
 * This function writes log for transaction involving VCreate
 * The log file gets created in /var/log/ directory by default if the path not
 *given
 *
 * @param[in] struct TxnLog
 * @param[in] inputdir where the log file is to be written, default is
 *"/var/log"
 *
 * @retval -1 on failure
 * @retval 0 on success
 */
int write_txn_log(struct TxnLog* txn_log, const char* inputdir) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  if (txn_log == nullptr) return -1;
  int ret = 0;
  proto::TransactionLog* txn_log_obj = new proto::TransactionLog();
  string txn_log_file_name(inputdir);
  string txn_log_txt_file_name(inputdir);
  if (strcmp(inputdir, "") == 0) {
    txn_log_file_name = "/var/log/txn_" + to_string(txn_log->txn_id);
    if (ENABLE_NORMAL_TEXT_LOGGING) {
      txn_log_txt_file_name =
          "/var/log/normal_txn_" + to_string(txn_log->txn_id);
    }
  } else {
    txn_log_file_name += "/txn_" + to_string(txn_log->txn_id);
    if (ENABLE_NORMAL_TEXT_LOGGING) {
      txn_log_txt_file_name += "/normal_txn_" + to_string(txn_log->txn_id);
    }
  }
  fstream input(txn_log_file_name.c_str(), ios::in | ios::binary);
  if (!input) {
    cout << txn_log_file_name << ": File not found. Creating a new file"
         << endl;
  } else if (!txn_log_obj->ParseFromIstream(&input)) {
    std::cerr << "Failed to parse existing file";
    return -1;
  }
  if (ENABLE_NORMAL_TEXT_LOGGING) {
    fstream input_normal(txn_log_txt_file_name.c_str(), ios::in);
    if (!input_normal) {
      cout << txn_log_txt_file_name << ": File not found. Creating a new file"
           << endl;
    } else if (!txn_log_obj->ParseFromIstream(&input_normal)) {
      std::cerr << "Failed to parse existing file";
      return -1;
    }
  }
  txn_log_obj->set_id(txn_log->txn_id);

  switch (txn_log->compound_type) {
    case txn_VNone:
      txn_log_obj->set_type(proto::TransactionType::NONE);
      break;
    case txn_VCreate:
      write_create_txn(txn_log, txn_log_obj);
      break;
    case txn_VMkdir:
      write_mkdir_txn(txn_log, txn_log_obj);
      break;
    case txn_VWrite:
      write_write_txn(txn_log, txn_log_obj);
      break;
    case txn_VRename:
      write_rename_txn(txn_log, txn_log_obj);
      break;
    case txn_VUnlink:
      write_unlink_txn(txn_log, txn_log_obj);
      break;
    case txn_VSymlink:
      write_symlink_txn(txn_log, txn_log_obj);
      break;
    default:
      txn_log_obj->set_type(proto::TransactionType::NONE);
      return -1;
  }

  fstream output(txn_log_file_name.c_str(),
                 ios::out | ios::trunc | ios::binary);
  if (!txn_log_obj->SerializeToOstream(&output)) {
    cout << "Failed to log transaction" << endl;
    ret = -1;
  }
  if (ENABLE_NORMAL_TEXT_LOGGING) {
    fstream normal_output(txn_log_txt_file_name.c_str(), ios::out | ios::trunc);
    if (!txn_log_obj->SerializeToOstream(&normal_output)) {
      cout << "Failed to log transaction" << endl;
      ret = -1;
    }
  }
  google::protobuf::ShutdownProtobufLibrary();
  return ret;
}

/**
 * @brief main function to read transaction log
 * This function reads log file and fills struct TxnLog
 *
 * @param[in] transaction id
 * @param[in] inputdir from where the log file is to be read, default is
 *"/var/log"
 *
 * @retval struct TxnLog
 */
struct TxnLog* read_txn_log(uint64_t txn_id, const char* inputdir) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  string txn_log_file_name(inputdir);
  if (strcmp(inputdir, "") == 0)
    txn_log_file_name = "/var/log/txn_" + to_string(txn_id);
  else
    txn_log_file_name += "/txn_" + to_string(txn_id);

  fstream input(txn_log_file_name.c_str(), ios::in | ios::binary);
  proto::TransactionLog* txn_log_obj = new proto::TransactionLog();
  struct TxnLog* txn_log = (struct TxnLog*)calloc(1, sizeof(struct TxnLog));

  if (!input) {
    cout << txn_log_file_name << ": File not found.  Creating a new file."
         << endl;
  } else if (!txn_log_obj->ParseFromIstream(&input)) {
    cout << "Failed to parse the file" << endl;
    return nullptr;
  }

  switch (txn_log_obj->type()) {
    case proto::TransactionType::NONE:
      txn_log->compound_type = txn_VNone;
      break;
    case proto::TransactionType::VCREATE:
      read_create_txn(txn_log_obj, txn_log);
      break;
    case proto::TransactionType::VMKDIR:
      read_mkdir_txn(txn_log_obj, txn_log);
      break;
    case proto::TransactionType::VWRITE:
      read_write_txn(txn_log_obj, txn_log);
      break;
    case proto::TransactionType::VRENAME:
      read_rename_txn(txn_log_obj, txn_log);
      break;
    case proto::TransactionType::VUNLINK:
      read_unlink_txn(txn_log_obj, txn_log);
      break;
    case proto::TransactionType::VSYMLINK:
      read_symlink_txn(txn_log_obj, txn_log);
      break;
    default:
      return nullptr;
  }

  google::protobuf::ShutdownProtobufLibrary();

  return txn_log;
}

/**
 * @brief dummy function to check if it gets invoked while iterate_txn_logs
 *
 * @param[in] struct TxnLog*
 *
 * @retval 0
 */
int log_processor(struct TxnLog* log) {
  cout << "log_processor execution started" << endl;
  cout << "txn_id:" << log->txn_id << endl;
  return 0;
}

/**
 * @brief main function to remove transaction log
 * This function deletes log file
 *
 * @param[in] transaction id
 *
 * @retval -1 on failure
 * @retval 0 on success
 */
int remove_txn_log(uint64_t txn_id, const char* inputdir) {
  string txn_log_file_name(inputdir);
  if (strcmp(inputdir, "") == 0)
    txn_log_file_name = "/var/log/txn_" + to_string(txn_id);
  else
    txn_log_file_name += "/txn_" + to_string(txn_id);
  FILE* fp = fopen(txn_log_file_name.c_str(), "r");
  if (fp) {
    remove(txn_log_file_name.c_str());
    return 0;
  }
  return -1;
}

/**
 * @brief function to iterate over a given directory and count the number of
 *valid transaction logs present
 *
 * @param[in] log_dir : input directory path
 *
 * @retval -1 if directory not valid
 * @retval exact count otherwise
 */
int iterate_txn_logs(const char* log_dir,
                     // Ming: consider change |log_processor| to std::function
                     // so that it can be either a function pointer, a lambda,
                     // or a functor object.
                     int (*log_processor)(struct TxnLog* log)) {
  struct dirent* d;
  int count = 0;
  if (log_dir == nullptr) return -1;
  DIR* dir = opendir(log_dir);
  if (!dir) {
    std::cerr << log_dir << " directory does not exist";
    return -1;
  }
  while ((d = readdir(dir)) != NULL) {
    if ((strlen(d->d_name) > 4) && (strncmp(d->d_name, "txn_", 4) == 0)) {
      const char* t = &(d->d_name[4]);
      char* endptr;
      long txn_id = 0;
      if (t != nullptr) {
        txn_id = strtol(t, &endptr, 10);
        if ((txn_id <= LONG_MIN) || (txn_id >= LONG_MAX))
          continue;
        else {
          struct TxnLog* txn_log_ret =
              (struct TxnLog*)calloc(1, sizeof(struct TxnLog));
          txn_log_ret = read_txn_log((int)txn_id, log_dir);
          if (txn_log_ret != nullptr) {
            log_processor(txn_log_ret);
            count++;
          }
        }
      }
    }
  }
  closedir(dir);
  return count;
}
