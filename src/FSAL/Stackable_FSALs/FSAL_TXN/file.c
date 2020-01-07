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

/* file.c
 * File I/O methods for TXN module
 */

#include "config.h"

#include "FSAL/access_check.h"
#include "FSAL/fsal_commonlib.h"
#include "fsal.h"
#include "fsal_convert.h"
#include "txnfs_methods.h"
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>

/**
 * @brief Callback arg for TXN async callbacks
 *
 * TXN needs to know what its object is related to the sub-FSAL's object.
 * This wraps the given callback arg with TXN specific info
 */
struct null_async_arg {
	struct fsal_obj_handle *obj_hdl; /**< TXN's handle */
	fsal_async_cb cb;		 /**< Wrapped callback */
	void *cb_arg;			 /**< Wrapped callback data */
};

/**
 * @brief Callback for TXN async calls
 *
 * Unstack, and call up.
 *
 * @param[in] obj		Object being acted on
 * @param[in] ret		Return status of call
 * @param[in] obj_data		Data for call
 * @param[in] caller_data	Data for caller
 */
void null_async_cb(struct fsal_obj_handle *obj, fsal_status_t ret,
		   void *obj_data, void *caller_data)
{
	struct fsal_export *save_exp = op_ctx->fsal_export;
	struct null_async_arg *arg = caller_data;

	op_ctx->fsal_export = save_exp->super_export;
	arg->cb(arg->obj_hdl, ret, obj_data, arg->cb_arg);
	op_ctx->fsal_export = save_exp;

	gsh_free(arg);
}

/* txnfs_close
 * Close the file if it is still open.
 * Yes, we ignor lock status.  Closing a file in POSIX
 * releases all locks but that is state and cache inode's problem.
 */

fsal_status_t txnfs_close(struct fsal_obj_handle *obj_hdl)
{
	UDBG;
	struct txnfs_fsal_obj_handle *handle =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
	    handle->sub_handle->obj_ops->close(handle->sub_handle);
	op_ctx->fsal_export = &export->export;

	txnfs_tracepoint(subfsal_op_done, status.major, op_ctx->opidx,
			 op_ctx->txnid, "close");
	return status;
}

fsal_status_t txnfs_open2(struct fsal_obj_handle *obj_hdl,
			  struct state_t *state, fsal_openflags_t openflags,
			  enum fsal_create_mode createmode, const char *name,
			  struct attrlist *attrs_in, fsal_verifier_t verifier,
			  struct fsal_obj_handle **new_obj,
			  struct attrlist *attrs_out, bool *caller_perm_check)
{
	UDBG;
	struct txnfs_fsal_obj_handle *handle =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);
	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);
	struct fsal_obj_handle *sub_handle = NULL;

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status = handle->sub_handle->obj_ops->open2(
	    handle->sub_handle, state, openflags, createmode, name, attrs_in,
	    verifier, &sub_handle, attrs_out, caller_perm_check);
	op_ctx->fsal_export = &export->export;

	txnfs_tracepoint(subfsal_op_done, status.major, op_ctx->opidx,
			 op_ctx->txnid, "open");
	if (sub_handle) {
		bool is_creation = createmode != FSAL_NO_CREATE;
		/* wrap the subfsal handle in a txnfs handle. */
		const char *parent_path = obj_hdl->absolute_path;
		const char *filename = (is_creation) ? name : "";

		return txnfs_alloc_and_check_handle(
		    export, sub_handle, obj_hdl->fs, new_obj, parent_path,
		    filename, status, is_creation);
	}

	return status;
}

bool txnfs_check_verifier(struct fsal_obj_handle *obj_hdl,
			  fsal_verifier_t verifier)
{
	UDBG;
	struct txnfs_fsal_obj_handle *handle =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	bool result = handle->sub_handle->obj_ops->check_verifier(
	    handle->sub_handle, verifier);
	op_ctx->fsal_export = &export->export;

	txnfs_tracepoint(subfsal_op_done, (int)result, op_ctx->opidx,
			 op_ctx->txnid, "check_verifier");
	return result;
}

fsal_openflags_t txnfs_status2(struct fsal_obj_handle *obj_hdl,
			       struct state_t *state)
{
	UDBG;
	struct txnfs_fsal_obj_handle *handle =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_openflags_t result =
	    handle->sub_handle->obj_ops->status2(handle->sub_handle, state);
	op_ctx->fsal_export = &export->export;

