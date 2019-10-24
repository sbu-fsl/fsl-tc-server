/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Stony Brook University 2016
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
#include "lock_manager.h"
#include "opvec.h"
#include "txnfs_methods.h"
#include <assert.h>
#include <fsal_api.h>
#include <hashtable.h>
#include <nfs_proto_tools.h>

struct lock_req_vec {
	size_t size;
	size_t cap;
	lock_request_t *v;
}

/**
 * @brief Get the MDCache fsal_obj_handle of TXNFS's root export dir.
 *
 * This is similar to get_txn_root(), but we need MDCACHE's fsal_obj_handle
 * here. Therefore we need a different function
 */
static int get_root_mdc_hdl(struct fsal_obj_handle **root_hdl, struct attrlist *attrs)
{
	struct fsal_obj_handle *mdc_root_entry = NULL;
	fsal_status_t ret;

	topcall(
	    ret = op_ctx->mdc_export->exp_ops.lookup_path(
	        op_ctx->fsal_export, op_ctx->ctx_export->fullpath, &mdc_root_entry, attrs)
	);

	assert(FSAL_IS_SUCCESS(ret));
	*root_hdl = mdc_root_entry;
}

/**
 * @brief Exchange CFH / Save CFH / Restore saved CFH to current
 *
 * These wrappers are intended to resolve reference issue. In locking.c
 * we should deal with MDCACHE's obj handles, ref countings DOES matter.
 *
 */
/* exchange_cfh: We assume that the new handle has been ref'd. */
static inline void exchange_cfh(struct fsal_obj_handle **current,
				struct fsal_obj_handle *new)
{
	if (!new) return;
	/* release a reference of the current handle */
	if (*current)
		topcall((*current)->obj_ops->put_ref(*current));
	*current = new;
}

static inline void save_cfh(struct fsal_obj_handle **saved,
			    struct fsal_obj_handle *current)
{
	if (!current) return;
	if (*saved)
		topcall((*saved)->obj_ops->put_ref(*saved));
	*saved = current;
}

static inline void restore_cfh(struct fsal_obj_handle **current,
			       struct fsal_obj_handle *saved)
{
	if (!saved) return;
	if (*current)
		topcall((*current)->obj_ops->put_ref(*current));
	*current = saved;
}

/**
 * @brief Convert NFS4 file handle into MDCache fsal_obj_handle
 *
 * @param[in] fh	NFSv4 file handle presented in compound args
 *
 * @return corresponding fsal_obj_handle
 */
static struct fsal_obj_handle *fh_to_obj_handle(nfs_fh4 *fh,
						struct attrlist *attrs)
{
	file_handle_v4_t *file_handle = (file_handle_v4_t *)fh->nfs_fh4_val;
	struct gsh_buffdesc buf = {.addr = file_handle->fsopaque,
				   .len = file_handle->fs_len};
	struct fsal_obj_handle *handle = NULL;
	struct attrlist _attrs = {0};
	struct fsal_export *exp = op_ctx->mdc_export;
	fsal_status_t ret;

	topcall(ret = exp->exp_ops.wire_to_host(exp, FSAL_DIGEST_NFSV4, &buf, 0));
	assert(FSAL_IS_SUCCESS(ret));

	topcall(ret = exp->exp_ops.create_handle(exp, &buf, &handle, &_attrs));
	if (FSAL_IS_SUCCESS(ret)) {
		if (attrs) *attrs = _attrs;
		return handle;
	} else {
		LogWarn(COMPONENT_FSAL, "can't get obj handle from fh: %d",
			ret.major);
		return NULL;
	}
}

/**
 * @brief Replay LOOKUP operation
 *
 * A helper to perform a file lookup according to the given LOOKUP4args
 * parameter, and update the current file handle & attrs
 */

static inline int replay_lookup(utf8string *encoded_name,
				struct fsal_obj_handle **current,
				struct attrlist *attrs)
{
	char *name;
	struct attrlist queried_attrs = {0};
	struct fsal_obj_handle *queried = NULL;
	fsal_status_t status;

	nfs4_utf8string2dynamic(encoded_name, UTF8_SCAN_ALL, &name);

	topcall(status = fsal_lookup(*current, name, &queried, &queried_attrs));

	/* cleanup *name after use */
	gsh_free(name);

	if (!queried) {
		return ERR_FSAL_NOENT;
	}

	if (FSAL_IS_ERROR(status)) {
		LogDebug(COMPONENT_FSAL, "replay lookup failed:");
		LogDebug(COMPONENT_FSAL, "%s", msg_fsal_err(status.major));
		return status.major;
	}

	exchange_cfh(current, queried);
	if (attrs) *attrs = queried_attrs;
	return 0;
}

