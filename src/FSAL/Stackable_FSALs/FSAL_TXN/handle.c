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

/* handle.c
 */

#include "config.h"

#include "FSAL/fsal_commonlib.h"
#include "fsal.h"
#include "fsal_convert.h"
#include "gsh_list.h"
#include "nfs4_acls.h"
#include "txnfs_methods.h"
#include <libgen.h> /* used for 'dirname' */
#include <os/subr.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>

/* helpers
 */

/* handle methods
 */

/**
 * Allocate and initialize a new txnfs handle.
 *
 * This function doesn't free the sub_handle if the allocation fails. It must
 * be done in the calling function.
 *
 * @param[in] export The txnfs export used by the handle.
 * @param[in] sub_handle The handle used by the subfsal.
 * @param[in] fs The filesystem of the new handle.
 *
 * @return The new handle, or NULL if the allocation failed.
 */
static struct txnfs_fsal_obj_handle *txnfs_alloc_handle(
    struct txnfs_fsal_export *export, struct fsal_obj_handle *sub_handle,
    struct fsal_filesystem *fs, uuid_t uuid)
{
	struct txnfs_fsal_obj_handle *result;
	UDBG;

	result = gsh_calloc(1, sizeof(struct txnfs_fsal_obj_handle));

	/* default handlers */
	fsal_obj_handle_init(&result->obj_handle, &export->export,
			     sub_handle->type);
	/* txnfs handlers */
	result->obj_handle.obj_ops = &TXNFS.handle_ops;
	result->sub_handle = sub_handle;
	result->obj_handle.type = sub_handle->type;
	result->obj_handle.fsid = sub_handle->fsid;
	result->obj_handle.fileid = sub_handle->fileid;
	result->obj_handle.fs = fs;
	result->obj_handle.state_hdl = sub_handle->state_hdl;
	result->refcnt = 1;

	// copy uuid
	uuid_copy(result->uuid, uuid);

	return result;
}

/**
 * Attempts to create a new txnfs handle, or cleanup memory if it fails.
 *
 * This function is a wrapper of txnfs_alloc_handle. It adds error checking
 * and logging. It also cleans objects allocated in the subfsal if it fails.
 *
 * @param[in] export The txnfs export used by the handle.
 * @param[in,out] sub_handle The handle used by the subfsal.
 * @param[in] fs The filesystem of the new handle.
 * @param[in] new_handle Address where the new allocated pointer should be
 * written.
 * @param[in] subfsal_status Result of the allocation of the subfsal handle.
 *
 * @return An error code for the function.
 */
fsal_status_t txnfs_alloc_and_check_handle(struct txnfs_fsal_export *export,
					   struct fsal_obj_handle *sub_handle,
					   struct fsal_filesystem *fs,
					   struct fsal_obj_handle **new_handle,
					   fsal_status_t subfsal_status,
					   bool is_creation)
{
	struct gsh_buffdesc fh_desc;
	UDBG;

	if (FSAL_IS_ERROR(subfsal_status)) return subfsal_status;
	/** Result status of the operation. */
	fsal_status_t status = subfsal_status;

	struct txnfs_fsal_obj_handle *txn_handle;

	/* calling subfsal method to get unique key
	 * corresponding to the sub-handle
	 */
	op_ctx->fsal_export = export->export.sub_export;
	sub_handle->obj_ops->handle_to_key(sub_handle, &fh_desc);
	op_ctx->fsal_export = &export->export;

	if (is_creation) {
		uuid_t uuid;
		int ret = txnfs_db_insert_handle(&fh_desc, uuid);
		assert(ret == 0);
		txn_handle = txnfs_alloc_handle(export, sub_handle, fs, uuid);
	} else {
		uuid_t uuid;
		int ret = txnfs_db_get_uuid(&fh_desc, uuid);

		if (ret == -1) {
			LogDebug(COMPONENT_FSAL,
				 "Handle not found, creating one!");
			int ret = txnfs_db_insert_handle(&fh_desc, uuid);
			assert(ret == 0);
		}
		txn_handle = txnfs_alloc_handle(export, sub_handle, fs, uuid);
	}
	*new_handle = &txn_handle->obj_handle;
	return status;
}

/* lookup
 * deprecated NULL parent && NULL path implies root handle
 */

