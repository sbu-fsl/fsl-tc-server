#ifndef _TXN_BACKEND_H
#define _TXN_BACKEND_H

//
// storage backend for reading, writing and enumerating transaction logs
struct txn_backend {
  //
  // initialize transaction backend storage
  void (*backend_init)(void);
  
  //
  // get transaction
  int (*get_txn)(uint64_t txn_id, struct TxnLog* txn);

  //
  // enumerate transactions
  void (*enumerate_txn)(void (*callback)(struct TxnLog* txn));

  //
  // remove transaction
  void (*remove_txn)(uint64_t txn_id);

  int (*add_txn)(uint64_t txn_id, struct TxnLog* txn);
  //
  // shutdown transaction backend storage
  void (*backend_shutdown)(void);
};

void init_ldbtxn_backend(struct txn_backend** txn_backend);

void init_fstxn_backend(struct txn_backend** txn_backend);
#endif
