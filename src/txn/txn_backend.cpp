#include <algorithm>
#include <iostream>
#include <fstream>
#include <string>
#include <experimental/filesystem>
#include <cassert>
#include <leveldb/db.h>
#include "txn_logger.h"
#include "txn_backend.h"
#include "txn.pb.h"

using namespace std;
namespace fs = std::experimental::filesystem;
leveldb::DB* db;

static const char* fstxn_backend_root = "/tmp/fstxn/";
static const char* fstxn_txnfilename = "txn.data";

static const char* ldbtxn_key_prefix = "txn_";

std::string ldbtxn_get_key(uint64_t txn_id)
{
  stringstream ss;
  ss << ldbtxn_key_prefix << txn_id;
  return ss.str();
}

void ldbtxn_init(void)
{
  leveldb::Options options;
  options.create_if_missing = true;
  leveldb::Status status = leveldb::DB::Open(options, "/tmp/testdb", &db);
  assert(status.ok());
}

int ldbtxn_create_txn(uint64_t txn_id, struct TxnLog* txn)
{
  proto::TransactionLog txnpb;
  std::string value;
  // serialize to protobuf
  if (txn_log_to_pb(txn, &txnpb))
  {
    std::cerr << "Failed to serialize to protobuf" << std::endl;
    return -1;
  }

  if (!txnpb.SerializeToString(&value))
  {
    std::cerr << "Failed to serialize protbuf to string" << std::endl;
    return -1;
  }
  
  leveldb::Status status = db->Put(leveldb::WriteOptions(), ldbtxn_get_key(txn_id), value);
  if (!status.ok())
  {
    std::cerr << "Failed to write txn to leveldb" << std::endl;
    return -1;
  }

  return 0;
}

int ldbtxn_get_txn(uint64_t txn_id, struct TxnLog* txn)
{
  std::string value;
  leveldb::Status status = db->Get(leveldb::ReadOptions(), ldbtxn_get_key(txn_id), &value);
  if (!status.ok())
  {
    std::cerr << "Failed to read txn to leveldb" << std::endl;
    return -1;
  }
    
  proto::TransactionLog txnpb;

  // parse protobuf
  if (!txnpb.ParseFromString(value)) 
  {
    std::cerr << "Failed to parse protbuf" << std::endl;
    return -1;
  }
  
  // convert to TxnLog
  if (txn_log_from_pb(&txnpb, txn) < 0)
  {
    std::cerr << "Failed to deserialize from protobuf" << std::endl;
  }
  return 0;
}

void ldbtxn_enumerate_txn(void (*callback)(struct TxnLog* txn))
{
  leveldb::Iterator* it = db->NewIterator(leveldb::ReadOptions());

  for (it->SeekToFirst(); it->Valid() && it->key().starts_with(ldbtxn_key_prefix); it->Next()) {
      struct TxnLog txn;
      uint64_t txn_id = stoull(it->key().ToString().substr(strlen(ldbtxn_key_prefix)));
      if (ldbtxn_get_txn(txn_id, &txn) < 0)
      {
        std::cerr << "Failed to read transaction" << txn_id << endl;
      }
      callback(&txn);
  }
  
  delete it;
}

int ldbtxn_remove_txn(uint64_t txn_id)
{
  leveldb::Status status = db->Delete(leveldb::WriteOptions(), ldbtxn_get_key(txn_id));
  if (!status.ok())
  {
    std::cerr << "Failed to read txn to leveldb" << std::endl;
    return -1;
  }

  return 0;
}

void ldbtxn_shutdown(void)
{
  delete db;
}

fs::path fstxn_get_txndir(uint64_t txn_id)
{
  fs::path txnpath = fstxn_backend_root;
  txnpath /= to_string(txn_id);

  return txnpath;
}

fs::path fstxn_get_or_create_txndir(uint64_t txn_id)
{
  auto txndir = fstxn_get_txndir(txn_id);

  if (!fs::exists(txndir))
  {
      assert(create_directory(txndir));
  }

  return txndir;
}

void fstxn_backend_init(void)
{
  // create root directory
  (void)fs::create_directory(fstxn_backend_root);
}

int fstxn_create_txn(uint64_t txn_id, struct TxnLog* txn)
{
  proto::TransactionLog txnpb;
  auto txnpath = fstxn_get_or_create_txndir(txn_id);
  string txnfile = fstxn_txnfilename;
  txnpath /= txnfile;
  
  // serialize to protobuf
  if (txn_log_to_pb(txn, &txnpb))
  {
    std::cerr << "Failed to serialize to protobuf" << std::endl;
    return -1;
  }
  
  // write to a file
  fstream output(txnpath.c_str(),
                 ios::out | ios::trunc | ios::binary);
 
  if (!txnpb.SerializeToOstream(&output)) {
    std::cerr << "Failed to log transaction" << std::endl;
    return -1;
  }

  return 0;
}

int fstxn_get_txn(uint64_t txn_id, struct TxnLog* txn)
{
  proto::TransactionLog txnpb;
  auto txnpath = fstxn_get_txndir(txn_id);
  string txnfile = fstxn_txnfilename;
  txnpath /= txnfile;
  
  if (!fs::exists(txnpath))
  {
    std::cerr << "Failed to read file at " << txnpath << endl;
    return -1;
  }
  
  // read from file
  fstream input(txnpath.c_str(), ios::in | ios::binary);
  
  // parse protobuf
  if (!txnpb.ParseFromIstream(&input)) 
  {
    std::cerr << "Failed to parse existing file" << std::endl;
    return -1;
  }
  
  // convert to TxnLog
  if (txn_log_from_pb(&txnpb, txn) < 0)
  {
    std::cerr << "Failed to deserialize from protobuf" << std::endl;
  }

  return 0;
}

void fstxn_enumerate_txn(void (*callback)(struct TxnLog* txn))
{
   for(auto& p: fs::directory_iterator(fstxn_backend_root))
   {
     if (fs::is_directory(p))
     {
        std::string dir = p.path().filename();
        uint64_t txn_id = stoull(dir);
        struct TxnLog txn;
        if (fstxn_get_txn(txn_id, &txn) < 0)
        {
          std::cerr << "Failed to read transaction" << txn_id << endl;
        }
        callback(&txn);
     }
   }
}

int fstxn_remove_txn(uint64_t txn_id)
{
  auto txn_path = fstxn_get_txndir(txn_id);

  fs::remove_all(txn_path);

  return 0;
}

void fstxn_shutdown(void)
{
}

static struct txn_backend ldbtxn_backend = {
  .backend_init = ldbtxn_init,
  .get_txn = ldbtxn_get_txn,
  .enumerate_txn = ldbtxn_enumerate_txn,
  .remove_txn = ldbtxn_remove_txn,
  .create_txn = ldbtxn_create_txn,
  .backend_shutdown = ldbtxn_shutdown
};

void init_ldbtxn_backend(struct txn_backend** txn_backend)
{
  *txn_backend = &ldbtxn_backend;
}

static struct txn_backend fstxn_backend = {
  .backend_init = fstxn_backend_init,
  .get_txn = fstxn_get_txn,
  .enumerate_txn = fstxn_enumerate_txn,
  .remove_txn = fstxn_remove_txn,
  .create_txn = fstxn_create_txn,
  .backend_shutdown = fstxn_shutdown
};

void init_fstxn_backend(struct txn_backend** txn_backend)
{
  *txn_backend = &fstxn_backend;
}

