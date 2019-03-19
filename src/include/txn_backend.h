#ifndef _TXN_BACKEND_H
#define _TXN_BACKEND_H

//
// storage backend for reading, writing and enumerating transaction logs
struct txn_backend {
  //
  // initialize transaction backend storage
  int (*backend_init)(void);

  //
  // get transaction
  int (*get_txn)(uint64_t txn_id, struct TxnLog* txn);

  //
  // enumerate transactions
  void (*enumerate_txn)(void (*callback)(struct TxnLog* txn));

  //
  // remove transaction
  int (*remove_txn)(uint64_t txn_id);

  int (*create_txn)(uint64_t txn_id, struct TxnLog* txn);
  //
  // shutdown transaction backend storage
  void (*backend_shutdown)(void);
};

// TxnBackend using LevelDB
// Database is stored at `root`
// Keys use prefix `prefix`
//
void init_ldbtxn_backend(const char* root, const char* prefix,
                         struct txn_backend** txn_backend);

//  TxnBackend using std::fileystem
//  Files are created at `root`
//  Transaction file is stored at /{root}/txn_{id}/{filename}
void init_fstxn_backend(const char* root, const char* filename,
                        struct txn_backend** txn_backend);
#endif
