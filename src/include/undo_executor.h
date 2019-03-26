#ifndef _UNDO_EXECUTOR_H
#define _UNDO_EXECUTOR_H

// undo transaction
//
void undo_txn_execute(struct TxnLog* txn);

//  undo transactions in backend
//
void undo_txns(struct txn_backend* backend, leveldb* db) {
  backend->enumerate_txn(&undo_txn_backend);
}

#endif
