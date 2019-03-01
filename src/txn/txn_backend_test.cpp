#include <gtest/gtest.h>
#include <stdlib.h>
#include <string.h>
#include "txn_backend.h"
#include "txn_logger.h"

void txn_processor(struct TxnLog* txn)
{
  std::cout << txn->txn_id << std::endl;
}

TEST(TxnBackend, SimpleTest) {
  struct txn_backend* backend;
  struct TxnLog txn_log1;
  struct TxnLog txn_log2;
  txn_log1.txn_id = 42;
  txn_log1.compound_type = txn_VNone;
  init_fstxn_backend(&backend);

  backend->backend_init();

  backend->add_txn(42, &txn_log1);
  backend->get_txn(42, &txn_log2);
  ASSERT_EQ(txn_log2.txn_id, txn_log1.txn_id);

  //backend->enumerate_txn(&txn_processor);
  backend->remove_txn(42);
  backend->backend_shutdown();
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