/**
 * @brief Extract the name for the file to be opened
 *
 * This helper extract the name of a file from a given OPEN4args.claim
 * structure. Note that OPEN is very complex and we have to consider multiple
 * scenario depending on OPEN4args.(open_claim4)claim.(open_claim_type4)claim.
 *
 * @param[in] claim	The given open_claim4 structure pointer
 *
 * @return	The encoded utf8string d/s. If it's a NULL pointer, use
 * 		the current file handle instead.
 */
static utf8string *extract_open_name(open_claim4 *claim)
{
	switch (claim->claim) {
		case CLAIM_NULL:
			return &claim->open_claim4_u.file;
			break;

		case CLAIM_DELEGATE_CUR:
			return &claim->open_claim4_u.delegate_cur_info.file;
			break;

		case CLAIM_PREVIOUS:
		case CLAIM_FH:
		case CLAIM_DELEG_PREV_FH:
		case CLAIM_DELEG_CUR_FH:
		default:
			break;
	}

	return NULL;
}

/**
 * @brief Analyze compound args and extract fsal object handles that will
 * involve in the compound execution.
 */
int find_relevant_handles(COMPOUND4args *args)
{
	int i, ret = 0;
	struct fsal_obj_handle *root = NULL;
	struct fsal_obj_handle *current = NULL;
	struct fsal_obj_handle *saved = NULL;
	fsal_status_t status;
	struct attrlist cur_attr = {0};
	struct lock_req_vec vector;

	/* initialize op vector */
	vector.size = 0;
	vector.cap = args->argarray.argarray_len * 2;
	vector.v = gsh_calloc(vector.cap, sizeof(lock_request_t));

	/* let's start from ROOT */
	get_root_mdc_hdl(&root, &cur_attr);
	exchange_cfh(&current, root);

	for (i = 0; i < args->argarray.argarray_len; i++) {
		struct nfs_argop4 *curop_arg = &args->argarray.argarray_val[i];

		int op = curop_arg->argop;
		struct fsal_obj_handle *temp;
		nfs_fh4 *fh = NULL;

		switch (op) {
			case NFS4_OP_PUTFH:
				/* update current file handle */
				fh = &curop_arg->nfs_argop4_u.opputfh.object;

				temp = fh_to_obj_handle(fh, &cur_attr);
				exchange_cfh(&current, temp);
				if (!current) {
					LogWarn(
					    COMPONENT_FSAL,
					    "Can't find obj_handle from fh.");
					ret = ERR_FSAL_NOENT;
				}
				break;

			case NFS4_OP_PUTROOTFH:
				/* update current fh to root */
				exchange_cfh(&current, root);
				break;

			case NFS4_OP_SAVEFH:
				save_cfh(&saved, current);
				break;

			case NFS4_OP_RESTOREFH:
				if (!saved) {
					ret = ERR_FSAL_FAULT;
					break;
				}
				restore_cfh(&current, saved);
				break;

			case NFS4_OP_LOOKUP:
				/* update current fh to the queried one */
				utf8string *name = curop_arg->nfs_argop4_u.oplookup.objname;
				ret = replay_lookup(name, &current,
						    &cur_attr);
				break;

			case NFS4_OP_LOOKUPP:
				/* update current fh to its parent */
				status =
				    replay_lookup("..", &current, &cur_attr);
				ret = status.major;
				exchange_cfh(&current, temp);
				break;

				/* Treat OPEN similar to LOOKUP. It's just
				 * the arg structure is different */
			case NFS4_OP_OPEN:
				utf8string *name = extract_open_name(&curop_arg->nfs_argop4_u.opopen.claim);
				if (name)
					ret = replay_lookup(curop_arg, &current, txnid, i);
				break;

			case NFS4_OP_CREATE:
			case NFS4_OP_LINK:
			case NFS4_OP_REMOVE:
			case NFS4_OP_RENAME:
			case NFS4_OP_WRITE:
			case NFS4_OP_COPY:
			case NFS4_OP_CLONE:
				ret = opvec_push(&vector, i, op, curop_arg,
						 curop_res, current, saved);
				break;

			default:
				LogWarn(COMPONENT_FSAL,
					"Operation %d will not be"
					" counted for rollback",
					op);
				break;
		}

		if (ret != 0) {
			LogWarn(COMPONENT_FSAL,
				"Error %d occurred when"
				"analyzing the compound. Txn rollback will"
				"not be supported. opidx=%d",
				ret, i);
			break;
		}
	}

	if (!ret) {
		ret = dispatch_undoer(&vector);
		if (ret)
			LogWarn(COMPONENT_FSAL,
				"Error %d occurred when"
				" executing rollback.\n",
				ret);
	}

	release_all_handles();
	opvec_destroy(&vector);
	return ret;
}
