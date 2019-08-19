/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Stony Brook University 2019
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include "txnfs_methods.h"
#include <assert.h>

static inline void txnfs_cache_expand(void)
{
	op_ctx->txn_cache->entries = gsh_realloc(
	    op_ctx->txn_cache->entries,
	    op_ctx->txn_cache->capacity * 2 * sizeof(struct txnfs_cache_entry));
	op_ctx->txn_cache->capacity *= 2;
}

static inline int txnfs_cache_cmpfh(struct txnfs_cache_entry *ent,
				    struct gsh_buffdesc *buf)
{
	if (ent->hdl_size != buf->len) return -1;
	if (likely(ent->hdl_size <= FH_LEN))
		return memcmp(ent->fh.data, buf->addr, ent->hdl_size);
	else
		return memcmp(ent->fh.addr, buf->addr, ent->hdl_size);
}

static inline void *txnfs_cache_copyfh(struct txnfs_cache_entry *ent, void *buf)
{
	if (likely(ent->hdl_size <= FH_LEN))
		return memcpy(buf, ent->fh.data, ent->hdl_size);
	else
		return memcpy(buf, ent->fh.addr, ent->hdl_size);
}

int txnfs_cache_insert(enum txnfs_cache_entry_type entry_type,
		       struct gsh_buffdesc *hdl_desc, uuid_t uuid)
{
	UDBG;
	assert((entry_type == txnfs_cache_entry_create && hdl_desc) ||
	       (entry_type == txnfs_cache_entry_delete));

	if (unlikely(op_ctx->txn_cache->size >= op_ctx->txn_cache->capacity))
		txnfs_cache_expand();

	struct txnfs_cache_entry *entry =
	    op_ctx->txn_cache->entries + op_ctx->txn_cache->size;
	entry->entry_type = entry_type;
	entry->hdl_size = 0;
	uuid_copy(entry->uuid, uuid);
	if (hdl_desc) {
		entry->hdl_size = hdl_desc->len;
		if (likely(hdl_desc->len <= FH_LEN)) {
			memcpy(entry->fh.data, hdl_desc->addr, hdl_desc->len);
		} else {
			LogWarnOnce(
			    COMPONENT_FSAL,
			    "sub-fsal fh size is %zu"
			    "which exceeds %d, individual malloc is required"
			    " and performance may be impacted",
			    hdl_desc->len, FH_LEN);
			entry->fh.addr = gsh_malloc(hdl_desc->len);
			memcpy(entry->fh.addr, hdl_desc->addr, hdl_desc->len);
		}
	}
	op_ctx->txn_cache->size++;

	return 0;
}

int txnfs_cache_get_uuid(struct gsh_buffdesc *hdl_desc, uuid_t uuid)
{
	UDBG;
	struct txnfs_cache_entry *entry;

	txnfs_cache_foreach(entry, op_ctx->txn_cache)
	{
		if (entry->entry_type == txnfs_cache_entry_create &&
		    txnfs_cache_cmpfh(entry, hdl_desc) == 0) {
			uuid_copy(uuid, entry->uuid);
			return 0;
		}
	}

	return -1;
}

int txnfs_cache_get_handle(uuid_t uuid, struct gsh_buffdesc *hdl_desc)
{
	struct txnfs_cache_entry *entry;
	char *hdl_str;

	txnfs_cache_foreach(entry, op_ctx->txn_cache)
	{
		/* If a matching entry is found in cache, it will copy the
		 * content of file handle into a new buffer. BE SURE TO FREE.
		 * The reason for such design is to make it consistent with
		 * txnfs_db_get_handle.
		 */
		if (entry->entry_type == txnfs_cache_entry_create &&
		    memcmp(entry->uuid, uuid, sizeof(uuid_t)) == 0) {
			hdl_str = gsh_malloc(entry->hdl_size);
			txnfs_cache_copyfh(entry, hdl_str);
			hdl_desc->addr = hdl_str;
			hdl_desc->len = entry->hdl_size;
			return 0;
		}
	}

	return -1;
}

