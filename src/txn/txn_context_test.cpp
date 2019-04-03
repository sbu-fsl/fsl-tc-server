#include <gtest/gtest.h>

#include "txn_context.h"

TEST(TxnContextTest, Initialization) {
	nfs_argop4 nfs4_op;
	txn_context_t* txn_context = new_txn_context(1, &nfs4_op);
	EXPECT_TRUE(uuid_is_null(txn_context->op_contexts[0].id));
	del_txn_context(txn_context);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
