#include <algorithm>
#include <iostream>
#include <fstream>
#include <string>
#include <experimental/filesystem>
#include <lwrapper.h>
#include "txn_logger.h"
#include "txn_backend.h"
#include "txn.pb.h"

using namespace std;
namespace fs = std::experimental::filesystem;
static db_store_t* db = NULL;

static const char* fstxn_backend_root = "/tmp/fstxn/";
static const char* fstxn_txnfilename = "txn.data";

void ldbtxn_init(void)
{
  db = init_db_store("test_db", true);
}

int ldbtxn_add_txn(uint64_t txn_id, struct TxnLog* txn)
{
  return 0;
}

int ldbtxn_get_txn(uint64_t txn_id, struct TxnLog* txn)
{
  return 0;
}

void ldbtxn_enumerate_txn(void (*callback)(struct TxnLog* txn))
{

}

void ldbtxn_remove_txn(uint64_t txn_id)
{

}

void ldbtxn_shutdown(void)
{
  destroy_db_store(db);
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

fs::path fstxn_get_txnpath(uint64_t txn_id)
{
  auto txnpath = fstxn_get_txndir(txn_id);
  txnpath /= "txn.data";
  return txnpath;
}

void fstxn_backend_init(void)
{
  // create root directory
  fs::create_directory(fstxn_backend_root);
}

int fstxn_add_txn(uint64_t txn_id, struct TxnLog* txn)
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
        fstxn_get_txn(txn_id, &txn);
        callback(&txn);
     }
   }
}

void fstxn_remove_txn(uint64_t txn_id)
{
  auto txn_path = fstxn_get_txndir(txn_id);

  fs::remove_all(txn_path);
}

void fstxn_shutdown(void)
{
}

static struct txn_backend ldbtxn_backend = {
  .backend_init = ldbtxn_init,
  .get_txn = ldbtxn_get_txn,
  .enumerate_txn = ldbtxn_enumerate_txn,
  .remove_txn = ldbtxn_remove_txn,
  .add_txn = ldbtxn_add_txn,
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
  .add_txn = fstxn_add_txn,
  .backend_shutdown = fstxn_shutdown
};

void init_fstxn_backend(struct txn_backend** txn_backend)
{
  *txn_backend = &fstxn_backend;
}