	txnfs_tracepoint(subfsal_op_done, result, op_ctx->opidx,
			 op_ctx->txnid, "status2");

	return result;
}

fsal_status_t txnfs_reopen2(struct fsal_obj_handle *obj_hdl,
			    struct state_t *state, fsal_openflags_t openflags)
{
	UDBG;
	struct txnfs_fsal_obj_handle *handle =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status = handle->sub_handle->obj_ops->reopen2(
	    handle->sub_handle, state, openflags);
	op_ctx->fsal_export = &export->export;

	txnfs_tracepoint(subfsal_op_done, status.major, op_ctx->opidx,
			 op_ctx->txnid, "reopen2");
	return status;
}

void txnfs_read2(struct fsal_obj_handle *obj_hdl, bool bypass,
		 fsal_async_cb done_cb, struct fsal_io_arg *read_arg,
		 void *caller_arg)
{
	UDBG;
	struct txnfs_fsal_obj_handle *handle =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);
	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);
	struct null_async_arg *arg;
	
	/* Set up async callback */
	arg = gsh_calloc(1, sizeof(*arg));
	arg->obj_hdl = obj_hdl;
	arg->cb = done_cb;
	arg->cb_arg = caller_arg;

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	handle->sub_handle->obj_ops->read2(handle->sub_handle, bypass,
					   null_async_cb, read_arg, arg);
	op_ctx->fsal_export = &export->export;

	txnfs_tracepoint(subfsal_op_done, read_arg->io_amount, op_ctx->opidx,
			 op_ctx->txnid, "read2");
}

void txnfs_write2(struct fsal_obj_handle *obj_hdl, bool bypass,
		  fsal_async_cb done_cb, struct fsal_io_arg *write_arg,
		  void *caller_arg)
{
	struct txnfs_fsal_obj_handle *handle =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);
	struct null_async_arg *arg;
	
	/* Set up async callback */
	arg = gsh_calloc(1, sizeof(*arg));
	arg->obj_hdl = obj_hdl;
	arg->cb = done_cb;
	arg->cb_arg = caller_arg;

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	handle->sub_handle->obj_ops->write2(handle->sub_handle, bypass,
					    null_async_cb, write_arg, arg);
	op_ctx->fsal_export = &export->export;

	txnfs_tracepoint(subfsal_op_done, write_arg->io_amount, op_ctx->opidx,
			 op_ctx->txnid, "write2");
}

fsal_status_t txnfs_seek2(struct fsal_obj_handle *obj_hdl,
			  struct state_t *state, struct io_info *info)
{
	UDBG;
	struct txnfs_fsal_obj_handle *handle =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
	    handle->sub_handle->obj_ops->seek2(handle->sub_handle, state, info);
	op_ctx->fsal_export = &export->export;

	txnfs_tracepoint(subfsal_op_done, status.major, op_ctx->opidx,
			 op_ctx->txnid, "seek2");
	return status;
}

fsal_status_t txnfs_io_advise2(struct fsal_obj_handle *obj_hdl,
			       struct state_t *state, struct io_hints *hints)
{
	UDBG;
	struct txnfs_fsal_obj_handle *handle =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status = handle->sub_handle->obj_ops->io_advise2(
	    handle->sub_handle, state, hints);
	op_ctx->fsal_export = &export->export;

	txnfs_tracepoint(subfsal_op_done, status.major, op_ctx->opidx,
			 op_ctx->txnid, "io_advise2");
	return status;
}

fsal_status_t txnfs_commit2(struct fsal_obj_handle *obj_hdl, off_t offset,
			    size_t len)
{
	UDBG;
	struct txnfs_fsal_obj_handle *handle =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status = handle->sub_handle->obj_ops->commit2(
	    handle->sub_handle, offset, len);
	op_ctx->fsal_export = &export->export;

	txnfs_tracepoint(subfsal_op_done, status.major, op_ctx->opidx,
			 op_ctx->txnid, "commit2");
	return status;
}

fsal_status_t txnfs_lock_op2(struct fsal_obj_handle *obj_hdl,
			     struct state_t *state, void *p_owner,
			     fsal_lock_op_t lock_op,
			     fsal_lock_param_t *req_lock,
			     fsal_lock_param_t *conflicting_lock)
{
	UDBG;
	struct txnfs_fsal_obj_handle *handle =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status = handle->sub_handle->obj_ops->lock_op2(
	    handle->sub_handle, state, p_owner, lock_op, req_lock,
	    conflicting_lock);
	op_ctx->fsal_export = &export->export;

	txnfs_tracepoint(subfsal_op_done, status.major, op_ctx->opidx,
			 op_ctx->txnid, "lock_op2");
	return status;
}

