// vim:expandtab:shiftwidth=2:tabstop=2:
#include <glib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lwrapper.h"

#define DEBUG 1
#undef DEBUG

#define CHECK_ERR(err)      \
  do {                      \
    if ((err) != NULL) {    \
      /* reset error var */ \
      leveldb_free((err));  \
      (err) = NULL;         \
      return -1;            \
    }                       \
  } while (0);

#define CHECK_SUCCESS(X, Y) \
  do {                      \
    if ((X) == NULL) {      \
      printf(Y);            \
      return NULL;          \
    }                       \
  } while (0);

#define SAFE_FREE(ptr)   \
  do {                   \
    if ((ptr) != NULL) { \
      free((void *)(ptr));       \
      (ptr) = NULL;      \
    }                    \
  } while (0);

#define TR_PREFIX "txn-"
#define ID_PREFIX "id-"
#define HDL_PREFIX "hdl-"

const char* ANCHOR = "ldb-anchor";

static char* memdup(const char* buf, size_t len) {
  return (char*)g_memdup(buf, len);
}

static int insert_markers(const db_store_t* db_st) {
  char* err = NULL;

  leveldb_put(db_st->db, db_st->w_options, TR_PREFIX, strlen(TR_PREFIX),
              ANCHOR, strlen(ANCHOR) + 1, &err);
  CHECK_ERR(err);

  leveldb_put(db_st->db, db_st->w_options, ID_PREFIX, strlen(ID_PREFIX),
              ANCHOR, strlen(ANCHOR) + 1, &err);
  CHECK_ERR(err);

  leveldb_put(db_st->db, db_st->w_options, HDL_PREFIX, strlen(HDL_PREFIX),
              ANCHOR, strlen(ANCHOR) + 1, &err);
  CHECK_ERR(err);

  return 0;
}

db_store_t* init_db_store(const char* db_dir_path, bool is_creation) {
  char* err = NULL;
  db_store_t* db_st = (db_store_t*)malloc(sizeof(db_store_t));
  CHECK_SUCCESS(db_st,
                "\nERROR: Failed to allocate memory for DB store object");
  db_st->init_options = leveldb_options_create();
  CHECK_SUCCESS(db_st->init_options,
                "\nERROR: Failed to create DB init options");
  db_st->w_options = leveldb_writeoptions_create();
  CHECK_SUCCESS(db_st->w_options, "\nERROR: Failed to create DB write options");
  db_st->r_options = leveldb_readoptions_create();
  CHECK_SUCCESS(db_st->r_options, "\nERROR: Failed to create DB read options");

  // Dont need compression - LevelDB offers Snappy
  // which isn't very effective for int data
  leveldb_options_set_compression((db_st->init_options),
                                  leveldb_no_compression);

  // Create the DB file if missing
  leveldb_options_set_create_if_missing(db_st->init_options, is_creation);

  // Where to store the levelDB logs
  // leveldb_options_set_info_log(db_st->init_options,
  //                             leveldb_logger_t*);

  // Set block size. LevelDB stores key-value pairs in
  // blocks. These blocks are the units it reads to and
  // from the persistent storage.
  leveldb_options_set_block_size(db_st->init_options, 4096);

  // LevelDB caches uncompressed blocks in an LRU cache.
  // Set its size to 10 MB
  db_st->lru_cache = leveldb_cache_create_lru(10 * 1024 * 1024);
  CHECK_SUCCESS(db_st->lru_cache,
                "\nERROR: Failed to create LRU cache for the DB");
  leveldb_options_set_cache(db_st->init_options, db_st->lru_cache);

  // Need to provide the default env to levelDB
  db_st->env = leveldb_create_default_env();
  CHECK_SUCCESS(db_st->env, "\nERROR: Failed to get default env");
  leveldb_options_set_env(db_st->init_options, db_st->env);

  // It is claimed that a bloom filter can reduce
  // disk reads for get() by a factor of 100.
  // It needs to store some bits per key. Setting
  // it to 25% of key length.
  db_st->filter = leveldb_filterpolicy_create_bloom(32);
  CHECK_SUCCESS(db_st->filter,
                "\nERROR: Failed to create default bloom filter");
  leveldb_options_set_filter_policy(db_st->init_options, db_st->filter);

  // Force levelDB to write synchronously. The writeback
  // cache will still be available in the system.
  leveldb_writeoptions_set_sync(db_st->w_options, 1);

  // Reads should fill cache. This option can be disabled
  // during actual get() if the request size is large and
  // is suspected to truncate all the cache.
  leveldb_readoptions_set_fill_cache(db_st->r_options, 1);

  // Read checksums can help detecting any internal
  // corruption.
  leveldb_readoptions_set_verify_checksums(db_st->r_options, 1);

  // Create levelDB handle
  db_st->db = leveldb_open(db_st->init_options, db_dir_path, &err);

  if (err != NULL) {
    CHECK_SUCCESS(NULL, "ERROR: Failed to open ldb handle");
    /* reset error var */
    leveldb_free(err);
    err = NULL;
    return NULL;
  }

  int ret = insert_markers(db_st);

  if (ret != 0) {
    destroy_db_store(db_st);
    return NULL;
  }

  return db_st;
}

