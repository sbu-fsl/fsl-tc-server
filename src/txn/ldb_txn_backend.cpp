#include "txn_backend.h"
#include "txn_logger.h"
#include "lwrapper.txt"

static db_store_t* db = NULL;


void get_tx_path(uint64_t txn_id, char* buf)
{
  sprintf("/txn/%ll", txn_id);
}

void fstxn_backend_init()
{
  db = init_db_store("test_db", true);
}

void fstxn_get_txn(uint64_t txn_id, struct TxLog* /*txn*/)
{
  char path[256];
  get_tx_path(txn_id, path);
  
  db_kvpair_t record = {
      .key = "test_del_key",
      .val = "test_del_value",
      .key_len = key_len,
      .val_len = val_len,
  };

  // put record
  ret = put_id_handle(&record, 1, db);
  ASSERT_FALSE(ret);
}

void fstxn_enumerate_txn()
{

}

void fstxn_remove_txn(uint64_t txn_id)
{

}

void fstxn_shutdown()
{
  destroy_db_store(db);
}

static struct fstxn_backend
{
  .backend_init = fstxn_backend_init,
  .get_txn = fstxn_get_txn,
  .enumerate_txn = fstxn_enumerate_txn,
  .remove_txn = fstxn_remove_txn,
  .backend_shutdown = fstxn_shutdown
};

void fstxn_backend(struct txn_backend** txn_backend)
{
  *txn_backend = fstxn_backend;
}