fsal_status_t txnfs_close2(struct fsal_obj_handle *obj_hdl,
			   struct state_t *state)
{
	UDBG;
	struct txnfs_fsal_obj_handle *handle =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
	    handle->sub_handle->obj_ops->close2(handle->sub_handle, state);
	op_ctx->fsal_export = &export->export;

	txnfs_tracepoint(subfsal_op_done, status.major, op_ctx->opidx,
			 op_ctx->txnid, "close2");
	return status;
}

fsal_status_t txnfs_fallocate(struct fsal_obj_handle *obj_hdl,
			      struct state_t *state, uint64_t offset,
			      uint64_t length, bool allocate)
{
	UDBG;
	struct txnfs_fsal_obj_handle *handle =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);
	fsal_status_t status;

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	status = handle->sub_handle->obj_ops->fallocate(
	    handle->sub_handle, state, offset, length, allocate);
	op_ctx->fsal_export = &export->export;

	txnfs_tracepoint(subfsal_op_done, status.major, op_ctx->opidx,
			 op_ctx->txnid, "fallocate");
	return status;
}

fsal_status_t txnfs_copy(struct fsal_obj_handle *src_hdl, uint64_t src_offset,
			 struct fsal_obj_handle *dst_hdl, uint64_t dst_offset,
			 uint64_t count, uint64_t *copied)
{
	struct txnfs_fsal_obj_handle *txn_src_hdl =
	    container_of(src_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_obj_handle *txn_dst_hdl =
	    container_of(dst_hdl, struct txnfs_fsal_obj_handle, obj_handle);
	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);
	fsal_status_t status;

	op_ctx->fsal_export = export->export.sub_export;
	status = txn_src_hdl->sub_handle->obj_ops->copy(
	    txn_src_hdl->sub_handle, src_offset, txn_dst_hdl->sub_handle,
	    dst_offset, count, copied);
	op_ctx->fsal_export = &export->export;

	txnfs_tracepoint(subfsal_op_done, status.major, op_ctx->opidx,
			 op_ctx->txnid, "copy");
	return status;
}

/* NOTE: Not sure if this method is needed in real workloads */
fsal_status_t txnfs_clone(struct fsal_obj_handle *src_hdl, char **dst_name,
			  struct fsal_obj_handle *dst_hdl, char *file_uuid)
{
	fsal_status_t fsal_status;
	struct txnfs_fsal_obj_handle *txn_src_hdl =
	    container_of(src_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_obj_handle *txn_dst_hdl =
	    container_of(dst_hdl, struct txnfs_fsal_obj_handle, obj_handle);
	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	LogDebug(COMPONENT_FSAL, "Clone in TXNFS layer");
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status = txn_src_hdl->sub_handle->obj_ops->clone(
	    txn_src_hdl->sub_handle, dst_name, txn_dst_hdl->sub_handle,
	    file_uuid);
	op_ctx->fsal_export = &export->export;
	LogDebug(COMPONENT_FSAL, "Returned to TXNFS layer");

	txnfs_tracepoint(subfsal_op_done, fsal_status.major, op_ctx->opidx,
			 op_ctx->txnid, "clone");
	return fsal_status;
}

fsal_status_t txnfs_clone2(struct fsal_obj_handle *src_hdl, loff_t *off_in,
			   struct fsal_obj_handle *dst_hdl, loff_t *off_out,
			   size_t len, unsigned int flags)
{
	struct txnfs_fsal_obj_handle *txn_hdl =
	    container_of(src_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_obj_handle *txn_hdl1 =
	    container_of(dst_hdl, struct txnfs_fsal_obj_handle, obj_handle);
	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status = txn_hdl->sub_handle->obj_ops->clone2(
	    txn_hdl->sub_handle, off_in, txn_hdl1->sub_handle, off_out, len,
	    flags);
	op_ctx->fsal_export = &export->export;

	txnfs_tracepoint(subfsal_op_done, status.major, op_ctx->opidx,
			 op_ctx->txnid, "clone2");
	return status;
}