static fsal_status_t lookup(struct fsal_obj_handle *parent, const char *path,
			    struct fsal_obj_handle **handle,
			    struct attrlist *attrs_out)
{
	/** Parent as txnfs handle.*/
	struct txnfs_fsal_obj_handle *txn_parent =
	    container_of(parent, struct txnfs_fsal_obj_handle, obj_handle);

	/** Handle given by the subfsal. */
	struct fsal_obj_handle *sub_handle = NULL;
	*handle = NULL;

	UDBG;
	/* call to subfsal lookup with the good context. */
	fsal_status_t status;
	/** Current txnfs export. */
	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);
	op_ctx->fsal_export = export->export.sub_export;
	status = txn_parent->sub_handle->obj_ops->lookup(
	    txn_parent->sub_handle, path, &sub_handle, attrs_out);
	op_ctx->fsal_export = &export->export;

	/* wraping the subfsal handle in a txnfs handle. */
	return txnfs_alloc_and_check_handle(export, sub_handle, parent->fs,
					    handle, status,
					    false /* is_creation */
	);
}

static fsal_status_t makedir(struct fsal_obj_handle *dir_hdl, const char *name,
			     struct attrlist *attrs_in,
			     struct fsal_obj_handle **new_obj,
			     struct attrlist *attrs_out)
{
	*new_obj = NULL;
	/** Parent directory txnfs handle. */
	struct txnfs_fsal_obj_handle *parent_hdl =
	    container_of(dir_hdl, struct txnfs_fsal_obj_handle, obj_handle);
	/** Current txnfs export. */
	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	/** Subfsal handle of the new directory.*/
	struct fsal_obj_handle *sub_handle;
	UDBG;
	/* Creating the directory with a subfsal handle. */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status = parent_hdl->sub_handle->obj_ops->mkdir(
	    parent_hdl->sub_handle, name, attrs_in, &sub_handle, attrs_out);
	op_ctx->fsal_export = &export->export;

	/* wraping the subfsal handle in a txnfs handle. */
	return txnfs_alloc_and_check_handle(export, sub_handle, dir_hdl->fs,
					    new_obj, status,
					    true /* is_creation */);
}

static fsal_status_t makenode(struct fsal_obj_handle *dir_hdl, const char *name,
			      object_file_type_t nodetype,
			      struct attrlist *attrs_in,
			      struct fsal_obj_handle **new_obj,
			      struct attrlist *attrs_out)
{
	/** Parent directory txnfs handle. */
	struct txnfs_fsal_obj_handle *txnfs_dir =
	    container_of(dir_hdl, struct txnfs_fsal_obj_handle, obj_handle);
	/** Current txnfs export. */
	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);
	UDBG;
	/** Subfsal handle of the new node.*/
	struct fsal_obj_handle *sub_handle;

	*new_obj = NULL;

	/* Creating the node with a subfsal handle. */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status = txnfs_dir->sub_handle->obj_ops->mknode(
	    txnfs_dir->sub_handle, name, nodetype, attrs_in, &sub_handle,
	    attrs_out);
	op_ctx->fsal_export = &export->export;

	/* wraping the subfsal handle in a txnfs handle. */
	return txnfs_alloc_and_check_handle(export, sub_handle, dir_hdl->fs,
					    new_obj, status,
					    true /* is_creation */
	);
}

/** makesymlink
 *  Note that we do not set mode bits on symlinks for Linux/POSIX
 *  They are not really settable in the kernel and are not checked
 *  anyway (default is 0777) because open uses that target's mode
 */

static fsal_status_t makesymlink(struct fsal_obj_handle *dir_hdl,
				 const char *name, const char *link_path,
				 struct attrlist *attrs_in,
				 struct fsal_obj_handle **new_obj,
				 struct attrlist *attrs_out)
{
	/** Parent directory txnfs handle. */
	struct txnfs_fsal_obj_handle *txnfs_dir =
	    container_of(dir_hdl, struct txnfs_fsal_obj_handle, obj_handle);
	/** Current txnfs export. */
	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	/** Subfsal handle of the new link.*/
	struct fsal_obj_handle *sub_handle;
	UDBG;
	*new_obj = NULL;

	/* creating the file with a subfsal handle. */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status = txnfs_dir->sub_handle->obj_ops->symlink(
	    txnfs_dir->sub_handle, name, link_path, attrs_in, &sub_handle,
	    attrs_out);
	op_ctx->fsal_export = &export->export;

	/* wraping the subfsal handle in a txnfs handle. */
	return txnfs_alloc_and_check_handle(export, sub_handle, dir_hdl->fs,
					    new_obj, status,
					    true /* is_creation */
	);
}

