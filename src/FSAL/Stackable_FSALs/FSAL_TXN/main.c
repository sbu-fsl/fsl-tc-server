/* main.c
 * Module core functions
 */

#include "config.h"

#include "fsal.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include "gsh_list.h"
#include "FSAL/fsal_init.h"
#include "config_parsing.h"
#include "txnfs_methods.h"


/* FSAL name determines name of shared library: libfsal<name>.so */
const char myname[] = "TXNFS";

/* my module private storage
 */

struct txnfs_fsal_module TXNFS = {
	.module = {
		.fs_info = {
			.maxfilesize = UINT64_MAX,
			.maxlink = _POSIX_LINK_MAX,
			.maxnamelen = 1024,
			.maxpathlen = 1024,
			.no_trunc = true,
			.chown_restricted = true,
			.case_insensitive = false,
			.case_preserving = true,
			.link_support = true,
			.symlink_support = true,
			.lock_support = true,
			.lock_support_async_block = false,
			.named_attr = true,
			.unique_handles = true,
			.acl_support = FSAL_ACLSUPPORT_ALLOW,
			.cansettime = true,
			.homogenous = true,
			.supported_attrs = ALL_ATTRIBUTES,
			.maxread = FSAL_MAXIOSIZE,
			.maxwrite = FSAL_MAXIOSIZE,
			.umask = 0,
			.auth_exportpath_xdev = false,
			.link_supports_permission_checks = true,
		}
	}
};

static struct config_item txn_items[] = {
	CONF_MAND_PATH("DbPath", 1, MAXPATHLEN, "/tmp/txndb",
		       txnfs_fsal_module, db_path),
	CONFIG_EOL
};

static struct config_block txn_block = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.txn",
	.blk_desc.name = "TXNFS",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = txn_items,
	.blk_desc.u.blk.commit = noop_conf_commit
};

/* Module methods
 */

/* init_config
 * must be called with a reference taken (via lookup_fsal)
 */

static fsal_status_t init_config(struct fsal_module *fsal_module,
				 config_file_t config_struct,
				 struct config_error_type *err_type)
{
	/* Configuration setting options:
	 * 1. there are none that are changable. (this case)
	 *
	 * 2. we set some here.  These must be independent of whatever
	 *    may be set by lower level fsals.
	 *
	 * If there is any filtering or change of parameters in the stack,
	 * this must be done in export data structures, not fsal params because
	 * a stackable could be configured above multiple fsals for multiple
	 * diverse exports.
	 */

  UDBG;
	LogDebug(COMPONENT_FSAL,
		 "FSAL_TXN INIT: Supported attributes mask = 0x%" PRIx64,
		 fsal_module->fs_info.supported_attrs);
	struct txnfs_fsal_module *txnfs_module =
	    container_of(fsal_module, struct txnfs_fsal_module, module);
	/* if we have fsal specific params, do them here
	 * fsal_hdl->name is used to find the block containing the
	 * params.
	 */
  int found = load_config_from_parse(config_struct,
				      &txn_block,
				      txnfs_module,
				      true,
				      err_type);
	if (!config_error_is_harmless(err_type))
		return fsalstat(ERR_FSAL_INVAL, 0);
	
  display_fsinfo(&txnfs_module->module);
	LogDebug(COMPONENT_FSAL,"dump_config found: %d db_path: %s", found, txnfs_module->db_path);
	lm = new_lock_manager();
  db = init_db_store(txnfs_module->db_path, true);
  assert(db != NULL);
  assert(initialize_id_manager(db) == 0);
  
  return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* Internal TXNFS method linkage to export object
 */

fsal_status_t txnfs_create_export(struct fsal_module *fsal_hdl,
				   void *parse_node,
				   struct config_error_type *err_type,
				   const struct fsal_up_vector *up_ops);

/* Module initialization.
 * Called by dlopen() to register the module
 * keep a private pointer to me in myself
 */

/* linkage to the exports and handle ops initializers
 */
MODULE_INIT void txnfs_init(void)
{
  UDBG;
	int retval;
	struct fsal_module *myself = &TXNFS.module;

	retval = register_fsal(myself, myname, FSAL_MAJOR_VERSION,
			       FSAL_MINOR_VERSION, FSAL_ID_NO_PNFS);
	if (retval != 0) {
		fprintf(stderr, "TXNFS module failed to register");
		return;
	}
	myself->m_ops.create_export = txnfs_create_export;
	myself->m_ops.init_config = init_config;

	/* Initialize the fsal_obj_handle ops for FSAL NULL */
	txnfs_handle_ops_init(&TXNFS.handle_ops);
}

MODULE_FINI void txnfs_unload(void)
{
	int retval;
  UDBG;

	retval = unregister_fsal(&TXNFS.module);
	if (retval != 0) {
		fprintf(stderr, "TXNFS module failed to unregister");
		return;
	}
}
