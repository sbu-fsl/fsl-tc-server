#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <stdlib.h>
#include <string.h>

#include "lwrapper.h"

MATCHER_P2(IsPair, key, value,
           std::string(negation ? "isn't" : "is") + "<" + std::string(key) +
               ", " + std::string(value) = ">") {
  return std::string(arg.key, arg.key_len) == key &&
         std::string(arg.val, arg.val_len) == value;
}

TEST(TestLWrapper, InitDbTest) {
  db_store_t* db = init_db_store("test_db", true);
  ASSERT_TRUE(db);

  // Check that reading a nonexistent key is not error.
  struct db_kvpair kvp;
  kvp.key = "key-not-exists";
  kvp.key_len = strlen(kvp.key);
  kvp.val_len = 10;  // poison it so that we can check later.

  EXPECT_EQ(0, get_keys(&kvp, 1, db));
  EXPECT_EQ(nullptr, kvp.val);
  EXPECT_EQ(0, kvp.val_len);

  destroy_db_store(db);

  db = init_db_store("test_db", false);
  ASSERT_TRUE(db);
  destroy_db_store(db);
}

TEST(TestLWrapper, InsertAndLookupTest) {
  db_store_t* db = init_db_store("test_db", true);
  int ret = -1;
  ASSERT_TRUE(db);
  size_t key_len = strlen("test_key");
  size_t val_len = strlen("test_value");

  db_kvpair_t record = {
      .key = "test_key",
      .val = "test_value",
      .key_len = key_len,
      .val_len = val_len,
  };

  // put record
  ret = put_id_handle(&record, 1, db);
  ASSERT_FALSE(ret);

  // Lookup record by id.
  record.val = NULL;
  record.val_len = 0;

  EXPECT_EQ(0, get_id_handle(&record, 1, db, false));
  EXPECT_THAT(record, IsPair("test_key", "test_value"));

  // Reverse lookup record by handle.
  db_kvpair_t rev_record = {
      .key = record.val,
      .val = NULL,
      .key_len = record.val_len,
      .val_len = 0,
  };
  EXPECT_EQ(0, get_id_handle(&rev_record, 1, db, true));
  EXPECT_THAT(rev_record, IsPair("test_value", "test_key"));

  // delete record by id
  ret = delete_id_handle(&record, 1, db, false);
  ASSERT_FALSE(ret);

  destroy_db_store(db);
}

TEST(TestLWrapper, InsertAndDeleteTest) {
  db_store_t* db = init_db_store("test_db", true);
  int ret = -1;
  ASSERT_TRUE(db);
  size_t key_len = strlen("test_del_key");
  size_t val_len = strlen("test_del_value");

  db_kvpair_t record = {
      .key = "test_del_key",
      .val = "test_del_value",
      .key_len = key_len,
      .val_len = val_len,
  };

  // put record
  ret = put_id_handle(&record, 1, db);
  ASSERT_FALSE(ret);

  record.val = NULL;
  record.val_len = 0;

  // lookup record by id
  EXPECT_EQ(0, get_id_handle(&record, 1, db, false));
  EXPECT_THAT(record, IsPair("test_del_key", "test_del_value"));

  // delete record by id
  ret = delete_id_handle(&record, 1, db, false);
  ASSERT_FALSE(ret);

  // lookup record by id
  record.val = NULL;
  record.val_len = 0;
  EXPECT_EQ(0, get_id_handle(&record, 1, db, false));
  ASSERT_FALSE(ret);
  ASSERT_FALSE(record.val);
  ASSERT_FALSE(record.val_len);

  // lookup record by handle
  db_kvpair_t rev_record = {
      rev_record.key = "test_del_value",
      rev_record.val = NULL,
      rev_record.key_len = val_len,
      rev_record.val_len = 0,
  };
  EXPECT_EQ(0, get_id_handle(&rev_record, 1, db, true));
  ASSERT_FALSE(ret);
  ASSERT_FALSE(rev_record.val);
  ASSERT_FALSE(rev_record.val_len);
  destroy_db_store(db);
}

TEST(TestLWrapper, CommitTransactionTest) {
  db_store_t* db = init_db_store("test_db", true);
  ASSERT_TRUE(db);

  db_kvpair_t* record = (db_kvpair_t*)malloc(sizeof(db_kvpair_t));
  const char* key = "1234";
  const char* value = "/a/b/c/d";
  size_t key_len = strlen(key);
  size_t val_len = strlen(value);
  record->key = key;
  record->val = value;
  record->key_len = key_len;
  record->val_len = val_len;

  // commit first transaction
  int ret = commit_transaction(record, 1, db);
  ASSERT_FALSE(ret);

  key = "5678";
  value = "/a/b/c/d/e";
  key_len = strlen(key);
  val_len = strlen(value);

  record->key = key;
  record->val = value;
  record->key_len = key_len;
  record->val_len = val_len;

  // commit second transaction
  ret = commit_transaction(record, 1, db);
  ASSERT_FALSE(ret);

  free(record);

  // now iterate over all the transactions
  db_kvpair_t** tr_records = NULL;
  int nrecs = 0;
  ret = iterate_transactions(&tr_records, &nrecs, db);

  // verify the commited transaction
  ASSERT_FALSE(ret);
  EXPECT_EQ(2, nrecs);
  EXPECT_THAT(*tr_records[0], IsPair("1234", "/a/b/c/d"));
  EXPECT_THAT(*tr_records[1], IsPair("5678", "/a/b/c/d/e"));

  // now delete all transacations
  ret = delete_transaction(tr_records[0], 1, db);
  ASSERT_FALSE(ret);

  ret = delete_transaction(tr_records[1], 1, db);
  ASSERT_FALSE(ret);

  cleanup_transaction_iterator(tr_records, nrecs);

  // now iterate again over committed transaction
  ret = iterate_transactions(&tr_records, &nrecs, db);

  ASSERT_FALSE(ret);
  ASSERT_TRUE(nrecs == 0);
  cleanup_transaction_iterator(tr_records, nrecs);

  destroy_db_store(db);
}

TEST(TestLWrapper, CommitTransactionAndInsertTest) {
  db_store_t* db = init_db_store("test_db", true);
  ASSERT_TRUE(db);

  db_kvpair_t* record = (db_kvpair_t*)malloc(sizeof(db_kvpair_t));
  const char* key = "1234";
  const char* value = "/a/b/c/d";
  size_t key_len = strlen(key);
  size_t val_len = strlen(value);

  record->key = key;
  record->val = value;
  record->key_len = key_len;
  record->val_len = val_len;

  // commit first transaction
  int ret = commit_transaction(record, 1, db);
  ASSERT_FALSE(ret);

  key = "5678";
  value = "/a/b/c/d/e";
  key_len = strlen(key);
  val_len = strlen(value);

  record->key = key;
  record->val = value;
  record->key_len = key_len;
  record->val_len = val_len;
  // insert a normal key
  ret = put_id_handle(record, 1, db);
  ASSERT_FALSE(ret);

  free(record);

  db_kvpair_t** tr_records = NULL;
  int nrecs = -1;
  ret = iterate_transactions(&tr_records, &nrecs, db);

  ASSERT_FALSE(ret);
  // only one transaction commit should be present
  ASSERT_EQ(1, nrecs);
  EXPECT_THAT(*tr_records[0], IsPair("1234", "/a/b/c/d"));

  cleanup_transaction_iterator(tr_records, nrecs);

  destroy_db_store(db);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