void destroy_db_store(db_store_t* db_st) {
  // Close the ldb handle first to avoid any
  // requests accessing cache, if they were pending
  leveldb_close(db_st->db);
  // Now free the cache
  leveldb_cache_destroy(db_st->lru_cache);
  // Do not delete the default env
  // leveldb_env_destroy(db_st->env);
  leveldb_filterpolicy_destroy(db_st->filter);
  leveldb_options_destroy(db_st->init_options);
  leveldb_writeoptions_destroy(db_st->w_options);
  leveldb_readoptions_destroy(db_st->r_options);
  void* ptr = (void*)db_st;
  SAFE_FREE(ptr);
  return;
}

static void generate_db_keys(db_kvpair_t* kvp, const char* prefix,
                             db_kvpair_t* new_kvp, int nums,
                             bool alloc_val_mem) {
  int i = 0;
  db_kvpair_t* curr_kvp = kvp;
  db_kvpair_t* curr_new_kvp = new_kvp;

  while (i < nums) {
    size_t len = curr_kvp->key_len + strlen(prefix);
    char* temp = (char*)malloc(sizeof(char) * len);
    // key => Transaction_prefix concats transaction ID
    strcpy(temp, prefix);
    memcpy(temp + strlen(prefix), curr_kvp->key, curr_kvp->key_len);
    // new allocated key will be automatically freed by the caller of commit
    // transaction api
    curr_new_kvp->key = temp;
    curr_new_kvp->key_len = len;
    if (alloc_val_mem) {
      curr_new_kvp->val = memdup(curr_kvp->val, curr_kvp->val_len);
      curr_new_kvp->val_len = curr_kvp->val_len;
    }

    i++;
    curr_kvp++;
    curr_new_kvp++;
#ifdef DEBUG
    printf("generated_key: %s  %lu\n", new_kvp->key, new_kvp->key_len);
#endif
  }
}

static void cleanup_allocated_kvps(db_kvpair_t* kvp, int nums) {
  db_kvpair_t* curr;
  for (int i = 0; i < nums; ++i) {
    curr = kvp;
    kvp++;
    SAFE_FREE(curr->key);
    SAFE_FREE(curr->val);
    SAFE_FREE(curr);
  }
}
/*
 * caller is owner of key, value pair memory. caller need to release the
 * memory
 */
int put_keys(db_kvpair_t* kvp, const int nums, const db_store_t* db_st) {
  if (nums < 0) return -1;

  if (nums == 0) return 0;

  char* err = NULL;

  // check db_type and call accordingly,
  // for now lib supports only LevelDB
  leveldb_writebatch_t* write_batch = leveldb_writebatch_create();
  int i = 0;
  db_kvpair_t* curr = kvp;

  while (i < nums) {
    leveldb_writebatch_put(write_batch, curr->key, curr->key_len,
                           curr->val, curr->val_len);
    i++;
    curr++;
  }

  leveldb_write(db_st->db, db_st->w_options, write_batch, &err);
  leveldb_writebatch_destroy(write_batch);

  CHECK_ERR(err);

  return 0;
}

/*
 * callee is the owner of 'kvp->val' memory. caller need to release this memory
 * after get returns
 */
int get_keys(db_kvpair_t* kvp, const int nums, const db_store_t* db_st) {
  char* err = NULL;
  int key_missing = 0;

  if (nums < 0) return -1;

  if (nums == 0) return 0;

  if (nums == 1) {
    kvp->val = leveldb_get(db_st->db, db_st->r_options, kvp->key, kvp->key_len,
                           &(kvp->val_len), &err);
    CHECK_ERR(err);

#ifdef DEBUG
    printf("get_keys: %s => %lu, err: %s\n", kvp->key, kvp->key_len, err);
#endif
  } else {
    // disable fill cache for bulk reads,
    // re-enable once bulk read is done.
    leveldb_readoptions_set_fill_cache(db_st->r_options, 0);
    int i = 0;
    db_kvpair_t* curr = kvp;

    while (i < nums) {
      curr->val = leveldb_get(db_st->db, db_st->r_options, curr->key,
                              curr->key_len, &(curr->val_len), &err);
      if (err != NULL) {
        /* reset error var */
        leveldb_free(err);
        err = NULL;
        key_missing = 1;
      }
      i++;
      curr++;
    }

    leveldb_readoptions_set_fill_cache(db_st->r_options, 1);
  }
  return (!key_missing ? 0 : -1);
}

