#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <set>
#include <stdlib.h>
#include <string.h>

#include <gtest/gtest.h>

#include "id_manager.h"

namespace fs = boost::filesystem;

struct IDCompare {
  bool operator()(const char *a, const char *b) const {
    for (int i = 0; i < TXN_UUID_LEN; i++) {
      if (a[i] < b[i]) return true;
      if (a[i] > b[i]) return false;
    }
    return false;
  }
};

constexpr const char kTestDbPath[] = "/tmp/txnfs_id_manager_test_db";

class IdManagerTest : public ::testing::Test {
 public:
  IdManagerTest() {
    if (fs::exists(kTestDbPath)) {
      fs::remove_all(kTestDbPath);
    }
    db = init_db_store(kTestDbPath, true);
    EXPECT_TRUE(db);
    EXPECT_EQ(0, initialize_id_manager(db));
  }
  ~IdManagerTest() {
    if (db != nullptr) destroy_db_store(db);
    fs::remove_all(kTestDbPath);
  }

 protected:
  db_store_t *db = nullptr;
};

TEST_F(IdManagerTest, RootFileIdBasics) {

  IDCompare comparator;
  char *root_id = get_root_id(db);
  char *first_id = generate_file_id(db);
  EXPECT_TRUE(comparator(root_id, first_id));

  EXPECT_EQ(0x100000000ULL, get_lower_half(root_id));
  EXPECT_EQ(0x0ULL, get_upper_half(root_id));

  EXPECT_EQ(0x100000001ULL, get_lower_half(first_id));

  free(root_id);
  free(first_id);
}

TEST_F(IdManagerTest, SimpleTest) {
  std::set<char *, IDCompare> ids;

  for (int i = 0; i < 512; i++) {
    char *id = generate_file_id(db);
    ASSERT_TRUE(id);
    ASSERT_TRUE(ids.find(id) == ids.end());

    auto p = ids.insert(id);
    // Ensure ID wasn't already inserted
    ASSERT_TRUE(p.second);

    char *s = id_to_string(id);
    ASSERT_TRUE(s);
    free(s);
  }

  for (auto &id : ids) {
    free(id);
  }
}

TEST_F(IdManagerTest, SimulateFailure) {
  std::set<char *, IDCompare> ids;

  srand(time(nullptr));

  for (int i = 0; i < 100; i++) {
    if (rand() % 50 == 0) {
      // Simulate crash
      destroy_db_store(db);

      db = init_db_store(kTestDbPath, false);
      ASSERT_TRUE(db);

      ASSERT_TRUE(initialize_id_manager(db) == 0);
    }

    char *id = generate_file_id(db);
    ASSERT_TRUE(id);
    ASSERT_TRUE(ids.find(id) == ids.end());

    auto p = ids.insert(id);
    // Ensure ID wasn't already inserted
    ASSERT_TRUE(p.second);

    char *s = id_to_string(id);
    ASSERT_TRUE(s);
    free(s);
  }

  for (auto &id : ids) {
    free(id);
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
