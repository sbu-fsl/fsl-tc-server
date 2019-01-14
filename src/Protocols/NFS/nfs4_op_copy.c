/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright Stony Brook University  (2016)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @file    nfs4_op_copy.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 *
 *
 */
#include "config.h"
#include "fsal.h"
#include "log.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_convert.h"
#include "nfs_file_handle.h"
#include "sal_functions.h"
/**
 * @brief The NFS4_OP_COPY operation
 *
 * This function implemenats the NFS4_OP_COPY operation. This
 * function can be called only from nfs4_Compound
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 373
 */

int nfs4_op_copy(struct nfs_argop4 *op, compound_data_t *data,
                 struct nfs_resop4 *resp) {
	COPY4args *const arg_COPY4 = &op->nfs_argop4_u.opcopy;
	COPY4res *const res_COPY4 = &resp->nfs_resop4_u.opcopy;
	struct fsal_obj_handle *dst_handle = NULL;
	struct fsal_obj_handle *src_handle = NULL;
	size_t copied = 0;
	struct gsh_buffdesc verf_desc;
	fsal_status_t fsal_status;

	LogDebug(COMPONENT_FSAL, "Entered nfs4_op_copy. Sizeof COPY4args, COPY4res, nfs_argop4, nfs_resop4 = %ld, %ld, %ld, %ld", sizeof(COPY4args), sizeof(COPY4res), sizeof(nfs_argop4), sizeof(nfs_resop4));
	resp->resop = NFS4_OP_COPY;
	res_COPY4->cr_status = 0;

	/* Do basic checks on a filehandle */
	res_COPY4->cr_status = nfs4_sanity_check_FH(data, REGULAR_FILE, false);
	if (res_COPY4->cr_status != NFS4_OK)
		goto out;

	res_COPY4->cr_status =
	    nfs4_sanity_check_saved_FH(data, REGULAR_FILE, false);
	if (res_COPY4->cr_status != NFS4_OK)
		goto out;

	if (nfs_in_grace()) {
		res_COPY4->cr_status = NFS4ERR_GRACE;
		goto out;
	}

	dst_handle = data->current_obj;
	src_handle = data->saved_obj;
	/* RFC: "SAVED_FH and CURRENT_FH must be different files." */
	if (src_handle == dst_handle) {
		res_COPY4->cr_status = NFS4ERR_INVAL;
		goto out;
	}

	fsal_status = fsal_copy(src_handle, arg_COPY4->ca_src_offset,
					dst_handle, arg_COPY4->ca_dst_offset,
					arg_COPY4->ca_count, &copied);
	if (FSAL_IS_ERROR(fsal_status)) {
		LogDebug(COMPONENT_FSAL, "Error in fsal_copy");
		res_COPY4->cr_status = nfs4_Errno_status(fsal_status);
		goto out;
	}

	res_COPY4->COPY4res_u.cr_bytes_copied = copied;
	res_COPY4->COPY4res_u.cr_resok4.wr_ids = 0;
	res_COPY4->COPY4res_u.cr_resok4.wr_count = copied;
	/* FIXME: for simplicity, we always sync file after copy */
	res_COPY4->COPY4res_u.cr_resok4.wr_committed = FILE_SYNC4;

	verf_desc.addr = &res_COPY4->COPY4res_u.cr_resok4.wr_writeverf;
	verf_desc.len = sizeof(verifier4);
	op_ctx->fsal_export->exp_ops.get_write_verifier(op_ctx->fsal_export, &verf_desc);

out:
	LogDebug(COMPONENT_FSAL, "Exited nfs4_op_copy. retval = %d", res_COPY4->cr_status);
	return res_COPY4->cr_status;
}

/**
 * @brief Free memory allocated for COPY result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_COPY operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_copy_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}
