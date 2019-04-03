// vim:noexpandtab:shiftwidth=8:tabstop=8:
#ifndef _TXN_CONTEXT_H
#define _TXN_CONTEXT_H

#include "id_manager.h"
#include "nfsv41.h"

// Per-operation context.
typedef struct {
	uuid_t id;
	bool is_new;
} op_context_t;

typedef struct {
	uint64_t txn_id;
	int ops_len;
	const nfs_argop4* ops;
	op_context_t* op_contexts;
} txn_context_t;

txn_context_t* new_txn_context(int ops_len, const nfs_argop4* ops);
void del_txn_context(txn_context_t* txn_context);

#endif
