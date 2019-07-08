#ifndef _UNDO_EXECUTOR_H
#define _UNDO_EXECUTOR_H
#include "lwrapper.h"

// undo transaction
//
void undo_txn_execute(struct TxnLog* txn, db_store_t* db);

//  undo transactions in backend
//
// void undo_txns(struct txn_backend* backend, leveldb* db) {
// backend->enumerate_txn(&undo_txn_execute);
//}

#endif