int txnfs_cache_delete_uuid(uuid_t uuid)
{
	/* Let's not scan the whole cache for existing items.
	 * The rationale are as follows:
	 * - We don't anticipate a compound that mixes CREATE/OPEN and REMOVE;
	 * - We don't allow such scenario where there are existing files
	 * created outside TXNFS context (which won't have a UUID and in the
	 * earlier implementation txnfs_alloc_and_check_handle will give it
	 * a new UUID and insert it to cache for later commit)
	 *
	 * Therefore, we will just insert a removal candidate into the
	 * cache for later commit. This should be very fast since it's O(1)
	 * and txn cache is a vector now.
	 */
	return txnfs_cache_insert(txnfs_cache_entry_delete, NULL, uuid);
}

void txnfs_cache_init(uint32_t compound_size)
{
	if (op_ctx->txn_cache)
		LogFatal(COMPONENT_FSAL,
			 "txnfs cache has already been initialized");
	op_ctx->txn_cache = gsh_calloc(1, sizeof(*op_ctx->txn_cache));
	op_ctx->txn_cache->capacity = MIN(compound_size, TXN_CACHE_CAP);
	op_ctx->txn_cache->size = 0;
	op_ctx->txn_cache->entries = gsh_calloc(
	    op_ctx->txn_cache->capacity, sizeof(struct txnfs_cache_entry));
}

static inline void combine_prefix(const char *prefix, const size_t prefix_len,
				  const char *src, const size_t src_len,
				  char **dest, size_t *length)
{
	*dest = NULL;
	if (!src || !prefix) return;
	char *key = gsh_malloc(prefix_len + src_len);

	memcpy(key, prefix, prefix_len);
	memcpy(key + prefix_len, src, src_len);
	*dest = key;
	*length = prefix_len + src_len;
}

// commit entries in `op_ctx->txn_cache` and remove txn log
int txnfs_cache_commit(void)
{
	UDBG;
	int ret = 0;
	char *err = NULL;
	char uuid_str[UUID_STR_LEN];
	struct txnfs_cache_entry *entry;

	struct fsal_module *fs = op_ctx->fsal_export->fsal;
	struct txnfs_fsal_module *txnfs =
	    container_of(fs, struct txnfs_fsal_module, module);
	db_store_t *db = txnfs->db;

	char *uuid_key = NULL;
	char *hdl_key = NULL;
	size_t uuid_key_len, hdl_key_len;
	int n_put = 0, n_del = 0;

	leveldb_writebatch_t *commit_batch = leveldb_writebatch_create();
	txnfs_cache_foreach(entry, op_ctx->txn_cache)
	{
		uuid_unparse_lower(entry->uuid, uuid_str);

		/* add prefix to keys */
		combine_prefix(UUID_KEY_PREFIX, PREF_LEN, entry->uuid,
			       sizeof(uuid_t), &uuid_key, &uuid_key_len);
		combine_prefix(FH_KEY_PREFIX, PREF_LEN, TXNCACHE_FH(entry),
			       entry->hdl_size, &hdl_key, &hdl_key_len);

		if (entry->entry_type == txnfs_cache_entry_create) {
			leveldb_writebatch_put(commit_batch, uuid_key,
					       uuid_key_len, TXNCACHE_FH(entry),
					       entry->hdl_size);

			leveldb_writebatch_put(commit_batch, hdl_key,
					       hdl_key_len, entry->uuid,
					       sizeof(uuid_t));

			LogDebug(COMPONENT_FSAL, "put_key:%s ", uuid_str);

			n_put++;
		} else if (entry->entry_type == txnfs_cache_entry_delete) {
			leveldb_writebatch_delete(commit_batch, uuid_key,
						  uuid_key_len);
			if (entry->hdl_size > 0)
				leveldb_writebatch_delete(commit_batch, hdl_key,
							  hdl_key_len);

			LogDebug(COMPONENT_FSAL, "delete_key:%s ", uuid_str);

			n_del++;
		}

		gsh_free(uuid_key);
		gsh_free(hdl_key);
	}
	txnfs_tracepoint(collected_cache_entries, op_ctx->txnid, n_put, n_del);
	// TODO - add entry to remove txn log
	/*char txnkey[20];
	strcpy(txnkey, "txn-", 4);
	uuid_copy(txnkey + 4, op_ctx->uuid);
	leveldb_writebatch_delete(commit_batch, txnkey, sizeof(uuid_t) + 4);*/

	if (n_put + n_del > 0) {
		leveldb_write(db->db, db->w_options, commit_batch, &err);

		if (err) {
			LogDebug(COMPONENT_FSAL, "leveldb error: %s", err);
			leveldb_free(err);
			ret = -1;
		}

		txnfs_tracepoint(committed_cache_to_db, op_ctx->txnid,
				 err != NULL);
	}

	leveldb_writebatch_destroy(commit_batch);

	return ret;
}