static fsal_status_t readsymlink(struct fsal_obj_handle *obj_hdl,
				 struct gsh_buffdesc *link_content,
				 bool refresh)
{
	struct txnfs_fsal_obj_handle *handle =
	    (struct txnfs_fsal_obj_handle *)obj_hdl;
	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);
	UDBG;
	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status = handle->sub_handle->obj_ops->readlink(
	    handle->sub_handle, link_content, refresh);
	op_ctx->fsal_export = &export->export;

	return status;
}

static fsal_status_t linkfile(struct fsal_obj_handle *obj_hdl,
			      struct fsal_obj_handle *destdir_hdl,
			      const char *name)
{
	struct txnfs_fsal_obj_handle *handle =
	    (struct txnfs_fsal_obj_handle *)obj_hdl;
	struct txnfs_fsal_obj_handle *txnfs_dir =
	    (struct txnfs_fsal_obj_handle *)destdir_hdl;
	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);
	UDBG;
	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status = handle->sub_handle->obj_ops->link(
	    handle->sub_handle, txnfs_dir->sub_handle, name);
	op_ctx->fsal_export = &export->export;

	return status;
}

/**
 * Callback function for read_dirents.
 *
 * See fsal_readdir_cb type for more details.
 *
 * This function restores the context for the upper stacked fsal or inode.
 *
 * @param name Directly passed to upper layer.
 * @param dir_state A txnfs_readdir_state struct.
 * @param cookie Directly passed to upper layer.
 *
 * @return Result coming from the upper layer.
 */
static enum fsal_dir_result txnfs_readdir_cb(const char *name,
					     struct fsal_obj_handle *sub_handle,
					     struct attrlist *attrs,
					     void *dir_state,
					     fsal_cookie_t cookie)
{
	struct txnfs_readdir_state *state =
	    (struct txnfs_readdir_state *)dir_state;
	struct fsal_obj_handle *new_obj;
	UDBG;
	if (FSAL_IS_ERROR(txnfs_alloc_and_check_handle(
		state->exp, sub_handle, sub_handle->fs, &new_obj,
		fsalstat(ERR_FSAL_NO_ERROR, 0), false))) {
		return false;
	}

	op_ctx->fsal_export = &state->exp->export;
	enum fsal_dir_result result =
	    state->cb(name, new_obj, attrs, state->dir_state, cookie);

	op_ctx->fsal_export = state->exp->export.sub_export;

	return result;
}

/**
 * read_dirents
 * read the directory and call through the callback function for
 * each entry.
 * @param dir_hdl [IN] the directory to read
 * @param whence [IN] where to start (next)
 * @param dir_state [IN] pass thru of state to callback
 * @param cb [IN] callback function
 * @param eof [OUT] eof marker true == end of dir
 */

