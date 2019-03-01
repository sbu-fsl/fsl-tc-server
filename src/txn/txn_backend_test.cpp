#include <gtest/gtest.h>
#include <stdlib.h>
#include <string.h>
#include "txn_backend.h"
#include "txn_logger.h"

void txn_processor(struct TxnLog* txn)
{
  std::cout << "enumerating txn_id=" << txn->txn_id << std::endl;
}

TEST(TxnBackend, SimpleTest) {
  struct txn_backend* backend;
  struct TxnLog txn_write;
  struct TxnLog txn_read;
  txn_write.compound_type = txn_VNone;
  init_fstxn_backend(&backend);

  backend->backend_init();

  txn_write.txn_id = 42;
  backend->add_txn(42, &txn_write);

  txn_write.txn_id = 52;
  backend->add_txn(52, &txn_write);
  
  txn_write.txn_id = 92;
  backend->add_txn(92, &txn_write);
  
  backend->get_txn(42, &txn_read);
  ASSERT_EQ(42, txn_read.txn_id);

  backend->get_txn(52, &txn_read);
  ASSERT_EQ(52, txn_read.txn_id);
  
  backend->get_txn(92, &txn_read);
  ASSERT_EQ(92, txn_read.txn_id);
  
  backend->enumerate_txn(&txn_processor);
  backend->remove_txn(42);
  backend->enumerate_txn(&txn_processor);
  backend->backend_shutdown();
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
