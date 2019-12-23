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
 * @brief This function checks if the RENAME compound can be considered as
 * a transaction. Note that we currently only support rename operations where
 * the src and dest are in the same directory.
 *
 * @param[in] args Compound args
 *
 * @return true if this RENAME compound can use transaction
 */
bool check_support_for_rename(COMPOUND4args *args)
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

				/* We only want the abs. path, so let's release
				 * the handle after use */
				topcall(current->obj_ops->release(current));
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
				ret = 0;
				free(name);
				break;

			case NFS4_OP_LOOKUPP:
				/* update current fh to its parent */
				ret = tc_path_join(current_path, "..",
						   current_path, PATH_MAX);
				assert(ret >= 0);
				ret = 0;
				break;

			case NFS4_OP_RENAME:
				break

			default:
				LogFullDebug(COMPONENT_FSAL,
					"Operation %d will not be"
					" counted for rename checking",
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