static fsal_status_t read_dirents(struct fsal_obj_handle *dir_hdl,
				  fsal_cookie_t *whence, void *dir_state,
				  fsal_readdir_cb cb, attrmask_t attrmask,
				  bool *eof)
{
	struct txnfs_fsal_obj_handle *handle =
	    container_of(dir_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	struct txnfs_readdir_state cb_state = {
	    .cb = cb, .dir_state = dir_state, .exp = export};
	UDBG;
	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status = handle->sub_handle->obj_ops->readdir(
	    handle->sub_handle, whence, &cb_state, txnfs_readdir_cb, attrmask,
	    eof);
	op_ctx->fsal_export = &export->export;

	return status;
}

/**
 * @brief Compute the readdir cookie for a given filename.
 *
 * Some FSALs are able to compute the cookie for a filename deterministically
 * from the filename. They also have a defined order of entries in a directory
 * based on the name (could be strcmp sort, could be strict alpha sort, could
 * be deterministic order based on cookie - in any case, the dirent_cmp method
 * will also be provided.
 *
 * The returned cookie is the cookie that can be passed as whence to FIND that
 * directory entry. This is different than the cookie passed in the readdir
 * callback (which is the cookie of the NEXT entry).
 *
 * @param[in]  parent  Directory file name belongs to.
 * @param[in]  name    File name to produce the cookie for.
 *
 * @retval 0 if not supported.
 * @returns The cookie value.
 */

fsal_cookie_t compute_readdir_cookie(struct fsal_obj_handle *parent,
				     const char *name)
{
	fsal_cookie_t cookie;
	struct txnfs_fsal_obj_handle *handle =
	    container_of(parent, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);
	UDBG;
	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	cookie = handle->sub_handle->obj_ops->compute_readdir_cookie(
	    handle->sub_handle, name);
	op_ctx->fsal_export = &export->export;
	return cookie;
}

/**
 * @brief Help sort dirents.
 *
 * For FSALs that are able to compute the cookie for a filename
 * deterministically from the filename, there must also be a defined order of
 * entries in a directory based on the name (could be strcmp sort, could be
 * strict alpha sort, could be deterministic order based on cookie).
 *
 * Although the cookies could be computed, the caller will already have them
 * and thus will provide them to save compute time.
 *
 * @param[in]  parent   Directory entries belong to.
 * @param[in]  name1    File name of first dirent
 * @param[in]  cookie1  Cookie of first dirent
 * @param[in]  name2    File name of second dirent
 * @param[in]  cookie2  Cookie of second dirent
 *
 * @retval < 0 if name1 sorts before name2
 * @retval == 0 if name1 sorts the same as name2
 * @retval >0 if name1 sorts after name2
 */

int dirent_cmp(struct fsal_obj_handle *parent, const char *name1,
	       fsal_cookie_t cookie1, const char *name2, fsal_cookie_t cookie2)
{
	int rc;
	struct txnfs_fsal_obj_handle *handle =
	    container_of(parent, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	rc = handle->sub_handle->obj_ops->dirent_cmp(handle->sub_handle, name1,
						     cookie1, name2, cookie2);
	op_ctx->fsal_export = &export->export;
	return rc;
}

static fsal_status_t renamefile(struct fsal_obj_handle *obj_hdl,
				struct fsal_obj_handle *olddir_hdl,
				const char *old_name,
				struct fsal_obj_handle *newdir_hdl,
				const char *new_name)
{
	struct txnfs_fsal_obj_handle *txnfs_olddir =
	    container_of(olddir_hdl, struct txnfs_fsal_obj_handle, obj_handle);
	struct txnfs_fsal_obj_handle *txnfs_newdir =
	    container_of(newdir_hdl, struct txnfs_fsal_obj_handle, obj_handle);
	struct txnfs_fsal_obj_handle *txnfs_obj =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);
	UDBG;
	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status = txnfs_olddir->sub_handle->obj_ops->rename(
	    txnfs_obj->sub_handle, txnfs_olddir->sub_handle, old_name,
	    txnfs_newdir->sub_handle, new_name);
	op_ctx->fsal_export = &export->export;

	return status;
}

static fsal_status_t getattrs(struct fsal_obj_handle *obj_hdl,
			      struct attrlist *attrib_get)
{
	struct txnfs_fsal_obj_handle *handle =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);
	UDBG;
	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status = handle->sub_handle->obj_ops->getattrs(
	    handle->sub_handle, attrib_get);
	op_ctx->fsal_export = &export->export;

	return status;
}

static fsal_status_t txnfs_setattr2(struct fsal_obj_handle *obj_hdl,
				    bool bypass, struct state_t *state,
				    struct attrlist *attrs)
{
	struct txnfs_fsal_obj_handle *handle =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status = handle->sub_handle->obj_ops->setattr2(
	    handle->sub_handle, bypass, state, attrs);
	op_ctx->fsal_export = &export->export;

	return status;
}

/* file_unlink
 * unlink the named file in the directory
 */

static fsal_status_t file_unlink(struct fsal_obj_handle *dir_hdl,
				 struct fsal_obj_handle *obj_hdl,
				 const char *name)
{
	UDBG;
	struct txnfs_fsal_obj_handle *txnfs_dir =
	    container_of(dir_hdl, struct txnfs_fsal_obj_handle, obj_handle);
	struct txnfs_fsal_obj_handle *txnfs_obj =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);
	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status = txnfs_dir->sub_handle->obj_ops->unlink(
	    txnfs_dir->sub_handle, txnfs_obj->sub_handle, name);
	op_ctx->fsal_export = &export->export;

	txnfs_db_delete_uuid(txnfs_obj->uuid);

	return status;
}

/* handle_to_wire
 * fill in the opaque f/s file handle part.
 * we zero the buffer to length first.  This MAY already be done above
 * at which point, remove memset here because the caller is zeroing
 * the whole struct.
 */

