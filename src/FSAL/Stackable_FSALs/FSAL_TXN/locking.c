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
#include "path_utils.h"
#include "txnfs_methods.h"
#include <assert.h>
#include <fsal_api.h>
#include <hashtable.h>
#include <nfs_proto_tools.h>

struct lock_req_vec {
	size_t size;
	size_t cap;
	lock_request_t *v;
};

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
	struct fsal_export *exp = (op_ctx->mdc_export) ? op_ctx->mdc_export : op_ctx->fsal_export;
	fsal_status_t ret;

	// topcall(ret = exp->exp_ops.wire_to_host(exp, FSAL_DIGEST_NFSV4, &buf, 0));
	// assert(FSAL_IS_SUCCESS(ret));

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
 *
 * @param[in] args Compound args
 * @param[in] lr_vec Lock request array: Should have adequate space to hold
 * 	      256 lock requests
 * 
 * @return Number of paths to be locked
 */
int find_relevant_handles(COMPOUND4args *args, lock_request_t *lr_vec)
{
	int i, ret = 0, veclen = 0;
	/* What we need is the path */
	char *current_path = gsh_calloc(1, PATH_MAX + 1);
	char *saved_path = gsh_calloc(1, PATH_MAX + 1);
	struct fsal_obj_handle *current = NULL;
	struct attrlist cur_attr = {0};
	utf8string utf8_name;
	char *name;

	/* let's start from ROOT */
	char *root_path = op_ctx->ctx_export->fullpath;
	strncpy(current_path, root_path, PATH_MAX);

	for (i = 0; i < args->argarray.argarray_len; i++) {
		struct nfs_argop4 *curop_arg = &args->argarray.argarray_val[i];

		int op = curop_arg->argop;
		nfs_fh4 *fh = NULL;

		switch (op) {
			case NFS4_OP_PUTFH:
				/* update current file handle */
				fh = &curop_arg->nfs_argop4_u.opputfh.object;

				current = fh_to_obj_handle(fh, &cur_attr);
				if (!current->absolute_path) {
					current->absolute_path = "/undefined";
				}
				strncpy(current_path, current->absolute_path, PATH_MAX);

				if (!current) {
					LogWarn(
					    COMPONENT_FSAL,
					    "Can't find obj_handle from fh.");
					ret = -ERR_FSAL_NOENT;
				}
				break;

			case NFS4_OP_PUTROOTFH:
				/* update current fh to root */
				memset(current_path, 0, PATH_MAX);
				strncpy(current_path, root_path, PATH_MAX);
				break;

			case NFS4_OP_SAVEFH:
				strncpy(saved_path, current_path, PATH_MAX);
				break;

			case NFS4_OP_RESTOREFH:
				strncpy(current_path, saved_path, PATH_MAX);
				break;

			case NFS4_OP_LOOKUP:;
				/* update current fh to the queried one */
				utf8_name = curop_arg->nfs_argop4_u.oplookup.objname;
				ret = nfs4_utf8string2dynamic(&utf8_name, UTF8_SCAN_ALL, &name);
				assert(ret == 0);
				ret = tc_path_join(current_path, name, current_path, PATH_MAX);
				assert(ret >= 0);
				free(name);
				break;

			case NFS4_OP_LOOKUPP:
				/* update current fh to its parent */
				ret = tc_path_join(current_path, "..", current_path, PATH_MAX);
				assert(ret >= 0);
				break;

				/* We don't have to worry about appending lock request
				 * until real read/write operations */
			case NFS4_OP_OPEN:;
				utf8string *u8name = extract_open_name(&curop_arg->nfs_argop4_u.opopen.claim);
				ret = nfs4_utf8string2dynamic(u8name, UTF8_SCAN_ALL, &name);
				assert(ret == 0);
				if (name) {
					tc_path_join(current_path, name, current_path, PATH_MAX);
					gsh_free(name);
				}
				/* If name is null, target is the current file */
					
				break;

			case NFS4_OP_CREATE:
				/* CREATE: lock parent dir (current) */
			case NFS4_OP_REMOVE:
				/* REMOVE: just lock the parent dir */
			case NFS4_OP_WRITE:;
				/* WRITE: lock current path */
				size_t pathlen = strnlen(current_path, PATH_MAX);
				char *pathbuf = gsh_calloc(1, pathlen + 1);
				strncpy(pathbuf, current_path, pathlen);
				lr_vec[veclen].path = pathbuf;
				lr_vec[veclen].write_lock = true;
				veclen++;
				break;

			case NFS4_OP_LINK:;
				/* LINK: lock src and dest dir
				 * saved_fh: source object
				 * current_fh: target dir */
				size_t destlen = strnlen(current_path, PATH_MAX);
				size_t srclen = strnlen(saved_path, PATH_MAX);
				char *destbuf = gsh_calloc(1, destlen + 1);
				char *srcbuf = gsh_calloc(1, srclen + 1);
				strncpy(destbuf, current_path, destlen);
				strncpy(srcbuf, saved_path, srclen);
				/* We should lock the parent of src, not src file */
				tc_path_join(srcbuf, "..", srcbuf, srclen);

				lr_vec[veclen].path = srcbuf;
				lr_vec[veclen].write_lock = false;
				veclen++;

				lr_vec[veclen].path = destbuf;
				lr_vec[veclen].write_lock = true;
				veclen++;
				break;

			case NFS4_OP_RENAME:
				/* rename is complex - let's not deal with it now */
				break;

			case NFS4_OP_COPY:
			case NFS4_OP_CLONE:
				/* they are rarely used - let's not care them for now */
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

	if (ret) {
		LogWarn(COMPONENT_FSAL,
			"Error %d occurred when"
			" analyzing lrq.\n",
			ret);
	}

	gsh_free(current_path);
	gsh_free(saved_path);
	return veclen;
}