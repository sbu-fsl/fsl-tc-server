/* xattrs.c
 * NULL object (file|dir) handle object extended attributes
 */

#include "config.h"

#include "FSAL/fsal_commonlib.h"
#include "fsal.h"
#include "fsal_convert.h"
#include "gsh_list.h"
#include "os/xattr.h"
#include "txnfs_methods.h"
#include <ctype.h>
#include <libgen.h> /* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>

fsal_status_t txnfs_list_ext_attrs(struct fsal_obj_handle *obj_hdl,
				   unsigned int argcookie,
				   fsal_xattrent_t *xattrs_tab,
				   unsigned int xattrs_tabsize,
				   unsigned int *p_nb_returned,
				   int *end_of_list)
{
	struct txnfs_fsal_obj_handle *handle =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status = handle->sub_handle->obj_ops->list_ext_attrs(
	    handle->sub_handle, argcookie, xattrs_tab, xattrs_tabsize,
	    p_nb_returned, end_of_list);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t txnfs_getextattr_id_by_name(struct fsal_obj_handle *obj_hdl,
					  const char *xattr_name,
					  unsigned int *pxattr_id)
{
	struct txnfs_fsal_obj_handle *handle =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
	    handle->sub_handle->obj_ops->getextattr_id_by_name(
		handle->sub_handle, xattr_name, pxattr_id);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t txnfs_getextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					   unsigned int xattr_id,
					   void *buffer_addr,
					   size_t buffer_size,
					   size_t *p_output_size)
{
	struct txnfs_fsal_obj_handle *handle =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
	    handle->sub_handle->obj_ops->getextattr_value_by_id(
		handle->sub_handle, xattr_id, buffer_addr, buffer_size,
		p_output_size);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t txnfs_getextattr_value_by_name(struct fsal_obj_handle *obj_hdl,
					     const char *xattr_name,
					     void *buffer_addr,
					     size_t buffer_size,
					     size_t *p_output_size)
{
	struct txnfs_fsal_obj_handle *handle =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
	    handle->sub_handle->obj_ops->getextattr_value_by_name(
		handle->sub_handle, xattr_name, buffer_addr, buffer_size,
		p_output_size);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t txnfs_setextattr_value(struct fsal_obj_handle *obj_hdl,
				     const char *xattr_name, void *buffer_addr,
				     size_t buffer_size, int create)
{
	struct txnfs_fsal_obj_handle *handle =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status = handle->sub_handle->obj_ops->setextattr_value(
	    handle->sub_handle, xattr_name, buffer_addr, buffer_size, create);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t txnfs_setextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					   unsigned int xattr_id,
					   void *buffer_addr,
					   size_t buffer_size)
{
	struct txnfs_fsal_obj_handle *handle =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
	    handle->sub_handle->obj_ops->setextattr_value_by_id(
		handle->sub_handle, xattr_id, buffer_addr, buffer_size);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t txnfs_remove_extattr_by_id(struct fsal_obj_handle *obj_hdl,
					 unsigned int xattr_id)
{
	struct txnfs_fsal_obj_handle *handle =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
	    handle->sub_handle->obj_ops->remove_extattr_by_id(
		handle->sub_handle, xattr_id);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t txnfs_remove_extattr_by_name(struct fsal_obj_handle *obj_hdl,
					   const char *xattr_name)
{
	struct txnfs_fsal_obj_handle *handle =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
	    handle->sub_handle->obj_ops->remove_extattr_by_name(
		handle->sub_handle, xattr_name);
	op_ctx->fsal_export = &export->export;

	return status;
}