static fsal_status_t handle_to_wire(const struct fsal_obj_handle *obj_hdl,
				    fsal_digesttype_t output_type,
				    struct gsh_buffdesc *fh_desc)
{
	struct txnfs_fsal_obj_handle *handle =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	memset(fh_desc->addr, 0, fh_desc->len);
	if (fh_desc->len >= TXN_UUID_LEN) {
		uuid_copy(fh_desc->addr, handle->uuid);
		fh_desc->len = sizeof(uuid_t);
	} else {
		LogMajor(COMPONENT_FSAL,
			 "Space too small for handle.  need %zu, have %zu",
			 TXN_UUID_LEN, fh_desc->len);
		return fsalstat(ERR_FSAL_TOOSMALL, 0);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * handle_to_key
 * return a handle descriptor into the handle in this object handle
 * @TODO reminder.  make sure things like hash keys don't point here
 * after the handle is released.
 */

static void handle_to_key(struct fsal_obj_handle *obj_hdl,
			  struct gsh_buffdesc *fh_desc)
{
	UDBG;
	struct txnfs_fsal_obj_handle *handle =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	uuid_t *uuid = malloc(sizeof(uuid_t));
	uuid_copy(*uuid, handle->uuid);
	fh_desc->addr = uuid;
	fh_desc->len = TXN_UUID_LEN;
}

/*
 * release
 * release our handle first so they know we are gone
 */

static void release(struct fsal_obj_handle *obj_hdl)
{
	// in tests, when shutdown handle is called
	// op_ctx seems to be NULL and causes crash
	if (!op_ctx) {
		return;
	}
	struct txnfs_fsal_obj_handle *hdl =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);
	UDBG;
	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	hdl->sub_handle->obj_ops->release(hdl->sub_handle);
	op_ctx->fsal_export = &export->export;

	/* cleaning data allocated by txnfs */
	fsal_obj_handle_fini(&hdl->obj_handle);
	gsh_free(hdl);
}

static bool txnfs_is_referral(struct fsal_obj_handle *obj_hdl,
			      struct attrlist *attrs, bool cache_attrs)
{
	struct txnfs_fsal_obj_handle *hdl =
	    container_of(obj_hdl, struct txnfs_fsal_obj_handle, obj_handle);

	struct txnfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);
	bool result;

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	result = hdl->sub_handle->obj_ops->is_referral(hdl->sub_handle, attrs,
						       cache_attrs);
	op_ctx->fsal_export = &export->export;

	return result;
}

void txnfs_handle_ops_init(struct fsal_obj_ops *ops)
{
	fsal_default_obj_ops_init(ops);

	ops->release = release;
	ops->lookup = lookup;
	ops->readdir = read_dirents;
	ops->compute_readdir_cookie = compute_readdir_cookie,
	ops->dirent_cmp = dirent_cmp, ops->mkdir = makedir;
	ops->mknode = makenode;
	ops->symlink = makesymlink;
	ops->readlink = readsymlink;
	ops->getattrs = getattrs;
	ops->link = linkfile;
	ops->rename = renamefile;
	ops->unlink = file_unlink;
	ops->close = txnfs_close;
	ops->handle_to_wire = handle_to_wire;
	ops->handle_to_key = handle_to_key;

	/* Multi-FD */
	ops->open2 = txnfs_open2;
	ops->check_verifier = txnfs_check_verifier;
	ops->status2 = txnfs_status2;
	ops->reopen2 = txnfs_reopen2;
	ops->read2 = txnfs_read2;
	ops->write2 = txnfs_write2;
	ops->seek2 = txnfs_seek2;
	ops->io_advise2 = txnfs_io_advise2;
	ops->commit2 = txnfs_commit2;
	ops->lock_op2 = txnfs_lock_op2;
	ops->setattr2 = txnfs_setattr2;
	ops->close2 = txnfs_close2;
	ops->fallocate = txnfs_fallocate;
	ops->copy = txnfs_copy;
	ops->clone = txnfs_clone;
	ops->clone2 = txnfs_clone2;

	/* xattr related functions */
	ops->list_ext_attrs = txnfs_list_ext_attrs;
	ops->getextattr_id_by_name = txnfs_getextattr_id_by_name;
	ops->getextattr_value_by_name = txnfs_getextattr_value_by_name;
	ops->getextattr_value_by_id = txnfs_getextattr_value_by_id;
	ops->setextattr_value = txnfs_setextattr_value;
	ops->setextattr_value_by_id = txnfs_setextattr_value_by_id;
	ops->remove_extattr_by_id = txnfs_remove_extattr_by_id;
	ops->remove_extattr_by_name = txnfs_remove_extattr_by_name;

	ops->is_referral = txnfs_is_referral;
}

