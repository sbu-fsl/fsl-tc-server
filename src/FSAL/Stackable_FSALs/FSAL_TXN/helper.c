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

int txnfs_cache_insert(enum txnfs_cache_entry_type entry_type,
		       struct gsh_buffdesc *hdl_desc, uuid_t uuid)
{
	UDBG;
	// allocate for cache
	struct txnfs_cache_entry *entry =
	    gsh_malloc(sizeof(struct txnfs_cache_entry));

	/* allocate and initialize for handle */
	uuid_copy(entry->uuid, uuid);
	entry->entry_type = entry_type;
	entry->hdl_desc.addr = NULL;
	entry->hdl_desc.len = 0;

	assert((entry_type == txnfs_cache_entry_create && hdl_desc) ||
	       (entry_type == txnfs_cache_entry_delete));
	if (hdl_desc) {
		entry->hdl_desc.addr = gsh_malloc(hdl_desc->len);
		entry->hdl_desc.len = hdl_desc->len;
		memcpy(entry->hdl_desc.addr, hdl_desc->addr, hdl_desc->len);
	}

	glist_add(&op_ctx->txn_cache, &entry->glist);

	return 0;
}

int txnfs_cache_get_uuid(struct gsh_buffdesc *hdl_desc, uuid_t uuid)
{
	UDBG;
	LogDebug(COMPONENT_FSAL, "HandleAddr: %p HandleLen: %zu",
		 hdl_desc->addr, hdl_desc->len);

	char uuid_str[UUID_STR_LEN];
	struct txnfs_cache_entry *entry;
	struct glist_head *glist;

	glist_for_each(glist, &op_ctx->txn_cache)
	{
		entry = glist_entry(glist, struct txnfs_cache_entry, glist);

		uuid_unparse_lower(uuid, uuid_str);
		LogDebug(COMPONENT_FSAL, "cache scan uuid=%s\n", uuid_str);
		if (entry->entry_type == txnfs_cache_entry_create &&
		    memcmp(entry->hdl_desc.addr, hdl_desc->addr,
			   hdl_desc->len) == 0) {
			uuid_copy(uuid, entry->uuid);
			return 0;
		}
	}

	return -1;
}

int txnfs_cache_get_handle(uuid_t uuid, struct gsh_buffdesc *hdl_desc)
{
	struct txnfs_cache_entry *entry;
	struct glist_head *glist;
	char *hdl_str;

	glist_for_each(glist, &op_ctx->txn_cache)
	{
		entry = glist_entry(glist, struct txnfs_cache_entry, glist);

		/* If a matching entry is found in cache, it will copy the
		 * content of file handle into a new buffer. BE SURE TO FREE.
		 * The reason for such design is to make it consistent with
		 * txnfs_db_get_handle.
		 */
		if (entry->entry_type == txnfs_cache_entry_create &&
		    memcmp(entry->uuid, uuid, sizeof(uuid_t)) == 0) {
			size_t len = entry->hdl_desc.len;

			hdl_str = gsh_malloc(len);
			memcpy(hdl_str, entry->hdl_desc.addr, len);
			hdl_desc->addr = hdl_str;
			hdl_desc->len = len;
			return 0;
		}
	}

	return -1;
}

int txnfs_cache_delete_uuid(uuid_t uuid)
{
	UDBG;

	char uuid_str[UUID_STR_LEN];
	struct txnfs_cache_entry *entry;
	struct glist_head *glist;

	glist_for_each(glist, &op_ctx->txn_cache)
	{
		entry = glist_entry(glist, struct txnfs_cache_entry, glist);
		uuid_unparse_lower(uuid, uuid_str);
		LogDebug(COMPONENT_FSAL, "cache scan uuid=%s\n", uuid_str);
		if (uuid_compare(uuid, entry->uuid) == 0) {
			if (entry->entry_type == txnfs_cache_entry_create) {
				glist_del(&entry->glist);
			}
			return 0;
		}
	}

	// the entry does not exist in the cache
	// but we must prevent future lookups from database
	return txnfs_cache_insert(txnfs_cache_entry_delete, NULL, uuid);
}

void txnfs_cache_init(void)
{
	UDBG;
	assert(glist_null(&op_ctx->txn_cache) == 1);
	glist_init(&op_ctx->txn_cache);
}