// cleanup txn entries
void txnfs_cache_cleanup(void)
{
	if (!op_ctx->txn_cache)
		LogFatal(COMPONENT_FSAL, "attempt to destroy null cache");
	gsh_free(op_ctx->txn_cache->entries);
	op_ctx->txn_cache->capacity = 0;
	gsh_free(op_ctx->txn_cache);
	op_ctx->txn_cache = NULL;
}

int txnfs_db_insert_handle(struct gsh_buffdesc *hdl_desc, uuid_t uuid)
{
	char uuid_str[UUID_STR_LEN];
	int ret = 0;
	char *err = NULL;

	struct fsal_module *fs = op_ctx->fsal_export->fsal;
	struct txnfs_fsal_module *txnfs =
	    container_of(fs, struct txnfs_fsal_module, module);
	db_store_t *db = txnfs->db;
	char *uuid_key = NULL, *hdl_key = NULL;
	size_t uuid_key_len, hdl_key_len;

	UDBG;
	uuid_generate(uuid);
	uuid_unparse_lower(uuid, uuid_str);
	LogDebug(COMPONENT_FSAL, "generate uuid=%s\n", uuid_str);

	if (op_ctx->txn_cache) {
		return txnfs_cache_insert(txnfs_cache_entry_create, hdl_desc,
					  uuid);
	}

	/* let's add prefix first */
	combine_prefix(UUID_KEY_PREFIX, PREF_LEN, uuid, sizeof(uuid_t),
		       &uuid_key, &uuid_key_len);
	combine_prefix(FH_KEY_PREFIX, PREF_LEN, hdl_desc->addr, hdl_desc->len,
		       &hdl_key, &hdl_key_len);

	/* write to database */
	leveldb_writebatch_t *commit_batch = leveldb_writebatch_create();
	leveldb_writebatch_put(commit_batch, uuid_key, uuid_key_len,
			       hdl_desc->addr, hdl_desc->len);
	leveldb_writebatch_put(commit_batch, hdl_key, hdl_key_len, uuid,
			       sizeof(uuid_t));
	leveldb_write(db->db, db->w_options, commit_batch, &err);

	if (err) {
		LogDebug(COMPONENT_FSAL, "leveldb error: %s", err);
		leveldb_free(err);
		ret = -1;
	}

	leveldb_writebatch_destroy(commit_batch);
	gsh_free(hdl_key);
	gsh_free(uuid_key);

	return ret;
}

/* @brief Query UUID with sub-FSAL host handle ONLY in levelDB */
int txnfs_db_get_uuid_nocache(struct gsh_buffdesc *hdl_desc, uuid_t uuid)
{
	struct fsal_module *fs = op_ctx->fsal_export->fsal;
	struct txnfs_fsal_module *txnfs =
	    container_of(fs, struct txnfs_fsal_module, module);
	db_store_t *db = txnfs->db;

	char *hdl_key;
	size_t hdl_key_len;
	combine_prefix(FH_KEY_PREFIX, PREF_LEN, hdl_desc->addr, hdl_desc->len,
		       &hdl_key, &hdl_key_len);

	char *val;
	char *err = NULL;
	size_t val_len;
	val = leveldb_get(db->db, db->r_options, hdl_key, hdl_key_len, &val_len,
			  &err);

	if (err) {
		LogFatal(COMPONENT_FSAL, "leveldb error: %s", err);
	}

	gsh_free(hdl_key);

	if (!val) {
		return -1;
	}

	assert(val_len == sizeof(uuid_t));
	uuid_copy(uuid, val);
	free(val);
	return 0;
}

/* @brief Query UUID with sub-FSAL host handle
 *
 * Note that this function will look up BOTH cache and the levelDB
 *
 * @param[in] hdl_desc	The buffer of the sub-FSAL's file handle
 * @param[out] uuid	The UUID
 *
 * @return 0 if successful, -1 if failed.
 */
