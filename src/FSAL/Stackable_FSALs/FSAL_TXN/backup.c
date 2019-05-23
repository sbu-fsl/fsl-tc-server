#include "txnfs_methods.h"
#include <assert.h>

void txnfs_create_backup_dir()
{
	// create txn backup directory
	 struct fsal_obj_handle* root_entry = NULL, *txnroot = NULL;
	 struct attrlist attrs;
	 struct attrlist attrs_out;
	 fsal_status_t status = op_ctx->fsal_export->exp_ops.lookup_path(op_ctx->fsal_export, op_ctx->ctx_export->fullpath, &root_entry, &attrs);
	 assert(status.major == 0);
	 assert(root_entry);
	  
	 FSAL_SET_MASK(attrs.valid_mask, ATTR_MODE | ATTR_OWNER | ATTR_GROUP);
	 attrs.mode = 0777; 
	 attrs.owner = 667;
	 attrs.group = 766;
	 fsal_prepare_attrs(&attrs_out, 0);

	status = fsal_create(root_entry, TXN_BKP_DIR, DIRECTORY,
			       &attrs, NULL, &txnroot, &attrs_out);
	assert(status.major == 0);
	assert(txnroot);
	fsal_release_attrs(&attrs_out); 

}
int txnfs_compound_backup(uint64_t txnid, COMPOUND4args* args) {
  UDBG;	
  // assert backup dir exists
  // create directory with uuid as name in backup dir
  // for each open / write / unlink / rename compound operation take backup
	return 0;
}

int txnfs_compound_restore(uint64_t txnid, COMPOUND4res* res) {
  UDBG;	
  // assert backup dir exists
  // assert txn backup dir exists
  //
	return 0;
}
