#include <algorithm>
#include <iostream>
#include <string>

#include <lwrapper.h>
#include "txn_logger.h"
#include "txn_backend.h"
#include "txn.pb.h"

using namespace std;

static db_store_t* db = NULL;

void ldbtxn_init(void)
{
  db = init_db_store("test_db", true);
}

void ldbtxn_add_txn(uint64_t txn_id, struct TxnLog* txn)
{
}

void ldbtxn_get_txn(uint64_t txn_id, struct TxnLog* txn)
{
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


void fstxn_backend_init(void)
{
  //db = init_db_store("test_db", true);
  cout << "fs backend init" << endl;
}

void fstxn_add_txn(uint64_t txn_id, struct TxnLog* txn)
{
  proto::TransactionLog txnpb;
  txn_log_to_pb(txn, &txnpb);
  
  cout << "add txn " << txnpb.id() <<  endl;
}

void fstxn_get_txn(uint64_t txn_id, struct TxnLog* txn)
{
  cout << "get txn " << txn_id <<  endl;
}

void fstxn_enumerate_txn(void (*callback)(struct TxnLog* txn))
{
  struct TxnLog txn;
  txn.txn_id = 42;
  for(int i = 0; i < 3; i++)
  {
    callback(&txn);
  }
}

void fstxn_remove_txn(uint64_t txn_id)
{
  //ret = delete_id_handle(&record, 1, db, false);
  cout << "remove txn " << txn_id <<  endl;
}

void fstxn_shutdown(void)
{
  //destroy_db_store(db);
  cout << "shutdown fs backend" << endl;
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

