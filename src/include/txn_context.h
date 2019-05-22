// vim:noexpandtab:shiftwidth=8:tabstop=8:
#ifndef _TXN_CONTEXT_H
#define _TXN_CONTEXT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "nfsv41.h"
#include <uuid/uuid.h>

#define TXN_UUID_LEN sizeof(uuid_t)

// Per-operation context.
typedef struct {
  uuid_t id;
  bool is_new;
} op_context_t;

typedef struct {
  uint64_t txn_id;
  int ops_len;
  const nfs_argop4 *ops;
  op_context_t *op_contexts;
} txn_context_t;

txn_context_t *new_txn_context(int ops_len, const nfs_argop4 *ops);
void del_txn_context(txn_context_t *txn_context);

#ifdef __cplusplus
}
#endif

#endif
