/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Jim Lieb jlieb@panasas.com
 *
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/* file.c
 * File I/O methods for TXN module
 */

#include "config.h"

#include <assert.h>
#include "fsal.h"
#include "FSAL/access_check.h"
#include "fsal_convert.h"
#include <unistd.h>
#include <fcntl.h>
#include "FSAL/fsal_commonlib.h"
#include "txnfs_methods.h"
#include <time.h>

/* txnfs_close
 * Close the file if it is still open.
 * Yes, we ignor lock status.  Closing a file in POSIX
 * releases all locks but that is state and cache inode's problem.
 */

fsal_status_t txnfs_close(struct fsal_obj_handle *obj_hdl)
{
	struct txnfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct txnfs_fsal_obj_handle,
			     obj_handle);

	struct txnfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct txnfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
		handle->sub_handle->obj_ops->close(handle->sub_handle);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t txnfs_open2(struct fsal_obj_handle *obj_hdl,
			   struct state_t *state,
			   fsal_openflags_t openflags,
			   enum fsal_create_mode createmode,
			   const char *name,
			   struct attrlist *attrs_in,
			   fsal_verifier_t verifier,
			   struct fsal_obj_handle **new_obj,
			   struct attrlist *attrs_out,
			   bool *caller_perm_check)
{
	struct txnfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct txnfs_fsal_obj_handle,
			     obj_handle);
	struct txnfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct txnfs_fsal_export,
			     export);
	struct fsal_obj_handle *sub_handle = NULL;

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
		handle->sub_handle->obj_ops->open2(handle->sub_handle, state,
						  openflags, createmode, name,
						  attrs_in, verifier,
						  &sub_handle, attrs_out,
						  caller_perm_check);
	op_ctx->fsal_export = &export->export;

	if (sub_handle) {
		/* wrap the subfsal handle in a txnfs handle. */
		if (createmode == FSAL_NO_CREATE)
			return txnfs_alloc_and_check_handle(export, sub_handle,
						obj_hdl->fs, new_obj,
						status, NULL, false);
		else {
			/*LogDebug(COMPONENT_FSAL, "uuid_index: %d and uuids_len:%d",
					op_ctx->uuid_index, op_ctx->uuids_len);
			if (op_ctx->uuid_index >= op_ctx->uuids_len) {
				LogMajor(COMPONENT_FSAL,
						"uuid_index is greater than or equal to total uuids");
				return fsalstat(ERR_FSAL_INVAL, 0);
			}

			return txnfs_alloc_and_check_handle(export, sub_handle,
					obj_hdl->fs, new_obj,
					status, op_ctx->uuids[op_ctx->uuid_index++], true);*/
			return txnfs_alloc_and_check_handle(export, sub_handle,
					obj_hdl->fs, new_obj,
					status, NULL, true);
		}
	}

	return status;
}

bool txnfs_check_verifier(struct fsal_obj_handle *obj_hdl,
			   fsal_verifier_t verifier)
{
	struct txnfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct txnfs_fsal_obj_handle,
			     obj_handle);

	struct txnfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct txnfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	bool result =
		handle->sub_handle->obj_ops->check_verifier(handle->sub_handle,
							   verifier);
	op_ctx->fsal_export = &export->export;

	return result;
}

fsal_openflags_t txnfs_status2(struct fsal_obj_handle *obj_hdl,
				struct state_t *state)
{
	struct txnfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct txnfs_fsal_obj_handle,
			     obj_handle);

	struct txnfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct txnfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_openflags_t result =
	    handle->sub_handle->obj_ops->status2(handle->sub_handle, state);
	op_ctx->fsal_export = &export->export;

	return result;
}

fsal_status_t txnfs_reopen2(struct fsal_obj_handle *obj_hdl,
			     struct state_t *state,
			     fsal_openflags_t openflags)
{
	struct txnfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct txnfs_fsal_obj_handle,
			     obj_handle);

	struct txnfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct txnfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status = handle->sub_handle->obj_ops->reopen2(
	    handle->sub_handle, state, openflags);
	op_ctx->fsal_export = &export->export;

	return status;
}

