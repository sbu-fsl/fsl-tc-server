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
  struct TxnLog txn_log;
  txn_log.txn_id = 92;
  init_fstxn_backend(&backend);

  backend->backend_init();

  backend->add_txn(42, &txn_log);
  backend->enumerate_txn(&txn_processor);
  backend->get_txn(42, NULL);

  backend->remove_txn(42);
  backend->backend_shutdown();
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}