// commit entries in `op_ctx->txn_cache` and remove txn log
int txnfs_cache_commit(void)
{
	UDBG;
	int ret = 0;
	char *err = NULL;
	char uuid_str[UUID_STR_LEN];
	struct txnfs_cache_entry *entry;
	struct glist_head *glist;

	struct fsal_module *fs = op_ctx->fsal_export->fsal;
	struct txnfs_fsal_module *txnfs =
	    container_of(fs, struct txnfs_fsal_module, module);
	db_store_t *db = txnfs->db;

	leveldb_writebatch_t *commit_batch = leveldb_writebatch_create();
	glist_for_each(glist, &op_ctx->txn_cache)
	{
		entry = glist_entry(glist, struct txnfs_cache_entry, glist);
		uuid_unparse_lower(entry->uuid, uuid_str);

		if (entry->entry_type == txnfs_cache_entry_create) {
			leveldb_writebatch_put(
			    commit_batch, entry->uuid, TXN_UUID_LEN,
			    entry->hdl_desc.addr, entry->hdl_desc.len);

			leveldb_writebatch_put(
			    commit_batch, entry->hdl_desc.addr,
			    entry->hdl_desc.len, entry->uuid, TXN_UUID_LEN);

			LogDebug(COMPONENT_FSAL, "put_key:%s ", uuid_str);
		} else if (entry->entry_type == txnfs_cache_entry_delete) {
			leveldb_writebatch_delete(commit_batch, entry->uuid,
						  TXN_UUID_LEN);
			if (entry->hdl_desc.addr)
				leveldb_writebatch_delete(commit_batch,
						  	  entry->hdl_desc.addr,
						  	  entry->hdl_desc.len);

			LogDebug(COMPONENT_FSAL, "delete_key:%s ", uuid_str);
		}
	}

	// TODO - add entry to remove txn log
	/*char txnkey[20];
	strcpy(txnkey, "txn-", 4);
	uuid_copy(txnkey + 4, op_ctx->uuid);
	leveldb_writebatch_delete(commit_batch, txnkey, TXN_UUID_LEN + 4);*/

	leveldb_write(db->db, db->w_options, commit_batch, &err);

	if (err) {
		LogDebug(COMPONENT_FSAL, "leveldb error: %s", err);
		leveldb_free(err);
		ret = -1;
	}

	leveldb_writebatch_destroy(commit_batch);

	return ret;
}

// cleanup txn entries
void txnfs_cache_cleanup(void)
{
	UDBG;
	assert(glist_null(&op_ctx->txn_cache) == 0);

	struct txnfs_cache_entry *entry;
	struct glist_head *glist, *glistn;

	glist_for_each_safe(glist, glistn, &op_ctx->txn_cache)
	{
		entry = glist_entry(glist, struct txnfs_cache_entry, glist);

		/* Remove this entry from list */
		glist_del(&entry->glist);

		/* And free it */
		gsh_free(entry);
	}
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

	UDBG;
	uuid_generate(uuid);
	uuid_unparse_lower(uuid, uuid_str);
	LogDebug(COMPONENT_FSAL, "generate uuid=%s\n", uuid_str);

	if (!glist_null(&op_ctx->txn_cache)) {
		return txnfs_cache_insert(txnfs_cache_entry_create, hdl_desc,
					  uuid);
	}

	/* write to database */
	leveldb_writebatch_t *commit_batch = leveldb_writebatch_create();
	leveldb_writebatch_put(commit_batch, uuid, TXN_UUID_LEN, hdl_desc->addr,
			       hdl_desc->len);
	leveldb_writebatch_put(commit_batch, hdl_desc->addr, hdl_desc->len,
			       uuid, TXN_UUID_LEN);
	leveldb_write(db->db, db->w_options, commit_batch, &err);

	if (err) {
		LogDebug(COMPONENT_FSAL, "leveldb error: %s", err);
		leveldb_free(err);
		ret = -1;
	}

	leveldb_writebatch_destroy(commit_batch);

	return ret;
}

