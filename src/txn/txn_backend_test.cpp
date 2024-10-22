#include <gtest/gtest.h>
#include <stdlib.h>
#include <string.h>
#include <experimental/filesystem>

#include "txn_backend.h"
#include "txn_logger.h"
namespace fs = std::experimental::filesystem;

void txn_processor(struct TxnLog* txn) {
  std::cout << "enumerating txn_id=" << txn->txn_id << std::endl;
}

TEST(TxnBackend, SimpleFsBackend) {
  struct txn_backend* backend;
  struct TxnLog txn_write;
  struct TxnLog txn_read;
  txn_write.compound_type = txn_VNone;
  std::string fsroot = "/tmp/fstxn";
  init_fstxn_backend(fsroot.c_str(), "txn.data", &backend);

  backend->backend_init();

  txn_write.txn_id = 42;
  backend->create_txn(42, &txn_write);

  txn_write.txn_id = 52;
  backend->create_txn(52, &txn_write);

  txn_write.txn_id = 92;
  backend->create_txn(92, &txn_write);

  backend->get_txn(42, &txn_read);
  ASSERT_EQ(42, txn_read.txn_id);

  backend->get_txn(52, &txn_read);
  ASSERT_EQ(52, txn_read.txn_id);

  backend->get_txn(92, &txn_read);
  ASSERT_EQ(92, txn_read.txn_id);

  backend->enumerate_txn(&txn_processor);
  ASSERT_EQ(0, backend->remove_txn(42));
  backend->enumerate_txn(&txn_processor);
  backend->backend_shutdown();

  // remove root directory
  fs::remove_all(fsroot);
}

TEST(TxnBackend, SimpleLDbBackend) {
  struct txn_backend* backend;
  struct TxnLog txn_write;
  struct TxnLog txn_read;
  txn_write.compound_type = txn_VNone;
  std::string dbroot = "/tmp/testdb";
  init_ldbtxn_backend(dbroot.c_str(), "txn_", &backend);

  backend->backend_init();

  txn_write.txn_id = 42;
  backend->create_txn(42, &txn_write);

  txn_write.txn_id = 52;
  backend->create_txn(52, &txn_write);

  txn_write.txn_id = 92;
  backend->create_txn(92, &txn_write);

  backend->get_txn(42, &txn_read);
  ASSERT_EQ(42, txn_read.txn_id);

  backend->get_txn(52, &txn_read);
  ASSERT_EQ(52, txn_read.txn_id);

  backend->get_txn(92, &txn_read);
  ASSERT_EQ(92, txn_read.txn_id);
  ASSERT_EQ(-1, backend->get_txn(12, &txn_read));

  backend->enumerate_txn(&txn_processor);
  ASSERT_EQ(0, backend->remove_txn(42));
  ASSERT_EQ(0, backend->remove_txn(52));
  ASSERT_EQ(0, backend->remove_txn(92));

  // okay, even if key does not exist
  ASSERT_EQ(0, backend->remove_txn(12));
  backend->enumerate_txn(&txn_processor);
  backend->backend_shutdown();

  // remove database
  fs::remove_all(dbroot);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
