// vim:noexpandtab:shiftwidth=8:tabstop=8:
# ifndef __LEVEL_DB_WRAPPER_H__
# define __LEVEL_DB_WRAPPER_H__

# include <leveldb/c.h>
# include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Contains default levelDB options */
struct db_store {
    leveldb_options_t*          init_options;
    leveldb_readoptions_t*      r_options;
    leveldb_writeoptions_t*     w_options;
    leveldb_cache_t*            lru_cache;
    leveldb_env_t*              env;
    leveldb_filterpolicy_t*     filter;
    leveldb_t*                  db;
};
typedef struct db_store db_store_t;

/*
 * Allocates memory for db handle
 * and other objects needed to interact
 * with level DB. Sets default values
 * to levelDB options in struct db_store.
 *
 * Input: Prefix of the DB files
 */
db_store_t* init_db_store(const char* db_dir_path, bool is_creation);


/*
 * Cleans up all the memory allocated during
 * init_db_store()
 */
void destroy_db_store(db_store_t*);

/* Encapsulates the key and value together */
struct db_kvpair {
    const char* key;
    const char* val;
    size_t key_len;
    size_t val_len;
};
typedef struct db_kvpair db_kvpair_t;

/*
 * Functions to put/get/delete keys from the DB
 * if return value is 0 : SUCCESS
 * if return value is -1: FAILURE / PARTIAL FAILURE
 */

/*
 * caller is owner of key, value pair memory. caller need to release the
 * memory.
 */
int put_keys(db_kvpair_t* kvp, const int nums, const db_store_t* db);

/*
 * callee is the owner of 'kvp->val' memory. caller need to release this memory
 * after get returns.
 *
 * Reading nonexistent keys are okay, and empty "kvp->val" and zero
 * "kvp->val_len" will be returned for each nonexistent key.
 */
int get_keys(db_kvpair_t* kvp, const int nums, const db_store_t* db);

/*
 * caller is the owner of 'kvp->key' memory, caller will release it. No mem
 * allocation required of 'val' for this routine from caller or callee itself.
 */
int delete_keys(db_kvpair_t* kvp, const int nums, const db_store_t* db);


/*
 * caller is owner of key, value pair memory. caller need to release the
 * memory, for transaction: key=> transaction ID, value=>absolute file path
 */
int put_id_handle(db_kvpair_t* kvp, const int nums, const db_store_t* db_st);

/*
 * callee is the owner of 'kvp->val' memory. caller need to release this memory
 * after get returns
 */
int get_id_handle(db_kvpair_t* kvp, const int nums, const db_store_t* db_st,
		  bool lookup_by_handle);

/*
 * caller is the owner of 'kvp->key' memory, caller will release it. No mem
 * allocation required of 'val' for this routine from caller or callee itself.
 */
int delete_id_handle(db_kvpair_t* kvp, const int nums, const db_store_t* db_st,
		     bool delete_by_handle);
/*
 * caller is owner of key, value pair memory. caller need to release the
 * memory, for transaction: key=> transaction ID, value=>absolute file path
 */
int commit_transaction(db_kvpair_t* kvp, const int nums,
		       const db_store_t* db_st);

/*
 * LevelDB basic transaction iterator
 * This populates all txn records in a memory with <txn-id, backup_path> format
 */
int iterate_transactions(db_kvpair_t*** recs, int* nrecs,
			 const db_store_t* db_st);

/*
 * caller is the owner of 'kvp->key' memory, caller will release it. No mem
 * allocation required of 'val' for this routine from caller or callee itself.
 */
int delete_transaction(db_kvpair_t* kvp, const int nums,
		       const db_store_t* db_st);

/*
 * This must be called for memory cleanup. User of iterate_transaction
 * must call this after consumption of txn-records returned 
 */
void cleanup_transaction_iterator(db_kvpair_t** records, int nrecs); 
#ifdef __cplusplus
}
#endif

#endif
