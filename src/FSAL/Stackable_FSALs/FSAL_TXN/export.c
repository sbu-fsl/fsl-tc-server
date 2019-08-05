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

/* export.c
 * TXNFS export object
 */

#include "config.h"

#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "config_parsing.h"
#include "export_mgr.h"
#include "fsal.h"
#include "fsal_convert.h"
#include "gsh_list.h"
#include "nfs_exports.h"
#include "nfs_proto_data.h"
#include "txn_logger.h"
#include "txnfs_methods.h"
#include <dlfcn.h>
#include <libgen.h> /* used for 'dirname' */
#include <nfs_proto_tools.h>
#include <os/mntent.h>
#include <os/quota.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>

/* helpers to/from other NULL objects
 */

/* export object methods
 */

static void release(struct fsal_export *exp_hdl)
{
	struct txnfs_fsal_export *myself;
	struct fsal_module *sub_fsal;

	myself = container_of(exp_hdl, struct txnfs_fsal_export, export);
	sub_fsal = myself->export.sub_export->fsal;

	/* Release the sub_export */
	myself->export.sub_export->exp_ops.release(myself->export.sub_export);
	fsal_put(sub_fsal);

	LogFullDebug(COMPONENT_FSAL, "FSAL %s refcount %" PRIu32,
		     sub_fsal->name, atomic_fetch_int32_t(&sub_fsal->refcount));

	fsal_detach_export(exp_hdl->fsal, &exp_hdl->exports);
	free_export_ops(exp_hdl);

	gsh_free(myself); /* elvis has left the building */
}

static fsal_status_t get_dynamic_info(struct fsal_export *exp_hdl,
				      struct fsal_obj_handle *obj_hdl,
				      fsal_dynamicfsinfo_t *infop)
{
	struct txnfs_fsal_export *exp =
	    container_of(exp_hdl, struct txnfs_fsal_export, export);

	struct txnfs_fsal_obj_handle *handle =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	/* calling subfsal method */
	op_ctx->fsal_export = exp->export.sub_export;
	fsal_status_t status = op_ctx->fsal_export->exp_ops.get_fs_dynamic_info(
	    op_ctx->fsal_export, handle->sub_handle, infop);
	op_ctx->fsal_export = &exp->export;

	return status;
}

