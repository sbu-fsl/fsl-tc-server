/* export.c
 * NULL FSAL export object
 */

#include "config.h"

#include "fsal.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <os/mntent.h>
#include <os/quota.h>
#include <dlfcn.h>
#include "gsh_list.h"
#include "config_parsing.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "txnfs_methods.h"
#include "nfs_exports.h"
#include "export_mgr.h"

/* helpers to/from other NULL objects
 */

struct fsal_staticfsinfo_t *txnfs_staticinfo(struct fsal_module *hdl);

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

	fsal_detach_export(exp_hdl->fsal, &exp_hdl->exports);
	free_export_ops(exp_hdl);

	gsh_free(myself);	/* elvis has left the building */
}

static fsal_status_t get_dynamic_info(struct fsal_export *exp_hdl,
				      struct fsal_obj_handle *obj_hdl,
				      fsal_dynamicfsinfo_t *infop)
{
	struct txnfs_fsal_export *exp =
		container_of(exp_hdl, struct txnfs_fsal_export, export);

	struct txnfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct txnfs_fsal_obj_handle,
			     obj_handle);

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
	bool result =
		exp->export.sub_export->exp_ops.fs_supports(
				exp->export.sub_export, option);

	op_ctx->fsal_export = &exp->export;

	return result;
}

