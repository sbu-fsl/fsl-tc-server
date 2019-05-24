#include "txnfs_methods.h"
#include <assert.h>

fsal_status_t txnfs_create_or_lookup_backup_dir(struct fsal_obj_handle** bkp_handle)
{
	// create txn backup directory
	struct fsal_obj_handle* root_entry = NULL;
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
  
  status = fsal_lookup(root_entry, TXN_BKP_DIR, bkp_handle, NULL);
  
  if (!(*bkp_handle))
  {
    status = fsal_create(root_entry, TXN_BKP_DIR, DIRECTORY,
               &attrs, NULL, bkp_handle, &attrs_out);
    assert(status.major == 0);
    assert(bkp_handle);
  }
  fsal_release_attrs(&attrs_out); 

  return status;
}

fsal_status_t txnfs_backup_file(unsigned int opidx, struct fsal_obj_handle *src_hdl)
{
	struct fsal_obj_handle *dst_hdl = NULL;
	struct fsal_obj_handle *root_entry = NULL;
  uint64_t copied = 0;
	struct attrlist attrs;
  struct attrlist attrs_out;
  char backup_name[20];
  
  fsal_status_t status = txnfs_create_or_lookup_backup_dir(&root_entry);
	assert(status.major == 0);
  
  // create dst_handle
  sprintf(backup_name, "%d.bkp", opidx);
	FSAL_SET_MASK(attrs.valid_mask, ATTR_MODE | ATTR_OWNER | ATTR_GROUP);
	attrs.mode = 0777; 
	attrs.owner = 667;
	attrs.group = 766;
  status = fsal_create(root_entry, backup_name, REGULAR_FILE, &attrs, NULL, &dst_hdl, NULL);
	assert(status.major == 0);

  // get file size
  attrs_out.request_mask = ATTR_SIZE;
  status = get_optional_attrs(src_hdl, &attrs_out);
  assert(status.major == 0);

  // copy src to backup dir
  status = fsal_copy(src_hdl, 0 /* src_offset */, dst_hdl, 0 /* dst_offset */, attrs_out.filesize, &copied);
  assert(status.major == 0);

  return status;
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
