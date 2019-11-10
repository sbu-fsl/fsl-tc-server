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

#include "lock_manager.h"
#include "log.h"
#include "opvec.h"
#include "path_utils.h"
#include "txnfs_methods.h"
#include <assert.h>
#include <fsal_api.h>
#include <hashtable.h>
#include <nfs_proto_tools.h>

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
	struct fsal_export *exp =
	    (op_ctx->mdc_export) ? op_ctx->mdc_export : op_ctx->fsal_export;
	fsal_status_t ret;

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

static void add_lock_request(lock_request_t *lrs, int *pos, const char *path, bool is_write)
{
	size_t pathlen = strnlen(path, PATH_MAX);
	char *pathbuf = gsh_calloc(1, pathlen + 1);
	strncpy(pathbuf, path, pathlen);
	lrs[*pos].path = pathbuf;
	lrs[*pos].write_lock = is_write;
	(*pos)++;
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
				strncpy(current_path, current->absolute_path,
					PATH_MAX);

				if (!current) {
					LogFatal(
					    COMPONENT_FSAL,
					    "Can't find obj_handle from fh.");
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
				utf8_name =
				    curop_arg->nfs_argop4_u.oplookup.objname;
				ret = nfs4_utf8string2dynamic(
				    &utf8_name, UTF8_SCAN_ALL, &name);
				assert(ret == 0);
				ret = tc_path_join(current_path, name,
						   current_path, PATH_MAX);
				assert(ret >= 0);
				free(name);
				break;

			case NFS4_OP_LOOKUPP:
				/* update current fh to its parent */
				ret = tc_path_join(current_path, "..",
						   current_path, PATH_MAX);
				assert(ret >= 0);
				break;

				/* We don't have to worry about appending lock
				 * request until real read/write operations */
			case NFS4_OP_OPEN:;
				utf8string *u8name = extract_open_name(
				    &curop_arg->nfs_argop4_u.opopen.claim);
				char *parent_path = gsh_calloc(1, PATH_MAX);

				/* If the OPEN operation creates new file, we
				 * should lock the parent directory. If that
				 * is the case, the current path should be the
				 * parent dir, not the file. */
				if (u8name) {
					strncpy(parent_path, current_path, PATH_MAX);
					ret = nfs4_utf8string2dynamic(
					    u8name, UTF8_SCAN_ALL, &name);
					assert(ret == 0);
					tc_path_join(current_path, name,
						     current_path, PATH_MAX);
					gsh_free(name);
				} else {
				/* If name is null, target is the current file.
				 * In that case the parent should be manually
				 * constructed. */
					tc_path_join(current_path, "..", parent_path, PATH_MAX);
				}

				/* request lock for parent path of the open
				 * target. */
				if (curop_arg->nfs_argop4_u.opopen.openhow.opentype & OPEN4_CREATE)
					add_lock_request(lr_vec, &veclen, parent_path, true);
				else
					add_lock_request(lr_vec, &veclen, parent_path, false);

				gsh_free(parent_path);
				break;

			case NFS4_OP_CREATE:
				/* CREATE: lock parent dir (current) */
			case NFS4_OP_REMOVE:
				/* REMOVE: just lock the parent dir */
			case NFS4_OP_WRITE:
				/* WRITE: lock current path */
				add_lock_request(lr_vec, &veclen, current_path, true);
				break;

			case NFS4_OP_LINK:;
				/* LINK: lock src and dest dir
				 * saved_fh: source object
				 * current_fh: target dir */
				size_t srclen = strnlen(saved_path, PATH_MAX);
				char *srcbuf = gsh_calloc(1, srclen + 1);
				char *destbuf = current_path;
				strncpy(srcbuf, saved_path, srclen);
				/* We should lock the parent of src, not src
				 * file */
				tc_path_join(srcbuf, "..", srcbuf, srclen);

				add_lock_request(lr_vec, &veclen, srcbuf, false);
				add_lock_request(lr_vec, &veclen, destbuf, true);
				gsh_free(srcbuf);
				break;

			case NFS4_OP_GETATTR:;
				/* GETATTR: used to check file existence */
				char *parent = gsh_calloc(1, PATH_MAX);
				tc_path_join(current_path, "..", parent, PATH_MAX);
				add_lock_request(lr_vec, &veclen, parent, false);

			case NFS4_OP_RENAME:
				/* rename is complex - let's not deal with it
				 * now */
				break;

			case NFS4_OP_COPY:
			case NFS4_OP_CLONE:
				/* they are rarely used - let's not care them
				 * for now */
				break;

			default:
				LogWarn(COMPONENT_FSAL,
					"Operation %d will not be"
					" counted for rollback",
					op);
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