static bool fs_supports(struct fsal_export *exp_hdl,
			fsal_fsinfo_options_t option)
{
	struct txnfs_fsal_export *exp =
	    container_of(exp_hdl, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;
	bool result = exp->export.sub_export->exp_ops.fs_supports(
	    exp->export.sub_export, option);

	op_ctx->fsal_export = &exp->export;

	return result;
}

static uint64_t fs_maxfilesize(struct fsal_export *exp_hdl)
{
	struct txnfs_fsal_export *exp =
	    container_of(exp_hdl, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;
	uint64_t result = exp->export.sub_export->exp_ops.fs_maxfilesize(
	    exp->export.sub_export);

	op_ctx->fsal_export = &exp->export;

	return result;
}

static uint32_t fs_maxread(struct fsal_export *exp_hdl)
{
	struct txnfs_fsal_export *exp =
	    container_of(exp_hdl, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;
	uint32_t result =
	    exp->export.sub_export->exp_ops.fs_maxread(exp->export.sub_export);

	op_ctx->fsal_export = &exp->export;

	return result;
}

static uint32_t fs_maxwrite(struct fsal_export *exp_hdl)
{
	struct txnfs_fsal_export *exp =
	    container_of(exp_hdl, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;
	uint32_t result =
	    exp->export.sub_export->exp_ops.fs_maxwrite(exp->export.sub_export);

	op_ctx->fsal_export = &exp->export;

	return result;
}

static uint32_t fs_maxlink(struct fsal_export *exp_hdl)
{
	struct txnfs_fsal_export *exp =
	    container_of(exp_hdl, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;
	uint32_t result =
	    exp->export.sub_export->exp_ops.fs_maxlink(exp->export.sub_export);

	op_ctx->fsal_export = &exp->export;

	return result;
}

static uint32_t fs_maxnamelen(struct fsal_export *exp_hdl)
{
	struct txnfs_fsal_export *exp =
	    container_of(exp_hdl, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;
	uint32_t result = exp->export.sub_export->exp_ops.fs_maxnamelen(
	    exp->export.sub_export);
	op_ctx->fsal_export = &exp->export;

	return result;
}

static uint32_t fs_maxpathlen(struct fsal_export *exp_hdl)
{
	struct txnfs_fsal_export *exp =
	    container_of(exp_hdl, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;
	uint32_t result = exp->export.sub_export->exp_ops.fs_maxpathlen(
	    exp->export.sub_export);
	op_ctx->fsal_export = &exp->export;

	return result;
}

static fsal_aclsupp_t fs_acl_support(struct fsal_export *exp_hdl)
{
	struct txnfs_fsal_export *exp =
	    container_of(exp_hdl, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;
	fsal_aclsupp_t result = exp->export.sub_export->exp_ops.fs_acl_support(
	    exp->export.sub_export);
	op_ctx->fsal_export = &exp->export;

	return result;
}

static attrmask_t fs_supported_attrs(struct fsal_export *exp_hdl)
{
	struct txnfs_fsal_export *exp =
	    container_of(exp_hdl, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;
	attrmask_t result = exp->export.sub_export->exp_ops.fs_supported_attrs(
	    exp->export.sub_export);

	op_ctx->fsal_export = &exp->export;

	return result;
}

static uint32_t fs_umask(struct fsal_export *exp_hdl)
{
	struct txnfs_fsal_export *exp =
	    container_of(exp_hdl, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;
	uint32_t result =
	    exp->export.sub_export->exp_ops.fs_umask(exp->export.sub_export);

	op_ctx->fsal_export = &exp->export;

	return result;
}

/* get_quota
 * return quotas for this export.
 * path could cross a lower mount boundary which could
 * mask lower mount values with those of the export root
 * if this is a real issue, we can scan each time with setmntent()
 * better yet, compare st_dev of the file with st_dev of root_fd.
 * on linux, can map st_dev -> /proc/partitions name -> /dev/<name>
 */

static fsal_status_t get_quota(struct fsal_export *exp_hdl,
			       const char *filepath, int quota_type,
			       int quota_id, fsal_quota_t *pquota)
{
	struct txnfs_fsal_export *exp =
	    container_of(exp_hdl, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;
	fsal_status_t result = exp->export.sub_export->exp_ops.get_quota(
	    exp->export.sub_export, filepath, quota_type, quota_id, pquota);
	op_ctx->fsal_export = &exp->export;

	return result;
}

/* set_quota
 * same lower mount restriction applies
 */

static fsal_status_t set_quota(struct fsal_export *exp_hdl,
			       const char *filepath, int quota_type,
			       int quota_id, fsal_quota_t *pquota,
			       fsal_quota_t *presquota)
{
	struct txnfs_fsal_export *exp =
	    container_of(exp_hdl, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;
	fsal_status_t result = exp->export.sub_export->exp_ops.set_quota(
	    exp->export.sub_export, filepath, quota_type, quota_id, pquota,
	    presquota);
	op_ctx->fsal_export = &exp->export;

	return result;
}

static struct state_t *txnfs_alloc_state(struct fsal_export *exp_hdl,
					 enum state_type state_type,
					 struct state_t *related_state)
{
	struct txnfs_fsal_export *exp =
	    container_of(exp_hdl, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;
	state_t *state = exp->export.sub_export->exp_ops.alloc_state(
	    exp->export.sub_export, state_type, related_state);
	op_ctx->fsal_export = &exp->export;

	/* Replace stored export with ours so stacking works */
	state->state_exp = exp_hdl;

	return state;
}

static void txnfs_free_state(struct fsal_export *exp_hdl, struct state_t *state)
{
	struct txnfs_fsal_export *exp =
	    container_of(exp_hdl, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;
	exp->export.sub_export->exp_ops.free_state(exp->export.sub_export,
						   state);
	op_ctx->fsal_export = &exp->export;
}

static bool txnfs_is_superuser(struct fsal_export *exp_hdl,
			       const struct user_cred *creds)
{
	struct txnfs_fsal_export *exp =
	    container_of(exp_hdl, struct txnfs_fsal_export, export);
	bool rv;

	op_ctx->fsal_export = exp->export.sub_export;
	rv = exp->export.sub_export->exp_ops.is_superuser(
	    exp->export.sub_export, creds);
	op_ctx->fsal_export = &exp->export;

	return rv;
}

static fsal_status_t txnfs_host_to_key(struct fsal_export *exp_hdl,
				       struct gsh_buffdesc *fh_desc)
{
	UDBG;
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};
	return status;
}

/* extract a file handle from a buffer.
 * do verification checks and flag any and all suspicious bits.
 * Return an updated fh_desc into whatever was passed.  The most
 * common behavior, done here is to just reset the length.
 */

static fsal_status_t wire_to_host(struct fsal_export *exp_hdl,
				  fsal_digesttype_t in_type,
				  struct gsh_buffdesc *fh_desc, int flags)
{
	UDBG;
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};
	return status;
}

static void txnfs_prepare_unexport(struct fsal_export *exp_hdl)
{
	UDBG;
	struct txnfs_fsal_export *exp =
	    container_of(exp_hdl, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;
	exp->export.sub_export->exp_ops.prepare_unexport(
	    exp->export.sub_export);
	op_ctx->fsal_export = &exp->export;
}

/**
 * Sometimes txn_cache is not initialized before calling txnfs_end_compound
 * because the start_compound method of PSEUDOFS is called. This function is
 * intended to check if the txn-related context data has been properly
 * initialized. If not, we should not perform any txn-related operations.
 */
static inline bool txn_context_valid(void) { return (op_ctx->op_args != NULL); }

fsal_status_t txnfs_start_compound(struct fsal_export *exp_hdl, void *data)
{
	COMPOUND4args *args = data;
	fsal_status_t res = {ERR_FSAL_NO_ERROR, 0};
	struct txnfs_fsal_module *fs =
	    container_of(exp_hdl->fsal, struct txnfs_fsal_module, module);

	LogDebug(COMPONENT_FSAL, "Start Compound in FSAL_TXN layer.");
	LogDebug(COMPONENT_FSAL, "Compound operations: %d",
		 args->argarray.argarray_len);

	txnfs_tracepoint(init_start_compound, args->argarray.argarray_len);

	op_ctx->txnid = create_txn_log(fs->db, args);

	txnfs_tracepoint(create_txn_log, op_ctx->txnid);

	// initialize txn cache
	txnfs_cache_init();

	txnfs_tracepoint(init_txn_cache, op_ctx->txnid);

	op_ctx->op_args = args;

	struct txnfs_fsal_export *exp =
	    container_of(exp_hdl, struct txnfs_fsal_export, export);

	if (exp->export.sub_export->exp_ops.start_compound) {
		op_ctx->fsal_export = exp->export.sub_export;
		res = exp->export.sub_export->exp_ops.start_compound(
		    exp->export.sub_export, data);
		op_ctx->fsal_export = &exp->export;
	}

	txnfs_tracepoint(called_subfsal_start_compound, op_ctx->txnid,
			 exp->export.sub_export->fsal->name);
	return res;
}

fsal_status_t txnfs_end_compound(struct fsal_export *exp_hdl, void *data)
{
	COMPOUND4res *res = data;
	fsal_status_t ret = {ERR_FSAL_NO_ERROR, 0};

	struct txnfs_fsal_export *exp =
	    container_of(exp_hdl, struct txnfs_fsal_export, export);

	if (exp->export.sub_export->exp_ops.end_compound) {
		op_ctx->fsal_export = exp->export.sub_export;
		ret = exp->export.sub_export->exp_ops.end_compound(
		    exp->export.sub_export, data);
		op_ctx->fsal_export = &exp->export;
	}
	txnfs_tracepoint(called_subfsal_end_compound, op_ctx->txnid,
			 exp->export.sub_export->fsal->name);

	LogDebug(COMPONENT_FSAL, "End Compound in FSAL_TXN layer.");
	LogDebug(COMPONENT_FSAL, "Compound status: %d operations: %d",
		 res->status, res->resarray.resarray_len);

	/* If txn-related data has neven been properly initialized, don't do
	 * the following operations. */
	if (!txn_context_valid()) return ret;

	txnfs_tracepoint(init_end_compound, res->status, op_ctx->txnid);
	if (res->status == NFS4_OK) {
		// commit entries to leveldb and remove txnlog entry
		txnfs_cache_commit();
		txnfs_tracepoint(committed_txn_cache, op_ctx->txnid);
	} else if (res->status != NFS4_OK && op_ctx->txnid > 0) {
		int err = txnfs_compound_restore(op_ctx->txnid, res);
		if (err != 0) {
			LogWarn(COMPONENT_FSAL, "compound_restore error: %d",
				err);
		}
		txnfs_tracepoint(restored_compound, op_ctx->txnid, err);
		// remove txn log entry
	}

	// clear the list of entry in op_ctx->txn_cache
	txnfs_cache_cleanup();
	txnfs_tracepoint(cleaned_up_cache, op_ctx->txnid);
	submit_cleanup_task(exp, op_ctx->txnid, op_ctx->txn_bkp_folder);
	/* backup folder is per transaction, so we should clear this */
	op_ctx->txn_bkp_folder = NULL;
	txnfs_tracepoint(cleaned_up_backup, op_ctx->txnid);

	return ret;
}

static inline int get_open_filename(struct nfs_argop4 *op, char **out)
{
	return nfs4_utf8string2dynamic(
	    &op->nfs_argop4_u.opopen.claim.open_claim4_u.file, UTF8_SCAN_ALL,
	    out);
}

static inline int get_remove_filename(struct nfs_argop4 *op, char **out)
{
	return nfs4_utf8string2dynamic(&op->nfs_argop4_u.opremove.target,
				       UTF8_SCAN_ALL, out);
}

fsal_status_t txnfs_backup_nfs4_op(struct fsal_export *exp_hdl,
				   unsigned int opidx,
				   struct fsal_obj_handle *current,
				   struct nfs_argop4 *op)
{
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};
	struct fsal_obj_handle *handle = NULL;
	struct txnfs_fsal_obj_handle *cur_hdl =
	    container_of(current, struct txnfs_fsal_obj_handle, obj_handle);
	struct txnfs_fsal_export *exp =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);
	struct attrlist attrs = {0};
	char *pathname = NULL;
	nfsstat4 ret;

	if (exp->export.sub_export->exp_ops.backup_nfs4_op) {
		op_ctx->fsal_export = exp->export.sub_export;
		status = exp->export.sub_export->exp_ops.backup_nfs4_op(
		    exp->export.sub_export, opidx, cur_hdl->sub_handle, op);
		op_ctx->fsal_export = &exp->export;
	}

	/* Do not backup if txn-related data has not been initialized or
	 * this compound is not eligible for transaction */
	if (!txn_context_valid() || op_ctx->txnid == 0) return status;

	switch (op->argop) {
		/**
		 * Do we really need to backup on OPEN operation?
		 * If a new file is created, then it would NOT exist before
		 * anyway.
		 *
		 * >> We do need this since OPEN_CREATE may truncate the
		 * existing file if createattrs->filesize is 0. However let's
		 * narrow down the condition in the next PR.
		 */
		case NFS4_OP_OPEN:
			// lookup first
			txnfs_tracepoint(
			    backup_open, op_ctx->txnid,
			    op->nfs_argop4_u.opopen.openhow.opentype &
				OPEN4_CREATE);

			/* We don't need to check opopen.openhow because
			 * create_txn_log has already done so. If OPEN is not
			 * accompanied with creation or truncation, txnid
			 * will be zero and the program will not reach here */
			ret = get_open_filename(op, &pathname);
			if (ret != NFS4_OK) {
				LogFatal(COMPONENT_FSAL,
					 "utf8 conversion failed. "
					 "state=%d",
					 ret);
			}
			op_ctx->fsal_export = exp->export.sub_export;
			status = cur_hdl->sub_handle->obj_ops->lookup(
			    cur_hdl->sub_handle, pathname, &handle, &attrs);
			op_ctx->fsal_export = &exp->export;

			txnfs_tracepoint(done_lookup, status.major, pathname,
					 attrs.filesize);

			if (status.major == ERR_FSAL_NO_ERROR) {
				txnfs_backup_file(opidx, handle);
			} else if (status.major != ERR_FSAL_NOENT) {
				LogFatal(COMPONENT_FSAL,
					 "lookup failed: (%d, %d)",
					 status.major, status.minor);
			}
			break;

		case NFS4_OP_WRITE:
			// TODO: check handle in db
			txnfs_tracepoint(
			    backup_write, op_ctx->txnid,
			    op->nfs_argop4_u.opwrite.offset,
			    op->nfs_argop4_u.opwrite.data.data_len);
			txnfs_backup_file(opidx, cur_hdl->sub_handle);
			break;
		case NFS4_OP_REMOVE:
			// lookup first
			txnfs_tracepoint(backup_remove, op_ctx->txnid);
			ret = get_remove_filename(op, &pathname);
			if (ret != NFS4_OK) {
				LogFatal(COMPONENT_FSAL,
					 "utf8 conversion failed. "
					 "status=%d",
					 ret);
				break;
			}
			op_ctx->fsal_export = exp->export.sub_export;
			status = cur_hdl->sub_handle->obj_ops->lookup(
			    cur_hdl->sub_handle, pathname, &handle, &attrs);
			op_ctx->fsal_export = &exp->export;
			txnfs_tracepoint(done_lookup, status.major, pathname,
					 attrs.filesize);
			free(pathname);

			if (status.major == ERR_FSAL_NO_ERROR) {
				txnfs_backup_file(opidx, handle);
			} else if (status.major != ERR_FSAL_NOENT) {
				assert(!"lookup failure!");
			}
		default:
			return status;
	}

	return status;
}
/* txnfs_export_ops_init
 * overwrite vector entries with the methods that we support
 */

void txnfs_export_ops_init(struct export_ops *ops)
{
	ops->release = release;
	ops->prepare_unexport = txnfs_prepare_unexport;
	ops->lookup_path = txnfs_lookup_path;
	ops->wire_to_host = wire_to_host;
	ops->host_to_key = txnfs_host_to_key;
	ops->create_handle = txnfs_create_handle;
	ops->get_fs_dynamic_info = get_dynamic_info;
	ops->fs_supports = fs_supports;
	ops->fs_maxfilesize = fs_maxfilesize;
	ops->fs_maxread = fs_maxread;
	ops->fs_maxwrite = fs_maxwrite;
	ops->fs_maxlink = fs_maxlink;
	ops->fs_maxnamelen = fs_maxnamelen;
	ops->fs_maxpathlen = fs_maxpathlen;
	ops->fs_acl_support = fs_acl_support;
	ops->fs_supported_attrs = fs_supported_attrs;
	ops->fs_umask = fs_umask;
	ops->get_quota = get_quota;
	ops->set_quota = set_quota;
	ops->alloc_state = txnfs_alloc_state;
	ops->free_state = txnfs_free_state;
	ops->is_superuser = txnfs_is_superuser;

	/* compound start and end */
	ops->start_compound = txnfs_start_compound;
	ops->end_compound = txnfs_end_compound;
	ops->backup_nfs4_op = txnfs_backup_nfs4_op;
}

struct txnfsal_args {
	struct subfsal_args subfsal;
};

static struct config_item sub_fsal_params[] = {
    CONF_ITEM_STR("name", 1, 10, NULL, subfsal_args, name), CONFIG_EOL};

static struct config_item export_params[] = {
    CONF_ITEM_NOOP("name"),
    CONF_RELAX_BLOCK("FSAL", sub_fsal_params, noop_conf_init, subfsal_commit,
		     txnfsal_args, subfsal),
    CONFIG_EOL};

static struct config_block export_param = {
    .dbus_interface_name = "org.ganesha.nfsd.config.fsal.txnfs-export%d",
    .blk_desc.name = "FSAL",
    .blk_desc.type = CONFIG_BLOCK,
    .blk_desc.u.blk.init = noop_conf_init,
    .blk_desc.u.blk.params = export_params,
    .blk_desc.u.blk.commit = noop_conf_commit};

/* create_export
 * Create an export point and return a handle to it to be kept
 * in the export list.
 * First lookup the fsal, then create the export and then put the fsal back.
 * returns the export with one reference taken.
 */

fsal_status_t txnfs_create_export(struct fsal_module *fsal_hdl,
				  void *parse_node,
				  struct config_error_type *err_type,
				  const struct fsal_up_vector *up_ops)
{
	fsal_status_t expres;
	struct fsal_module *fsal_stack;
	struct txnfs_fsal_export *myself;
	struct txnfsal_args txnfsal;
	int retval;
	UDBG;

	/* process our FSAL block to get the name of the fsal
	 * underneath us.
	 */
	retval = load_config_from_node(parse_node, &export_param, &txnfsal,
				       true, err_type);
	if (retval != 0) return fsalstat(ERR_FSAL_INVAL, 0);
	fsal_stack = lookup_fsal(txnfsal.subfsal.name);
	if (fsal_stack == NULL) {
		LogMajor(COMPONENT_FSAL,
			 "txnfs create export failed to lookup for FSAL %s",
			 txnfsal.subfsal.name);
		return fsalstat(ERR_FSAL_INVAL, EINVAL);
	}

	myself = gsh_calloc(1, sizeof(struct txnfs_fsal_export));
	expres = fsal_stack->m_ops.create_export(
	    fsal_stack, txnfsal.subfsal.fsal_node, err_type, up_ops);
	fsal_put(fsal_stack);

	LogFullDebug(COMPONENT_FSAL, "FSAL %s refcount %" PRIu32,
		     fsal_stack->name,
		     atomic_fetch_int32_t(&fsal_stack->refcount));

	if (FSAL_IS_ERROR(expres)) {
		LogMajor(COMPONENT_FSAL,
			 "Failed to call create_export on underlying FSAL %s",
			 txnfsal.subfsal.name);
		gsh_free(myself);
		return expres;
	}

	fsal_export_stack(op_ctx->fsal_export, &myself->export);

	fsal_export_init(&myself->export);
	txnfs_export_ops_init(&myself->export.exp_ops);
#ifdef EXPORT_OPS_INIT
	/*** FIX ME!!!
	 * Need to iterate through the lists to save and restore.
	 */
	txnfs_handle_ops_init(myself->export.obj_ops);
#endif /* EXPORT_OPS_INIT */
	myself->export.up_ops = up_ops;
	myself->export.fsal = fsal_hdl;
	myself->root = NULL;
	myself->bkproot = NULL;
	op_ctx->txn_bkp_folder = NULL;

	/* lock myself before attaching to the fsal.
	 * keep myself locked until done with creating myself.
	 */
	op_ctx->fsal_export = &myself->export;

	get_txn_root(&myself->root, NULL);
	init_backup_worker(myself);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
