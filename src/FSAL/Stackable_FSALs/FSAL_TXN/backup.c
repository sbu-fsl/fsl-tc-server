#include "txnfs_methods.h"
#include <assert.h>

fsal_status_t txnfs_create_or_lookup_backup_dir(struct fsal_obj_handle** bkp_handle)
{
  UDBG;
	// create txn backup directory
	struct fsal_obj_handle* root_entry = NULL;
	struct fsal_obj_handle* txn_handle = NULL;
	struct attrlist attrs;
  memset(&attrs, 0, sizeof(struct attrlist));
  char txnid[20];
  sprintf(txnid, "%lu", op_ctx->txnid);
	
  fsal_status_t status = op_ctx->fsal_export->exp_ops.lookup_path(op_ctx->fsal_export, op_ctx->ctx_export->fullpath, &root_entry, &attrs);
	assert(status.major == 0);
	struct txnfs_fsal_export *exp =
		container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;

	struct txnfs_fsal_obj_handle *txn_root_entry =
		container_of(root_entry, struct txnfs_fsal_obj_handle,
			     obj_handle);
	assert(txn_root_entry);

	FSAL_CLEAR_MASK(attrs.valid_mask); 
	FSAL_SET_MASK(attrs.valid_mask, ATTR_MODE | ATTR_OWNER | ATTR_GROUP);
	attrs.mode = 0666; 
	attrs.owner = 0;
	attrs.group = 0;
  
  // check if txn backup root exists
  status = fsal_lookup(txn_root_entry->sub_handle, TXN_BKP_DIR, &txn_handle, NULL);
  
  if (status.major == ERR_FSAL_NOENT)
  {
    // create txn backup root 
    status = fsal_create(txn_root_entry->sub_handle, TXN_BKP_DIR, DIRECTORY,
               &attrs, NULL, &txn_handle, NULL);
    assert(status.major == 0);
    assert(txn_handle);
  }
  
  status = fsal_lookup(txn_handle, txnid, bkp_handle, NULL);
  
  if (status.major == ERR_FSAL_NOENT)
  {
    // create txnid directory
    status = fsal_create(txn_handle, txnid, DIRECTORY,
               &attrs, NULL, bkp_handle, NULL);
    assert(status.major == 0);
    assert(*bkp_handle);
  }
	
  op_ctx->fsal_export = &exp->export;
  
  return status;
}

fsal_status_t txnfs_backup_file(unsigned int opidx, struct fsal_obj_handle *src_hdl)
{
  UDBG;
	struct fsal_obj_handle *dst_hdl = NULL;
	struct fsal_obj_handle *root_entry = NULL;
  uint64_t copied = 0;
	struct attrlist attrs;
  struct attrlist attrs_out;
  char backup_name[20];
	
  struct txnfs_fsal_obj_handle *txn_src_hdl =
		container_of(src_hdl, struct txnfs_fsal_obj_handle,
			     obj_handle);
	assert(txn_src_hdl);
  
  fsal_status_t status = txnfs_create_or_lookup_backup_dir(&root_entry);
	assert(status.major == 0);
	
  struct txnfs_fsal_export *exp =
		container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	op_ctx->fsal_export = exp->export.sub_export;
  
  // get file size
  attrs_out.request_mask = ATTR_SIZE;
	status = get_optional_attrs(txn_src_hdl->sub_handle, &attrs_out);
  assert(status.major == 0);

  // copy src to backup dir
  if (attrs_out.filesize > 0)
  {
    // create dst_handle
    sprintf(backup_name, "%d.bkp", opidx);
    FSAL_CLEAR_MASK(attrs.valid_mask); 
    FSAL_SET_MASK(attrs.valid_mask, ATTR_MODE | ATTR_OWNER | ATTR_GROUP);
    attrs.mode = 0666; 
    attrs.owner = 0;
    attrs.group = 0;
    status = fsal_create(root_entry, backup_name, REGULAR_FILE, &attrs, NULL, &dst_hdl, NULL);
    assert(status.major == 0);

    status = fsal_copy(txn_src_hdl->sub_handle, 0 /* src_offset */, dst_hdl, 0 /* dst_offset */, attrs_out.filesize, &copied);
    assert(status.major == 0);
  }
  op_ctx->fsal_export = &exp->export;
  return status;
}

int txnfs_compound_restore(uint64_t txnid, COMPOUND4res* res) {
  UDBG;	
  // assert backup dir exists
  // assert txn backup dir exists
  //
	return 0;
}
