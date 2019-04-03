#include "txn_context.h"

txn_context_t* new_txn_context(int ops_len, const nfs_argop4* ops) {
  txn_context_t* txn_context = (txn_context_t*)malloc(sizeof(txn_context_t));
  txn_context->ops_len = ops_len;
  txn_context->ops = ops;
  txn_context->op_contexts =
      (op_context_t*)calloc(ops_len, sizeof(op_context_t));
  return txn_context;
}

void del_txn_context(txn_context_t* txn_context) {
  free(txn_context->op_contexts);
  free(txn_context);
}

