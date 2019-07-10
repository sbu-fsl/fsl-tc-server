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

#include "log.h"
#include "txnfs_methods.h"
#include <assert.h>

/**
 * @brief Find the global backup directory
 *
 * This function checks if the global TXNFS backup root exists, and
 * outputs its object handle. It will return NULL if it does not exist.
 * If an error other than @c ERR_FSAL_NOENT occurs, the server will be
 * terminated.
 *
 * NOTE: This function assumes LOWER fsal, so the caller should
 * replace @c op_ctx->fsal_export with the corresponding @c sub_export.
 *
 * @param[in] txn_root		The root obj handle of the TXNFS export
 *
 * @return The @c fsal_obj_handle pointer of the backup root directory
 * 	   or NULL if the directory doesn't exist.
 */
struct fsal_obj_handle *query_backup_root(struct fsal_obj_handle *txn_root)
{
	struct fsal_obj_handle *txn_backup_root;
	struct txnfs_fsal_obj_handle *txn_root_entry;
	fsal_status_t ret;

	txn_root_entry =
	    container_of(txn_root, struct txnfs_fsal_obj_handle, obj_handle);
	assert(txn_root_entry);

	/* lookup the txnfs backup directory
	 * TXN_BKP_DIR is ".txn"
	 * We don't need attrs here so the last arg is NULL.
	 */
	ret = fsal_lookup(txn_root_entry->sub_handle, TXN_BKP_DIR,
			  &txn_backup_root, NULL);

	if (ret.major == 0)
		return txn_backup_root;
	else if (ret.major == ERR_FSAL_NOENT)
		return NULL;
	else {
		/* This is bad, so we should panic */
		LogFatal(COMPONENT_FSAL, "query_backup_root failed: %d",
			 ret.major);
	}
	/* the control flow will not reach here, but the compiler wants it */
	return NULL;
}

/**
 * @brief Check if the particular transcation's backup directory exists
 *
 * This function checks if the backup directory for the given transaction
 * exists, and returns its object handle. Similarly, if an errror other than
 * @c ERR_FSAL_NOENT occurs, this will abort the system.
 *
 * NOTE: This function assumes LOWER fsal, so the caller should
 * replace @c op_ctx->fsal_export with the corresponding @c sub_export.
 *
 * @param[in] backup_root	TXNFS's global backup directory. This should
 * 				be the @c sub_handle.
 * @param[in] txnid		Transaction ID
 *
 * @return The @c fsal_obj_handle pointer of the backup folder for the given
 * 	   transaction
 */
struct fsal_obj_handle *query_txn_backup(struct fsal_obj_handle *backup_root,
					 uint64_t txnid)
{
	struct fsal_obj_handle *backup_dir = NULL;
	fsal_status_t ret;
	char dir_name[BKP_FN_LEN];

	/* construct backup folder name */
	snprintf(dir_name, BKP_FN_LEN, "%lu", txnid);

	ret = fsal_lookup(backup_root, dir_name, &backup_dir, NULL);

	if (ret.major == 0)
		return backup_dir;
	else if (ret.major == ERR_FSAL_NOENT)
		return NULL;
	else {
		LogFatal(COMPONENT_FSAL, "query_txn_backup failed: %d",
			 ret.major);
	}
	return NULL;
}

/**
 * @brief Find the individual backup directory for the ongoing transaction
 *
 * This function will look up the individual backup folder for the transaction
 * that is being performed. If the folder does not exist, it will try to
 * create one.
 *
 * Note, that this function does not require @c txnid parameter because it
 * retrieves this from the context variable @c op_ctx.
 *
 * TODO: store backup root directory in @c txnfs_fsal_export so that we
 * won't have to look it up every time this is called.
 *
 * @param[out] bkp_handle	The @c fsal_obj_handle of the backup folder
 *
 * @return The FSAL status code.
 */
