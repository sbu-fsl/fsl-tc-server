#include <iostream>
#include "txn_logger.h"
#include "txn_backend.h"
#include "lwrapper.h"

using namespace std;
//static db_store_t* db = NULL;


void get_tx_path(uint64_t txn_id, char* buf)
{
  //sprintf("/txn/%ll", txn_id);
}

void fstxn_backend_init(void)
{
  //db = init_db_store("test_db", true);
  cout << "fs backend init" << endl;
}

void fstxn_add_txn(uint64_t txn_id, struct TxnLog* txn)
{
  cout << "get txn " << txn_id <<  endl;
}

void fstxn_get_txn(uint64_t txn_id, struct TxnLog* txn)
{
  //char path[256];
  //get_tx_path(txn_id, path);
  
  //db_kvpair_t record = {
      //.key = "test_del_key",
      //.val = "test_del_value",
      //.key_len = key_len,
      //.val_len = val_len,
  //};

  //// put record
  //ret = put_id_handle(&record, 1, db);
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

static struct txn_backend fstxn_backend = {
  .backend_init = fstxn_backend_init,
  .get_txn = fstxn_get_txn,
  .enumerate_txns = fstxn_enumerate_txn,
  .remove_txn = fstxn_remove_txn,
  .add_txn = fstxn_add_txn,
  .backend_shutdown = fstxn_shutdown
};

void init_fstxn_backend(struct txn_backend** txn_backend)
{
  *txn_backend = &fstxn_backend;
}