/*
 * caller is the owner of 'kvp->key' memory, caller will release it. No mem
 * allocation required of 'val' for this routine from caller or callee itself.
 */
int delete_keys(db_kvpair_t* kvp, const int nums, const db_store_t* db_st) {
  char* err = NULL;
  int i = 0;
  db_kvpair_t* curr = kvp;

  leveldb_writebatch_t* del_batch = leveldb_writebatch_create();

  while (i < nums) {
    leveldb_writebatch_delete(del_batch, curr->key, curr->key_len);
#ifdef DEBUG
    printf("delete_key:%s => %lu\n", curr->key, curr->key_len);
#endif
    i++;
    curr++;
  }

  leveldb_write(db_st->db, db_st->w_options, del_batch, &err);
  leveldb_writebatch_destroy(del_batch);
  return 0;
}

void static swap_key_values(db_kvpair_t* kvp, int nums) {
  int i = 0;
  db_kvpair_t* curr_kvp = kvp;

  while (i < nums) {
    int temp_len;
    const char* temp_val;
    temp_val = curr_kvp->key;
    temp_len = curr_kvp->key_len;
    curr_kvp->key = curr_kvp->val;
    curr_kvp->key_len = curr_kvp->val_len;
    curr_kvp->val = temp_val;
    curr_kvp->val_len = temp_len;
    curr_kvp++;
    i++;
  }
}

/*
 * caller is owner of key, value pair memory. caller need to release the
 * memory, for transaction: key=> transaction ID, value=>absolute file path
 */
int put_id_handle(db_kvpair_t* kvp, const int nums, const db_store_t* db_st) {
  int ret = -1;
  db_kvpair_t* new_kvp = (db_kvpair_t*)malloc(nums * sizeof(db_kvpair_t));

  generate_db_keys(kvp, ID_PREFIX, new_kvp, nums, true);

  ret = put_keys(new_kvp, nums, db_st);

  cleanup_allocated_kvps(new_kvp, nums);

  if (ret) return ret;

  new_kvp = (db_kvpair_t*)malloc(nums * sizeof(db_kvpair_t));
  // use same memory for reverse map inserts
  // this is just an optimization to avoid extra mem allocation
  // for reverse map inserts
  swap_key_values(kvp, nums);

  generate_db_keys(kvp, HDL_PREFIX, new_kvp, nums, true);
  ret = put_keys(new_kvp, nums, db_st);
  cleanup_allocated_kvps(new_kvp, nums);

  // reset to original state
  swap_key_values(kvp, nums);
  return ret;
}

/*
 * callee is the owner of 'kvp->val' memory. caller need to release this memory
 * after get returns
 */
int get_id_handle(db_kvpair_t* kvp, const int nums, const db_store_t* db_st,
                  bool lookup_by_handle) {
  int ret = -1;
  const char* prefix = ID_PREFIX;
  int i = 0;
  db_kvpair_t* curr_kvp = kvp;

  if (lookup_by_handle) prefix = HDL_PREFIX;

  db_kvpair_t* new_kvp = (db_kvpair_t*)malloc(nums * sizeof(db_kvpair_t));
  db_kvpair_t* orig_n_kvp = new_kvp;

  generate_db_keys(kvp, prefix, new_kvp, nums, false);

  ret = get_keys(new_kvp, nums, db_st);

  if (ret) return ret;

  while (i < nums) {
    int len = curr_kvp->val_len = new_kvp->val_len;
    if (len) {
      curr_kvp->val = memdup(new_kvp->val, len);
    }
    i++;
    curr_kvp++;
    new_kvp++;
  }

  cleanup_allocated_kvps(orig_n_kvp, nums);
  return ret;
}

/*
 * caller is the owner of 'kvp->key' memory, caller will release it. No mem
 * allocation required of 'val' for this routine from caller or callee itself.
 */