int txnfs_db_get_uuid(struct gsh_buffdesc *hdl_desc, uuid_t uuid)
{
	UDBG;

	struct fsal_module *fs = op_ctx->fsal_export->fsal;
	struct txnfs_fsal_module *txnfs =
	    container_of(fs, struct txnfs_fsal_module, module);
	db_store_t *db = txnfs->db;

	// search txnfs compound cache
	if (!glist_null(&op_ctx->txn_cache) &&
	    txnfs_cache_get_uuid(hdl_desc, uuid) == 0) {
		return 0;
	}

	LogDebug(COMPONENT_FSAL, "HandleAddr: %p HandleLen: %zu",
		 hdl_desc->addr, hdl_desc->len);

	char *val;
	char *err = NULL;
	size_t val_len;
	val = leveldb_get(db->db, db->r_options, hdl_desc->addr,
			  (size_t)hdl_desc->len, &val_len, &err);

	if (err) {
		LogDebug(COMPONENT_FSAL, "leveldb error: %s", err);
		leveldb_free(err);
	}

	if (!val) {
		return -1;
	}

	assert(val_len == TXN_UUID_LEN);
	uuid_copy(uuid, val);
	free(val);
	return 0;
}

int txnfs_db_get_handle(uuid_t uuid, struct gsh_buffdesc *hdl_desc)
{
	struct fsal_module *fs = op_ctx->fsal_export->fsal;
	struct txnfs_fsal_module *txnfs = 
	    container_of(fs, struct txnfs_fsal_module, module);
	db_store_t *db = txnfs->db;

	/* look up in the cache first */
	if (!glist_null(&op_ctx->txn_cache) &&
	    txnfs_cache_get_handle(uuid, hdl_desc) == 0) {
		return 0;
	}

	char *val;
	size_t length;
	char *err = NULL;

	val = leveldb_get(db->db, db->r_options, uuid, sizeof(uuid_t),
			  &length, &err);

	if (err) {
		LogFatal(COMPONENT_FSAL, "leveldb error: %s", err);
	}

	if (!val)
		return -1;

	/* NOTE: Be sure to **free** hdl_desc->addr after use */
	hdl_desc->len = length;
	hdl_desc->addr = val;
	return 0;
}

int txnfs_db_delete_uuid(uuid_t uuid)
{
	UDBG;

	struct fsal_module *fs = op_ctx->fsal_export->fsal;
	struct txnfs_fsal_module *txnfs =
	    container_of(fs, struct txnfs_fsal_module, module);
	db_store_t *db = txnfs->db;

	if (!glist_null(&op_ctx->txn_cache)) {
		return txnfs_cache_delete_uuid(uuid);
	}

	char *val;
	char *err = NULL;
	size_t val_len;
	val = leveldb_get(db->db, db->r_options, uuid, TXN_UUID_LEN, &val_len,
			  &err);
	assert(val);

	if (err) {
		LogDebug(COMPONENT_FSAL, "leveldb error: %s", err);
		leveldb_free(err);
		return -1;
	}

	char uuid_str[UUID_STR_LEN];
	uuid_unparse_lower(uuid, uuid_str);
	LogDebug(COMPONENT_FSAL, "delete uuid=%s\n", uuid_str);

	leveldb_delete(db->db, db->w_options, uuid, TXN_UUID_LEN, &err);

	if (err) {
		LogDebug(COMPONENT_FSAL, "leveldb error: %s", err);
		leveldb_free(err);
		return -1;
	}

	return 0;
}

bool txnfs_db_handle_exists(struct gsh_buffdesc *hdl_desc)
{
	UDBG;
	uuid_t uuid;

	// search txnfs compound cache
	if (!glist_null(&op_ctx->txn_cache) &&
	    txnfs_cache_get_uuid(hdl_desc, uuid) == 0) {
		return 0;
	}
	return txnfs_db_get_uuid(hdl_desc, uuid) == 0;
}

/* TODO: we should consider storing @c fsal_obj_handle of the TXNFS root dir
 * in our private FSAL data structure to avoid always having to look up. */
void get_txn_root(struct fsal_obj_handle **root_handle, struct attrlist *attrs)
{
	struct fsal_obj_handle *root_entry = NULL;
	fsal_status_t ret = op_ctx->fsal_export->exp_ops.lookup_path(
	    op_ctx->fsal_export, op_ctx->ctx_export->fullpath, &root_entry,
	    attrs);
	assert(FSAL_IS_SUCCESS(ret));
	*root_handle = root_entry;
}