fsal_status_t txnfs_create_or_lookup_backup_dir(
    struct fsal_obj_handle **bkp_handle)
{
	UDBG;
	/* create txn backup directory */
	struct fsal_obj_handle *root_entry = NULL;
	struct fsal_obj_handle *txn_handle = NULL;
	struct attrlist attrs = {0};
	uint64_t txnid = op_ctx->txnid;
	char txnid_name[20] = {'\0'};
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};

	/* get txnfs root directory handle */
	get_txn_root(&root_entry, &attrs);

	struct txnfs_fsal_export *exp =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	/* Switch to the lower fsal */
	op_ctx->fsal_export = exp->export.sub_export;

	/* Config attributes if we need to create something */
	FSAL_CLEAR_MASK(attrs.valid_mask);
	FSAL_SET_MASK(attrs.valid_mask, ATTR_MODE | ATTR_OWNER | ATTR_GROUP);
	attrs.mode = 0666;
	attrs.owner = 0;
	attrs.group = 0;

	/* check if txn backup root exists */
	txn_handle = query_backup_root(root_entry);

	if (txn_handle == NULL) {
		/* create txn backup root
		 * note, we should use the sub_handle of root entry
		 * to create such a directory, and the generated handle
		 * should belong to the sub-export. */
		struct txnfs_fsal_obj_handle *txn_root_handle;
		txn_root_handle = container_of(
		    root_entry, struct txnfs_fsal_obj_handle, obj_handle);
		status =
		    fsal_create(txn_root_handle->sub_handle, TXN_BKP_DIR,
				DIRECTORY, &attrs, NULL, &txn_handle, NULL);
		assert(status.major == 0);
		assert(txn_handle);
	}

	*bkp_handle = query_txn_backup(txn_handle, txnid);

	if (*bkp_handle == NULL) {
		/* create txnid directory
		 * note, that @c txn_handle is already a handle that belongs
		 * to the lower fsal export. */
		snprintf(txnid_name, BKP_FN_LEN, "%lu", txnid);
		status = fsal_create(txn_handle, txnid_name, DIRECTORY, &attrs,
				     NULL, bkp_handle, NULL);
		assert(status.major == 0);
		assert(*bkp_handle);
	}

	op_ctx->fsal_export = &exp->export;

	return status;
}

/**
 * @brief Backup a file
 *
 * Make a backup of the file described by given @c fsal_obj_handle.
 *
 * @params[in] opidx	Operation index. This is used to compose the file name.
 * @params[in] src_hdl	The @c fsal_obj_handle of the source file
 *
 * @return FSAL status code
 */
fsal_status_t txnfs_backup_file(unsigned int opidx,
				struct fsal_obj_handle *src_hdl)
{
	UDBG;
	struct fsal_obj_handle *dst_hdl = NULL;
	struct fsal_obj_handle *root_entry = NULL;
	uint64_t copied = 0;
	struct attrlist attrs = {0};
	struct attrlist attrs_out = {0};
	char backup_name[20];

	struct txnfs_fsal_obj_handle *txn_src_hdl =
	    container_of(src_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	assert(txn_src_hdl);

	fsal_status_t status = txnfs_create_or_lookup_backup_dir(&root_entry);
	assert(status.major == 0);

	struct txnfs_fsal_export *exp =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;

	/* get file size */
	attrs_out.request_mask = ATTR_SIZE;
	status = get_optional_attrs(txn_src_hdl->sub_handle, &attrs_out);
	assert(status.major == 0);

	/* copy src to backup dir */
	/* TODO: currently we are copying the whole file, but we plan to
	 * implement copy_range where we copy only the parts changed by the
	 * compound operation. */
	if (attrs_out.filesize > 0) {
		/* create dst_handle */
		snprintf(backup_name, BKP_FN_LEN, "%d.bkp", opidx);
		FSAL_CLEAR_MASK(attrs.valid_mask);
		FSAL_SET_MASK(attrs.valid_mask,
			      ATTR_MODE | ATTR_OWNER | ATTR_GROUP);
		attrs.mode = 0666;
		attrs.owner = 0;
		assert(status.major == 0);
		status = fsal_copy(txn_src_hdl->sub_handle, 0 /* src_offset */,
				   dst_hdl, 0 /* dst_offset */,
				   attrs_out.filesize, &copied);

		assert(status.major == 0);
	}
	op_ctx->fsal_export = &exp->export;
	return status;
}

/**
 * @brief Rollback a transaction
 *
 * This function will be called if a transaction failed because of reasons
 * other than server crash. It will iterate through the compound to see
 * what have been done successfully, and try to undo these operations given
 * the backup files.
 *
 * @param[in] txnid	Transaction ID
 * @param[in] res	NFSv4 Compound object
 *
 * @return Status code. Will return 0 if done successfully.
 */
int txnfs_compound_restore(uint64_t txnid, COMPOUND4res *res)
{
	UDBG;
	int ret = 0;

	/* it makes no sense to call this if compound operation is completed */
	assert(res->status != NFS4_OK);

	/* call the payload function */
	ret = do_txn_rollback(txnid, res);

	return ret;
}