int delete_id_handle(db_kvpair_t* kvp, const int nums, const db_store_t* db_st,
                     bool delete_by_handle) {
  int ret = -1;
  const char* prefix = ID_PREFIX;

  if (delete_by_handle) prefix = HDL_PREFIX;

  db_kvpair_t* new_kvp = (db_kvpair_t*)malloc(nums * sizeof(db_kvpair_t));

  generate_db_keys(kvp, prefix, new_kvp, nums, false);

  ret = delete_keys(new_kvp, nums, db_st);

  cleanup_allocated_kvps(new_kvp, nums);

  if (ret) return ret;

  new_kvp = (db_kvpair_t*)malloc(nums * sizeof(db_kvpair_t));
  // use same memory for reverse map deletes
  // this is just an optimization to avoid extra mem allocation
  // for reverse map deletes
  swap_key_values(kvp, nums);

  if (delete_by_handle)
    prefix = ID_PREFIX;
  else
    prefix = HDL_PREFIX;

  generate_db_keys(kvp, prefix, new_kvp, nums, true);
  ret = delete_keys(new_kvp, nums, db_st);
  cleanup_allocated_kvps(new_kvp, nums);

  // reset to original state
  swap_key_values(kvp, nums);
  return ret;
}

/*
 * caller is owner of key, value pair memory. caller need to release the
 * memory, for transaction: key=> transaction ID, value=>absolute file path
 */
int commit_transaction(db_kvpair_t* kvp, const int nums,
                       const db_store_t* db_st) {
  int ret = -1;
  db_kvpair_t* new_kvp = (db_kvpair_t*)malloc(nums * sizeof(db_kvpair_t));

  generate_db_keys(kvp, TR_PREFIX, new_kvp, nums, true);

  ret = put_keys(new_kvp, nums, db_st);

  cleanup_allocated_kvps(new_kvp, nums);
  return ret;
}

/*
 * caller is the owner of 'kvp->key' memory, caller will release it. No mem
 * allocation required of 'val' for this routine from caller or callee itself.
 */
int delete_transaction(db_kvpair_t* kvp, const int nums,
                       const db_store_t* db_st) {
  int ret = -1;
  db_kvpair_t* new_kvp = (db_kvpair_t*)malloc(nums * sizeof(db_kvpair_t));

  generate_db_keys(kvp, TR_PREFIX, new_kvp, nums, true);

  ret = delete_keys(new_kvp, nums, db_st);

  cleanup_allocated_kvps(new_kvp, nums);
  return ret;
}

/*
 * LevelDB basic transaction iterator
 * This populates all txn records in a memory with <txn-id, backup_path> format
 */
#define TXN_RECORDS 100000

int iterate_transactions(db_kvpair_t*** recs, int* nrecs,
                         const db_store_t* db_st) {
  leveldb_iterator_t* iter =
      leveldb_create_iterator(db_st->db, db_st->r_options);
  char* err = NULL;
  int prefix_len = strlen(TR_PREFIX);
  db_kvpair_t** records =
      (db_kvpair_t**)malloc(sizeof(db_kvpair_t*) * TXN_RECORDS);
  int count = 0;

  for (leveldb_iter_seek(iter, TR_PREFIX, strlen(TR_PREFIX));
       leveldb_iter_valid(iter); leveldb_iter_next(iter)) {
    size_t key_len, value_len;
    char* key_ptr = (char*)leveldb_iter_key(iter, &key_len);
    // TODO: fix
    char* value_ptr = (char*)leveldb_iter_value(iter, &value_len);

    char* prefix = strstr(key_ptr, TR_PREFIX);

    if (prefix == NULL || prefix[0] != key_ptr[0] || !strcmp(value_ptr, ANCHOR))
      continue;

#ifdef DEBUG
    printf("iter: %s => %s\t%lu\t%lu\n", key_ptr, (char*)value_ptr, key_len,
           value_len);
#endif

    db_kvpair_t* record = (db_kvpair_t*)malloc(sizeof(db_kvpair_t));
    char* temp = key_ptr;
    // since this is char pointer, it is okay to add directly length
    temp = temp + prefix_len;
    record->key_len = key_len - strlen(TR_PREFIX);
    record->key = memdup(temp, record->key_len);

    record->val_len = value_len;
    record->val = memdup(value_ptr, value_len);

    records[count++] = record;

#ifdef DEBUG
    printf("transaction: %s => %s\n", records[count - 1]->key,
           (char*)records[count - 1]->val);
#endif
  }

  *nrecs = count;
  *recs = records;
  leveldb_iter_destroy(iter);

  CHECK_ERR(err);

  return 0;
}

/*
 * This must be called for memory cleanup. User of iterate_transaction
 * must call this at some point
 */
void cleanup_transaction_iterator(db_kvpair_t** records, int nrecs) {
  int i;
  for (i = 0; i < nrecs; i++) {
    SAFE_FREE(records[i]->key);
    SAFE_FREE(records[i]->val);
    SAFE_FREE(records[i]);
  }

  SAFE_FREE(records);
}