int txnfs_db_get_uuid(struct gsh_buffdesc *hdl_desc, uuid_t uuid)
{
	// search txnfs compound cache first
	if (op_ctx->txn_cache && txnfs_cache_get_uuid(hdl_desc, uuid) == 0) {
		return 0;
	}

	LogDebug(COMPONENT_FSAL, "HandleAddr: %p HandleLen: %zu",
		 hdl_desc->addr, hdl_desc->len);

	return txnfs_db_get_uuid_nocache(hdl_desc, uuid);
}

int txnfs_db_get_handle(uuid_t uuid, struct gsh_buffdesc *hdl_desc)
{
	struct fsal_module *fs = op_ctx->fsal_export->fsal;
	struct txnfs_fsal_module *txnfs =
	    container_of(fs, struct txnfs_fsal_module, module);
	db_store_t *db = txnfs->db;

	/* look up in the cache first */
	if (op_ctx->txn_cache && txnfs_cache_get_handle(uuid, hdl_desc) == 0) {
		return 0;
	}

	char *uuid_key;
	size_t uuid_key_len;
	combine_prefix(UUID_KEY_PREFIX, PREF_LEN, uuid, sizeof(uuid_t),
		       &uuid_key, &uuid_key_len);

	char *val;
	size_t length;
	char *err = NULL;

	val = leveldb_get(db->db, db->r_options, uuid_key, uuid_key_len,
			  &length, &err);

	if (err) {
		LogFatal(COMPONENT_FSAL, "leveldb error: %s", err);
	}
	gsh_free(uuid_key);

	if (!val) return -1;

	/* NOTE: Be sure to **free** hdl_desc->addr after use */
	hdl_desc->len = length;
	hdl_desc->addr = val;
	return 0;
}

int txnfs_db_delete_uuid(uuid_t uuid)
{
	struct fsal_module *fs = op_ctx->fsal_export->fsal;
	struct txnfs_fsal_module *txnfs =
	    container_of(fs, struct txnfs_fsal_module, module);
	db_store_t *db = txnfs->db;
	int ret = 0;

	if (op_ctx->txn_cache) {
		return txnfs_cache_delete_uuid(uuid);
	}

	char *uuid_key;
	size_t uuid_key_len;
	combine_prefix(UUID_KEY_PREFIX, PREF_LEN, uuid, sizeof(uuid_t),
		       &uuid_key, &uuid_key_len);

	char *val;
	char *err = NULL;
	size_t val_len;
	val = leveldb_get(db->db, db->r_options, uuid_key, uuid_key_len,
			  &val_len, &err);
	if (err) {
		LogDebug(COMPONENT_FSAL, "leveldb error: %s", err);
		leveldb_free(err);
		ret = -1;
		goto end;
	}

	char uuid_str[UUID_STR_LEN];
	uuid_unparse_lower(uuid, uuid_str);
	LogDebug(COMPONENT_FSAL, "delete uuid=%s\n", uuid_str);
	if (unlikely(!val)) {
		LogDebug(COMPONENT_FSAL, "uuid {%s} not found", uuid_str);
		ret = -1;
		goto end;
	}

	leveldb_delete(db->db, db->w_options, uuid_key, uuid_key_len, &err);

	if (err) {
		LogDebug(COMPONENT_FSAL, "leveldb error: %s", err);
		leveldb_free(err);
		ret = -1;
	}
end:
	gsh_free(uuid_key);
	return ret;
}

bool txnfs_db_handle_exists(struct gsh_buffdesc *hdl_desc)
{
	UDBG;
	uuid_t uuid;

	// search txnfs compound cache
	if (op_ctx->txn_cache && txnfs_cache_get_uuid(hdl_desc, uuid) == 0) {
		return 0;
	}
	return txnfs_db_get_uuid(hdl_desc, uuid) == 0;
}

void get_txn_root(struct fsal_obj_handle **root_handle, struct attrlist *attrs)
{
	struct txnfs_fsal_export *exp =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);
	if (exp->root) {
		*root_handle = exp->root;
		return;
	}

	struct fsal_obj_handle *root_entry = NULL;
	fsal_status_t ret = op_ctx->fsal_export->exp_ops.lookup_path(
	    op_ctx->fsal_export, op_ctx->ctx_export->fullpath, &root_entry,
	    attrs);
	assert(FSAL_IS_SUCCESS(ret));
	*root_handle = root_entry;
	exp->root = root_entry;
}