void txnfs_read2(struct fsal_obj_handle *obj_hdl, bool bypass,
		 fsal_async_cb done_cb, struct fsal_io_arg *read_arg,
		 void *caller_arg)
{
	struct txnfs_fsal_obj_handle *handle =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	handle->sub_handle->obj_ops->read2(handle->sub_handle, bypass, done_cb,
					   read_arg, caller_arg);
	op_ctx->fsal_export = &export->export;
}

void txnfs_write2(struct fsal_obj_handle *obj_hdl, bool bypass,
		  fsal_async_cb done_cb, struct fsal_io_arg *write_arg,
		  void *caller_arg)
{
	/*db_kvpair_t kvpair;
	char key[20];
	struct attrlist attrs;
	struct fsal_obj_handle *sub_handle;
	struct fsal_export *sub_export;
	struct gsh_buffdesc fh_desc;*/

	struct txnfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct txnfs_fsal_obj_handle,
			     obj_handle);

	struct txnfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct txnfs_fsal_export,
			     export);

	/*snprintf(key, 20, "%ld", obj_hdl->fileid);
	LogDebug(COMPONENT_FSAL, "File ID received is %s", key);
	kvpair.key = key;
	kvpair.key_len = strlen(key);
	LogCrit(COMPONENT_FSAL, "get_keys = %d", get_keys(&kvpair, 1, db));
	fh_desc.addr = kvpair.val;
	fh_desc.len = kvpair.val_len;
	fsal_prepare_attrs(&attrs,
 				op_ctx->fsal_export->exp_ops.
 				fs_supported_attrs(op_ctx->fsal_export)
				& ~ATTR_ACL);
	sub_export = export->export.sub_export;
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status = sub_export->exp_ops.create_handle(sub_export,
							&fh_desc,
							&sub_handle,
							&attrs);
	op_ctx->fsal_export = &export->export;
	if (FSAL_IS_ERROR(status)) {
		LogWarn(COMPONENT_FSAL, "create_handle failed with %s", fsal_err_txt(status));
		fsal_release_attrs(&attrs);
		return status;
	}*/
	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	handle->sub_handle->obj_ops->write2(handle->sub_handle, bypass, done_cb,
					    write_arg, caller_arg);
	/*sub_handle->obj_ops.write2(sub_handle, bypass,
					  state, offset, buf_size,
					  buffer, write_amount,
					  fsal_stable, info);*/
	op_ctx->fsal_export = &export->export;
	//fsal_release_attrs(&attrs);
}

fsal_status_t txnfs_seek2(struct fsal_obj_handle *obj_hdl,
			   struct state_t *state,
			   struct io_info *info)
{
	struct txnfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct txnfs_fsal_obj_handle,
			     obj_handle);

	struct txnfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct txnfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
	    handle->sub_handle->obj_ops->seek2(handle->sub_handle, state, info);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t txnfs_io_advise2(struct fsal_obj_handle *obj_hdl,
				struct state_t *state,
				struct io_hints *hints)
{
	struct txnfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct txnfs_fsal_obj_handle,
			     obj_handle);

	struct txnfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct txnfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status = handle->sub_handle->obj_ops->io_advise2(
	    handle->sub_handle, state, hints);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t txnfs_commit2(struct fsal_obj_handle *obj_hdl, off_t offset,
			     size_t len)
{
	struct txnfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct txnfs_fsal_obj_handle,
			     obj_handle);

	struct txnfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct txnfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status = handle->sub_handle->obj_ops->commit2(
	    handle->sub_handle, offset, len);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t txnfs_lock_op2(struct fsal_obj_handle *obj_hdl,
			      struct state_t *state,
			      void *p_owner,
			      fsal_lock_op_t lock_op,
			      fsal_lock_param_t *req_lock,
			      fsal_lock_param_t *conflicting_lock)
{
	struct txnfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct txnfs_fsal_obj_handle,
			     obj_handle);

	struct txnfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct txnfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status = handle->sub_handle->obj_ops->lock_op2(
	    handle->sub_handle, state, p_owner, lock_op, req_lock,
	    conflicting_lock);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t txnfs_close2(struct fsal_obj_handle *obj_hdl,
			    struct state_t *state)
{
	struct txnfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct txnfs_fsal_obj_handle,
			     obj_handle);

	struct txnfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct txnfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
	    handle->sub_handle->obj_ops->close2(handle->sub_handle, state);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t txnfs_copy(struct fsal_obj_handle *src_hdl,
				uint64_t src_offset,
				struct fsal_obj_handle *dst_hdl,
				uint64_t dst_offset, uint64_t count,
				uint64_t *copied)
{
	struct txnfs_fsal_obj_handle *src_txn_hdl =
		container_of(src_hdl, struct txnfs_fsal_obj_handle,
				obj_handle);
	struct txnfs_fsal_obj_handle *dst_txn_hdl =
		container_of(dst_hdl, struct txnfs_fsal_obj_handle,
				obj_handle);
	struct txnfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct txnfs_fsal_export,
				export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status = src_txn_hdl->sub_handle->obj_ops->copy(
	    src_txn_hdl->sub_handle, src_offset, dst_txn_hdl->sub_handle,
	    dst_offset, count, copied);
	op_ctx->fsal_export = &export->export;

	return status;
	
}