static uint64_t fs_maxfilesize(struct fsal_export *exp_hdl)
{
	struct txnfs_fsal_export *exp =
		container_of(exp_hdl, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;
	uint64_t result =
		exp->export.sub_export->exp_ops.fs_maxfilesize(
				exp->export.sub_export);
	op_ctx->fsal_export = &exp->export;

	return result;
}

static uint32_t fs_maxread(struct fsal_export *exp_hdl)
{
	struct txnfs_fsal_export *exp =
		container_of(exp_hdl, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;
	uint32_t result = exp->export.sub_export->exp_ops.fs_maxread(
				exp->export.sub_export);

	op_ctx->fsal_export = &exp->export;

	return result;
}

static uint32_t fs_maxwrite(struct fsal_export *exp_hdl)
{
	struct txnfs_fsal_export *exp =
		container_of(exp_hdl, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;
	uint32_t result = exp->export.sub_export->exp_ops.fs_maxwrite(
				exp->export.sub_export);

	op_ctx->fsal_export = &exp->export;

	return result;
}

static uint32_t fs_maxlink(struct fsal_export *exp_hdl)
{
	struct txnfs_fsal_export *exp =
		container_of(exp_hdl, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;
	uint32_t result = exp->export.sub_export->exp_ops.fs_maxlink(
				exp->export.sub_export);

	op_ctx->fsal_export = &exp->export;

	return result;
}

static uint32_t fs_maxnamelen(struct fsal_export *exp_hdl)
{
	struct txnfs_fsal_export *exp =
		container_of(exp_hdl, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;
	uint32_t result =
		exp->export.sub_export->exp_ops.fs_maxnamelen(
				exp->export.sub_export);
	op_ctx->fsal_export = &exp->export;

	return result;
}

static uint32_t fs_maxpathlen(struct fsal_export *exp_hdl)
{
	struct txnfs_fsal_export *exp =
		container_of(exp_hdl, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;
	uint32_t result =
		exp->export.sub_export->exp_ops.fs_maxpathlen(
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
	attrmask_t result =
		exp->export.sub_export->exp_ops.fs_supported_attrs(
		exp->export.sub_export);
	op_ctx->fsal_export = &exp->export;

	return result;
}

static uint32_t fs_umask(struct fsal_export *exp_hdl)
{
	struct txnfs_fsal_export *exp =
		container_of(exp_hdl, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;
	uint32_t result = exp->export.sub_export->exp_ops.fs_umask(
				exp->export.sub_export);

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
			       int quota_id,
			       fsal_quota_t *pquota)
{
	struct txnfs_fsal_export *exp =
		container_of(exp_hdl, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;
	fsal_status_t result =
		exp->export.sub_export->exp_ops.get_quota(
			exp->export.sub_export, filepath,
			quota_type, quota_id, pquota);
	op_ctx->fsal_export = &exp->export;

	return result;
}

/* set_quota
 * same lower mount restriction applies
 */

static fsal_status_t set_quota(struct fsal_export *exp_hdl,
			       const char *filepath, int quota_type,
			       int quota_id,
			       fsal_quota_t *pquota, fsal_quota_t *presquota)
{
	struct txnfs_fsal_export *exp =
		container_of(exp_hdl, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;
	fsal_status_t result =
		exp->export.sub_export->exp_ops.set_quota(
			exp->export.sub_export, filepath, quota_type, quota_id,
			pquota, presquota);
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
	state_t *state =
		exp->export.sub_export->exp_ops.alloc_state(
			exp->export.sub_export, state_type, related_state);
	op_ctx->fsal_export = &exp->export;

	/* Replace stored export with ours so stacking works */
	state->state_exp = exp_hdl;

	return state;
}

static void txnfs_free_state(struct fsal_export *exp_hdl,
			      struct state_t *state)
{
	struct txnfs_fsal_export *exp = container_of(exp_hdl,
					struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;
	exp->export.sub_export->exp_ops.free_state(exp->export.sub_export,
						   state);
	op_ctx->fsal_export = &exp->export;
}


/* extract a file handle from a buffer.
 * do verification checks and flag any and all suspicious bits.
 * Return an updated fh_desc into whatever was passed.  The most
 * common behavior, done here is to just reset the length.  There
 * is the option to also adjust the start pointer.
 */

static fsal_status_t wire_to_host(struct fsal_export *exp_hdl,
				    fsal_digesttype_t in_type,
				    struct gsh_buffdesc *fh_desc,
				    int flags)
{
	/*struct txnfs_fsal_export *exp =
		container_of(exp_hdl, struct txnfs_fsal_export, export);*/
	db_kvpair_t kvpair;

	if (fh_desc->len < TXN_UUID_LEN) {
		LogMajor(COMPONENT_FSAL, "File handle too short");
		return fsalstat(ERR_FSAL_BADHANDLE, 0);
	}

	fh_desc->len = TXN_UUID_LEN;
	if (memcmp(fh_desc->addr, get_root_id(db), TXN_UUID_LEN) == 0) {
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	LogDebug(COMPONENT_FSAL, "Received file ID %s. ID Length = %zu",
		(char *) fh_desc->addr, fh_desc->len);
	kvpair.key = (char *) fh_desc->addr;
	kvpair.key_len = TXN_UUID_LEN;
	kvpair.val = NULL;
	int res = get_keys(&kvpair, 1, db);
	LogDebug(COMPONENT_FSAL, "get_keys = %d", res);
	if (kvpair.val == NULL) {
		LogMajor(COMPONENT_FSAL, "No entry in DB for file ID %s", kvpair.key);
		return fsalstat(ERR_FSAL_BADHANDLE, 0);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t txnfs_host_to_key(struct fsal_export *exp_hdl,
				       struct gsh_buffdesc *fh_desc)
{
	struct txnfs_fsal_export *exp =
	    container_of(exp_hdl, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;
	fsal_status_t result =
		exp->export.sub_export->exp_ops.host_to_key(
			exp->export.sub_export, fh_desc);
	op_ctx->fsal_export = &exp->export;

	return result;
}


/* txnfs_export_ops_init
 * overwrite vector entries with the methods that we support
 */

void txnfs_export_ops_init(struct export_ops *ops)
{
	ops->release = release;
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
}

struct txnfsal_args {
	struct subfsal_args subfsal;
};

static struct config_item sub_fsal_params[] = {
	CONF_ITEM_STR("name", 1, 10, NULL,
		      subfsal_args, name),
	CONFIG_EOL
};

static struct config_item export_params[] = {
	CONF_ITEM_NOOP("name"),
	CONF_RELAX_BLOCK("FSAL", sub_fsal_params,
			 noop_conf_init, subfsal_commit,
			 txnfsal_args, subfsal),
	CONFIG_EOL
};

static struct config_block export_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.txnfs-export%d",
	.blk_desc.name = "FSAL",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = export_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

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

	/* process our FSAL block to get the name of the fsal
	 * underneath us.
	 */
	retval = load_config_from_node(parse_node,
				       &export_param,
				       &txnfsal,
				       true,
				       err_type);
	if (retval != 0)
		return fsalstat(ERR_FSAL_INVAL, 0);
	fsal_stack = lookup_fsal(txnfsal.subfsal.name);
	if (fsal_stack == NULL) {
		LogMajor(COMPONENT_FSAL,
			 "txnfs_create_export: failed to lookup for FSAL %s",
			 txnfsal.subfsal.name);
		return fsalstat(ERR_FSAL_INVAL, EINVAL);
	}

	myself = gsh_calloc(1, sizeof(struct txnfs_fsal_export));
	expres = fsal_stack->m_ops.create_export(fsal_stack,
						 txnfsal.subfsal.fsal_node,
						 err_type,
						 up_ops);
	fsal_put(fsal_stack);
	if (FSAL_IS_ERROR(expres)) {
		LogMajor(COMPONENT_FSAL,
			 "Failed to call create_export on underlying FSAL %s",
			 txnfsal.subfsal.name);
		gsh_free(myself);
		return expres;
	}

	fsal_export_stack(op_ctx->fsal_export, &myself->export);

	/* Init next_ops structure */
	/*** FIX ME!!!
	 * This structure had 3 mallocs that were never freed,
	 * and would leak for every export created.
	 * Now static to avoid the leak, the saved contents were
	 * never restored back to the original.
	 */

	memcpy(&next_ops.exp_ops,
	       &myself->export.sub_export->exp_ops,
	       sizeof(struct export_ops));
#ifdef EXPORT_OPS_INIT
	/*** FIX ME!!!
	 * Need to iterate through the lists to save and restore.
	 */
	memcpy(&next_ops.obj_ops,
	       myself->export.sub_export->obj_ops,
	       sizeof(struct fsal_obj_ops));
	memcpy(&next_ops.dsh_ops,
	       myself->export.sub_export->dsh_ops,
	       sizeof(struct fsal_dsh_ops));
#endif				/* EXPORT_OPS_INIT */
	next_ops.up_ops = up_ops;

	fsal_export_init(&myself->export);
	txnfs_export_ops_init(&myself->export.exp_ops);
#ifdef EXPORT_OPS_INIT
	/*** FIX ME!!!
	 * Need to iterate through the lists to save and restore.
	 */
	txnfs_handle_ops_init(myself->export.obj_ops);
#endif				/* EXPORT_OPS_INIT */
	myself->export.up_ops = up_ops;
	myself->export.fsal = fsal_hdl;

	/* lock myself before attaching to the fsal.
	 * keep myself locked until done with creating myself.
	 */
	op_ctx->fsal_export = &myself->export;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}