/* export methods that create object handles
 */

/* lookup_path
 * modeled on old api except we don't stuff attributes.
 * KISS
 */

fsal_status_t txnfs_lookup_path(struct fsal_export *exp_hdl, const char *path,
				struct fsal_obj_handle **handle,
				struct attrlist *attrs_out)
{
	/** Handle given by the subfsal. */
	struct fsal_obj_handle *sub_handle = NULL;
	*handle = NULL;
	struct gsh_buffdesc fh_desc;
	UDBG;
	/* call underlying FSAL ops with underlying FSAL handle */
	struct txnfs_fsal_export *exp =
	    container_of(exp_hdl, struct txnfs_fsal_export, export);

	/* call to subfsal lookup with the good context. */
	fsal_status_t status;

	op_ctx->fsal_export = exp->export.sub_export;

	status = exp->export.sub_export->exp_ops.lookup_path(
	    exp->export.sub_export, path, &sub_handle, attrs_out);
	sub_handle->obj_ops->handle_to_key(sub_handle, &fh_desc);
	op_ctx->fsal_export = &exp->export;

	/* wraping the subfsal handle in a txnfs handle. */
	/* Note : txnfs filesystem = subfsal filesystem or NULL ? */

	return txnfs_alloc_and_check_handle(exp, sub_handle, NULL, handle,
					    status,
					    !txnfs_db_handle_exists(&fh_desc));
}

/* create_handle
 * Does what original FSAL_ExpandHandle did (sort of)
 * returns a ref counted handle to be later used in cache_inode etc.
 * NOTE! you must release this thing when done with it!
 * BEWARE! Thanks to some holes in the *AT syscalls implementation,
 * we cannot get an fd on an AF_UNIX socket, nor reliably on block or
 * character special devices.  Sorry, it just doesn't...
 * we could if we had the handle of the dir it is in, but this method
 * is for getting handles off the wire for cache entries that have LRU'd.
 * Ideas and/or clever hacks are welcome...
 */

fsal_status_t txnfs_create_handle(struct fsal_export *exp_hdl,
				  struct gsh_buffdesc *hdl_desc,
				  struct fsal_obj_handle **handle,
				  struct attrlist *attrs_out)
{
	/** Current txnfs export. */
	struct txnfs_fsal_export *export =
	    container_of(exp_hdl, struct txnfs_fsal_export, export);

	struct fsal_obj_handle *sub_handle; /*< New subfsal handle.*/
	struct gsh_buffdesc sub_fh;
	fsal_status_t status;
	uuid_t uuid = {0};
	void *start;

	*handle = NULL;

	LogDebug(COMPONENT_FSAL, "handle: %p, len: %lu", hdl_desc->addr,
		 hdl_desc->len);

	/* The NFS file handle passed in **contains** the UUID. However based
	 * on observation, the UUID is located in the LATTER part of the
	 * handle.
	 *
	 * Here we should query the db/cache for sub-FSAL's host handle, and
	 * call sub-FSAL's create_handle method to retrieve the sub-handle. 
	 */

	start = (char *)hdl_desc->addr + (hdl_desc->len - sizeof(uuid_t));
	memcpy(uuid, start, sizeof(uuid_t));

	if (txnfs_db_get_handle(uuid, &sub_fh) != 0) {
		LogDebug(COMPONENT_FSAL, "handle %p is not in db",
			 hdl_desc->addr);
		return fsalstat(ERR_FSAL_INVAL, 0);
	}
	op_ctx->fsal_export = export->export.sub_export;

	status = export->export.sub_export->exp_ops.create_handle(
	    export->export.sub_export, &sub_fh, &sub_handle, attrs_out);

	op_ctx->fsal_export = &export->export;

	LogDebug(COMPONENT_FSAL, "handle %p found. obj_hdl=%p, fileid=%lu",
		 hdl_desc->addr, sub_handle, sub_handle->fileid);

	/* wraping the subfsal handle in a txnfs handle. */
	/* Note : txnfs filesystem = subfsal filesystem or NULL ? */
	return txnfs_alloc_and_check_handle(export, sub_handle, NULL, handle,
					    status, false /* is_creation */);
}