fsal_status_t txnfs_start_compound(struct fsal_obj_handle *root_backup_hdl,
				   void *data)
{
	char name[20] = {0};
	time_t current_time;
	struct attrlist *attrs_out = NULL;
	struct attrlist attrs;
	// compound_data_t *cdata = (compound_data_t *)data;

	struct txnfs_fsal_obj_handle *txn_hdl =
		container_of(root_backup_hdl,
			     struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct txnfs_fsal_export,
				export);

	op_ctx->fsal_export = export->export.sub_export;
	LogCrit(COMPONENT_FSAL, "In TXN start");

	/* create timestamp based backup_dir here */
	current_time = time(NULL);
	if (current_time == ((time_t) - 1)) {
		LogCrit(COMPONENT_FSAL, "get time failed");
	}
	sprintf(name, "txn_%ld", current_time);

	memset(&attrs, 0, sizeof(struct attrlist));
	FSAL_SET_MASK(attrs.valid_mask, ATTR_MODE | ATTR_OWNER | ATTR_GROUP);
	attrs.mode = 777;
	attrs.owner = 667;
	attrs.group = 766;

	fsal_status_t status = txn_hdl->sub_handle->obj_ops->mkdir(
	    txn_hdl->sub_handle, name, &attrs,
	    /* TODO: &cdata->tc_backup_dir */ NULL, attrs_out);

	LogCrit(COMPONENT_FSAL, "Created backupdir at: %s", name);
	op_ctx->fsal_export = &export->export;
	// cdata->lh = lm_lock(lm, cdata->files, cdata->nr_files);
	return status;
}

fsal_status_t txnfs_end_compound(struct fsal_obj_handle *root_backup_hdl,
				 void *data)
{
	struct txnfs_fsal_obj_handle *txn_hdl =
		container_of(root_backup_hdl, struct txnfs_fsal_obj_handle,
				obj_handle);

	struct txnfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct txnfs_fsal_export,
				export);
	// compound_data_t *cdata = (compound_data_t *)data;

	op_ctx->fsal_export = export->export.sub_export;
	LogCrit(COMPONENT_FSAL, "In TXN end");
	fsal_status_t status = txn_hdl->sub_handle->obj_ops->end_compound(
	    txn_hdl->sub_handle, data);
	// unlock_handle(cdata->lh);
	// cdata->lh = NULL;

	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t txnfs_clone(struct fsal_obj_handle *src_hdl,
			  char **dst_name,
			  struct fsal_obj_handle *dir_hdl,
			  char *uuid)
{
	struct txnfs_fsal_obj_handle *txn_hdl =
		container_of(src_hdl, struct txnfs_fsal_obj_handle,
			     obj_handle);

	LogDebug(COMPONENT_FSAL, "txn: clone");

	fsal_status_t status = txn_hdl->sub_handle->obj_ops->clone(
	    txn_hdl->sub_handle, dst_name, dir_hdl, (char *)txn_hdl->uuid);
	if (dst_name)
		LogDebug(COMPONENT_FSAL, "txn: clone sucess, %s", *dst_name);
	else
		LogDebug(COMPONENT_FSAL, "txn: clone fail");
	return status;
}
