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

#include "log.h"
#include "opvec.h"
#include "txnfs_methods.h"
#include <assert.h>
#include <fsal_api.h>
#include <hashtable.h>
#include <nfs_proto_tools.h>

static void truncate_file(struct fsal_obj_handle *f);
static uint64_t get_file_size(struct fsal_obj_handle *f);

static uint32_t undoer_hdlset_indexfxn(struct hash_param *param,
				       struct gsh_buffdesc *key)
{
	return (uint64_t)key->addr % param->index_size;
}

static uint64_t undoer_hdlset_hashfxn(struct hash_param *param,
				      struct gsh_buffdesc *key)
{
	return (uint64_t)key->addr;
}

static int undoer_hdl_compare(struct gsh_buffdesc *key1,
			      struct gsh_buffdesc *key2)
{
	return (uint64_t)key1->addr - (uint64_t)key2->addr;
}

static int undoer_key_to_str(struct gsh_buffdesc *key, char *output)
{
	return snprintf(output, HASHTABLE_DISPLAY_STRLEN, "%p", key->addr);
}

static int undoer_val_to_str(struct gsh_buffdesc *val, char *output)
{
	struct fsal_obj_handle *hdl = val->addr;
	return snprintf(output, HASHTABLE_DISPLAY_STRLEN, "%ld", hdl->fileid);
}

static hash_parameter_t undoer_hdl_set_param = {
	.index_size = 17,
	.hash_func_key = undoer_hdlset_indexfxn,
	.hash_func_rbt = undoer_hdlset_hashfxn,
	.compare_key = undoer_hdl_compare,
	.key_to_str = undoer_key_to_str,
	.val_to_str = undoer_val_to_str,
	.flags = HT_FLAG_NONE
};

static inline void insert_handle(struct fsal_obj_handle *hdl)
{
	if (op_ctx->txn_hdl_set == NULL) return;

	struct gsh_buffdesc element;
	element.addr = hdl;
	element.len = sizeof(*hdl);
	/* ignore HASHTABLE_KEY_ARELEADY_EXISTS */
	HashTable_Set(op_ctx->txn_hdl_set, &element, &element);
}

static int hdlset_free_func(struct gsh_buffdesc key, struct gsh_buffdesc val)
{
	struct txnfs_fsal_export *exp =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);
	struct fsal_obj_handle *hdl = key.addr;
	if (hdl == NULL || hdl == exp->root || hdl == exp->bkproot ||
	    hdl == op_ctx->txn_bkp_folder)
		return 0;
	hdl->obj_ops->release(hdl);
	return 0;
}

/**
 * @brief Release all newly allocated fsal object handles used by the undoer.
 *
 * This will call ->release to all @c fsal_obj_handles in the hash set, and then
 * destroy the hash set. This is to be called after the undo executor finishes
 * its jobs.
 */
static inline void release_all_handles(void)
{
	hashtable_destroy(op_ctx->txn_hdl_set, hdlset_free_func);
	op_ctx->txn_hdl_set = NULL;
}

/**
 * @page Compound transaction undo executor payloads
 *
 * @section Summary
 *
 * This file contains a series of helper functions that performs the rollback
 * of partially completed transactions
 *
 */

static inline COMPOUND4args *get_compound_args() { return op_ctx->op_args; }

/**
 * @brief Exchange current file handle with a new one / Save current fh
 * / Restore saved fh to current
 *
 * These wrappers are here because we need to take care of reference issue,
 * otherwise the object handles created inside the undo executor (via
 * create_handle and lookup) will never get released and will leak.
 *
 * Note that obj_ops->get_ref and obj_ops->put_ref will not work here because
 * they are not implemented by most FSALs, assuming that ref counting will be
 * managed by FSAL_MDCACHE. However we are UNDER MDCACHE layer, so it cannot
 * have any sense of what is happening here. To address this issue, we will put
 * every used object handle in a hash set, and release them all after the undo
 * executor finishing its job.
 */
static inline void exchange_cfh(struct fsal_obj_handle **current,
				struct fsal_obj_handle *new)
{
	if (!new) return;
	if (*current) insert_handle(*current);
	*current = new;
}

static inline void save_cfh(struct fsal_obj_handle **saved,
			    struct fsal_obj_handle *current)
{
	if (!current) return;
	if (*saved) insert_handle(*saved);
	*saved = current;
}

static inline void restore_cfh(struct fsal_obj_handle **current,
			       struct fsal_obj_handle *saved)
{
	if (!saved) return;
	if (*current) insert_handle(*current);
	*current = saved;
}

/**
 * @brief Call the lookup method of @c parent and insert output object handle
 * into the hash set.
 */
static inline fsal_status_t my_lookup(struct fsal_obj_handle *parent,
				      const char *name,
				      struct fsal_obj_handle **output,
				      struct attrlist *attrs)
{
	fsal_status_t res;
	res = parent->obj_ops->lookup(parent, name, output, attrs);
	if (FSAL_IS_SUCCESS(res)) insert_handle(*output);
	return res;
}

/**
 * @brief Convert NFS4 file handle into @c fsal_obj_handle
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
	struct fsal_export *exp = op_ctx->fsal_export;
	fsal_status_t ret;

	ret = exp->exp_ops.wire_to_host(exp, FSAL_DIGEST_NFSV4, &buf, 0);
	assert(FSAL_IS_SUCCESS(ret));

	ret = exp->exp_ops.create_handle(exp, &buf, &handle, &_attrs);
	if (FSAL_IS_SUCCESS(ret)) {
		if (attrs) *attrs = _attrs;
		insert_handle(handle);
		return handle;
	} else {
		LogWarn(COMPONENT_FSAL, "can't get obj handle from fh: %d",
			ret.major);
		return NULL;
	}
}

/**
 * @brief Check if an operation in a compound is OK
 *
 * While each NFSv4 operation has different @c *arg and @c *res structures
 * representing operation parameters and results, there is a common feature
 * that the first field is always a @c nfsstat4 that indicates the status
 * code of the operation. Thus we actually can write a unified function to
 * check if the operation is completed successfully by simply checking if
 * that @c nfsstat4 variable is 0.
 */
static inline bool is_op_ok(struct nfs_resop4 *res)
{
	return res->nfs_resop4_u.opaccess.status == NFS4_OK;
}

/**
 * @brief Replay LOOKUP operation
 *
 * A helper to perform a file lookup according to the given LOOKUP4args
 * parameter, and update the current file handle & attrs
 */

static inline int replay_lookup(struct nfs_argop4 *arg,
				struct fsal_obj_handle **current,
				struct attrlist *attrs)
{
	char *name;
	utf8string *str = &arg->nfs_argop4_u.oplookup.objname;
	struct attrlist queried_attrs = {0};
	struct fsal_obj_handle *queried;
	fsal_status_t status;

	nfs4_utf8string2dynamic(str, UTF8_SCAN_ALL, &name);
	status = my_lookup(*current, name, &queried, &queried_attrs);

	/* cleanup *name after use */
	gsh_free(name);

	if (FSAL_IS_ERROR(status)) {
		LogWarn(COMPONENT_FSAL, "replay lookup failed:");
		LogWarn(COMPONENT_FSAL, "%s", msg_fsal_err(status.major));
		return status.major;
	}

	exchange_cfh(current, queried);
	if (attrs) *attrs = queried_attrs;
	return 0;
}

/**
 * @brief Extract the file name from CREATE operation's arg object
 *
 * This helper function extracts objname and convert the internal @c utf8string
 * into a regular C-style string (@c char*). We assume the conversion will
 * succeed because the compound operation would have failed here if it failed.
 */
static inline char *extract_create_name(struct nfs_argop4 *arg)
{
	char *name = NULL;
	nfs4_utf8string2dynamic(&arg->nfs_argop4_u.opcreate.objname,
				UTF8_SCAN_ALL, &name);
	assert(name);
	return name;
}

/**
 * @brief Undo CREATE operation
 *
 * NOTE: CREATE operation is intended for creating NON-regular files.
 * Supported types are: NF4LNK, NF4DIR, NF4SOCK, NF4FIFO, NF4CHR, NF4BLK.
 *
 * We don't have to check if the current object handle points to a directory,
 * because this operation would have failed if "current" is not a directory.
 *
 * @return Status code
 */
static int undo_create(struct nfs_argop4 *arg, struct fsal_obj_handle *cur)
{
	char *name = extract_create_name(arg);
	fsal_status_t status;
	struct fsal_obj_handle *created;

	status = my_lookup(cur, name, &created, NULL);
	assert(FSAL_IS_SUCCESS(status));
	status = cur->obj_ops->unlink(cur, created, name);

	/* cleanup name */
	gsh_free(name);

	if (FSAL_IS_ERROR(status)) {
		LogWarn(COMPONENT_FSAL, "undo create failed:");
		LogWarn(COMPONENT_FSAL, "%s", msg_fsal_err(status.major));
		return status.major;
	}

	return 0;
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
 * @return	The C-style char * string. If it's a NULL pointer, use
 * 		the current file handle instead.
 */
static char *extract_open_name(open_claim4 *claim)
{
	char *str = NULL;
	switch (claim->claim) {
		case CLAIM_NULL:
			nfs4_utf8string2dynamic(&claim->open_claim4_u.file,
						UTF8_SCAN_ALL, &str);
			break;

		case CLAIM_DELEGATE_CUR:
			nfs4_utf8string2dynamic(
			    &claim->open_claim4_u.delegate_cur_info.file,
			    UTF8_SCAN_ALL, &str);
			break;

		case CLAIM_PREVIOUS:
		case CLAIM_FH:
		case CLAIM_DELEG_PREV_FH:
		case CLAIM_DELEG_CUR_FH:
		default:
			break;
	}

	return str;
}

/**
 * @brief Check if a file has been allocated a UUID.
 *
 * This function queries the LevelDB entries to find if a given file has
 * been allocated a UUID. This is intended for @c undo_open to determine if
 * an opened file had existed before this compound or it is newly created.
 *
 * Every time a OPEN or LOOKUP operation on TXNFS is performed, FSAL_TXN
 * calls @txnfs_alloc_and_check_handle which tries to allocate a new or
 * retrieve an existing UUID associated with that file. If the file did not
 * exist before this compound, we put the UUID insertion request to a cache
 * and commit only when the whole compound is run successfully. If the compound
 * failed in the middle and some files has been created, they will NOT have
 * associated UUIDs.
 */
static bool file_has_uuid(struct fsal_obj_handle *file)
{
	struct gsh_buffdesc fh_desc;
	struct txnfs_fsal_export *exp;
	struct txnfs_fsal_obj_handle *txn_file;
	struct fsal_obj_handle *sub_file;
	uuid_t uuid_val;
	bool ret;

	/* switch context */
	exp =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);
	assert(exp);
	op_ctx->fsal_export = exp->export.sub_export;
	/* get the sub handle for file */
	txn_file = container_of(file, struct txnfs_fsal_obj_handle, obj_handle);
	sub_file = txn_file->sub_handle;
	/* retrieve the "host handle" */
	sub_file->obj_ops->handle_to_key(sub_file, &fh_desc);
	/* switch the context back */
	op_ctx->fsal_export = &exp->export;

	ret = txnfs_db_get_uuid_nocache(&fh_desc, uuid_val);

	return (ret == 0);
}

static int restore_data(struct fsal_obj_handle *target, uint64_t txnid,
			int opidx, bool truncate_dest)
{
	char backup_name[BKP_FN_LEN] = {'\0'};
	struct fsal_obj_handle *root, *backup_root, *backup_dir;
	struct fsal_obj_handle *backup_file = NULL;
	struct txnfs_fsal_export *exp =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);
	int ret = 0;
	fsal_status_t status;
	uint64_t in = 0, out = 0;
	uint64_t size = 0, copied = 0;

	/* construct names */
	snprintf(backup_name, BKP_FN_LEN, "%d.bkp", opidx);

	/* lookup backup file */
	get_txn_root(&root, NULL);
	/* switch context */
	op_ctx->fsal_export = exp->export.sub_export;
	backup_root = query_backup_root(root);
	assert(backup_root);
	backup_dir = query_txn_backup(backup_root, txnid);
	assert(backup_dir);
	status = my_lookup(backup_dir, backup_name, &backup_file, NULL);
	if (FSAL_IS_ERROR(status)) {
		ret = status.major;
		LogWarn(COMPONENT_FSAL,
			"can't lookup backup. err=%d, txnid=%lu, opidx=%d", ret,
			txnid, opidx);
		goto end;
	}

	/* find SUB handle of the target */
	struct txnfs_fsal_obj_handle *txn_cur =
	    container_of(target, struct txnfs_fsal_obj_handle, obj_handle);
	struct fsal_obj_handle *sub_cur = txn_cur->sub_handle;

	/* truncate the source file if requested */
	if (truncate_dest) truncate_file(sub_cur);

	size = get_file_size(backup_file);
	/* overwrite the source file. CFH is the file being written */
	status = backup_file->obj_ops->clone2(backup_file, &in, sub_cur, &out,
					      size, 0);
	/* ->clone2 uses FICLONERANGE ioctl which depends on CoW support
	 * in the underlying file system. We'll fall back to ->copy if
	 * this is not supported. */
	if (FSAL_IS_ERROR(status)) {
		LogWarn(COMPONENT_FSAL, "clone failed (%d, %d), try copy",
			status.major, status.minor);
		status = backup_file->obj_ops->copy(backup_file, in, sub_cur,
						    out, size, &copied);
		LogDebug(COMPONENT_FSAL, "%lu bytes copied", copied);
	}
	ret = status.major;

end:
	/* switch context back */
	op_ctx->fsal_export = &exp->export;

	return ret;
}

/**
 * @brief Undo OPEN operation
 *
 * OPEN operation may create a regular file that does not exist before, which
 * is what we want to undo here. If OPEN4args.openhow.opentype == OPEN4_CREATE,
 * the server will create a file as specified by the client. Another factor
 * that controls the type of creation is OPEN4args.openhow.openflag4_u.how.mode
 * If that field is UNCHECKED, OPEN operation will NOT issue error if the
 * requested file already exists. If mode is EXCLUSIVE4/EXCLUSIVE4_1, the
 * server will NOT issue error if the requested file exists and the "verifier"
 * matches.
 *
 * Therefore there is a problem - in some cases we cannot figure out whether
 * a file existed before or not. We need further discussion regarding this.
 *
 * >> Solution: Look up the persistent LevelDB entry - A newly created file
 * does not have such entry in DB.
 */
static int undo_open(struct nfs_argop4 *arg, struct fsal_obj_handle **cur,
		     uint64_t txnid, int opidx)
{
	int ret = 0;
	char *name = extract_open_name(&arg->nfs_argop4_u.opopen.claim);
	struct fsal_obj_handle *target;
	struct attrlist attrs;

	fsal_status_t status;

	/* retrieve the file handle being opened */
	if (name) {
		status = my_lookup(*cur, name, &target, &attrs);
	} else {
		/* if name is NULL then CURRENT is what is to be opened */
		target = *cur;
		status = get_optional_attrs(target, &attrs);
	}

	if (FSAL_IS_ERROR(status)) {
		LogWarn(COMPONENT_FSAL,
			"undo_open: lookup/getattr fail: %d, name=%s",
			status.major, name ? name : "<null>");
		ret = status.major;
		goto end;
	}

	/* to undo, let's close the file */
	target->obj_ops->close(target);

	/* if the file does not have an associated UUID, then we can say
	 * that the file is newly created. In this case we will remove that
	 * file */
	if (!file_has_uuid(target)) {
		status = (*cur)->obj_ops->unlink(*cur, target, name);
		ret = status.major;
	} else if (arg->nfs_argop4_u.opopen.openhow.opentype & OPEN4_CREATE &&
		   attrs.filesize == 0) {
		/* In this case the file might have been truncated when open */
		ret = restore_data(target, txnid, opidx, false);
	}

	exchange_cfh(cur, target);
end:
	gsh_free(name);
	return ret;
}

/**
 * @brief Undo LINK operation
 *
 * The LINK operation creates a hard link for the file represented by the
 * SAVED file handle, in the directory represented by CURRENT file handle.
 * The name is in LINK4args.newname.
 *
 * Basically we can undo this by using current->obj_ops->unlink.
 */
static int undo_link(struct nfs_argop4 *arg, struct fsal_obj_handle *cur)
{
	char *name;
	utf8string *str = &arg->nfs_argop4_u.oplink.newname;
	struct fsal_obj_handle *created;
	fsal_status_t status;

	nfs4_utf8string2dynamic(str, UTF8_SCAN_ALL, &name);
	status = my_lookup(cur, name, &created, NULL);
	assert(FSAL_IS_SUCCESS(status));
	status = cur->obj_ops->unlink(cur, created, name);
	if (FSAL_IS_ERROR(status)) {
		LogWarn(COMPONENT_FSAL, "undo link failed:");
		LogWarn(COMPONENT_FSAL, "%s", msg_fsal_err(status.major));
	}

	gsh_free(name);
	return status.major;
}

/**
 * @brief Undo REMOVE operation
 *
 * To undo a REMOVE operation, we simply move the backup file into the
 * directory represented by CURRENT filehandle. More specifically we do
 * the following:
 *
 * current->rename(current, olddir_hdl=backup_dir, old_name=opidx.bkp,
 * 		   newdir_hdl=current, new_name=REMOVE4args.target)
 *
 * NOTE: Since this involves backup, make sure to switch op_ctx->fsal_export
 * to txnfs_fsal_export.sub_export before doing the work.
 */
static int undo_remove(struct nfs_argop4 *arg, struct fsal_obj_handle *cur,
		       uint64_t txnid, int opidx)
{
	char backup_name[BKP_FN_LEN] = {'\0'};
	char *real_name;
	utf8string *str = &arg->nfs_argop4_u.opremove.target;
	struct fsal_obj_handle *root, *backup_root, *backup_dir;
	struct txnfs_fsal_export *exp =
	    container_of(op_ctx->fsal_export, struct txnfs_fsal_export, export);
	fsal_status_t status;

	/* construct names */
	snprintf(backup_name, BKP_FN_LEN, "%d.bkp", opidx);
	nfs4_utf8string2dynamic(str, UTF8_SCAN_ALL, &real_name);

	/* lookup backup directory */
	get_txn_root(&root, NULL);
	/* important: switch context */
	op_ctx->fsal_export = exp->export.sub_export;
	/* continue looking up backup dir */
	backup_root = query_backup_root(root);
	assert(backup_root);
	backup_dir = query_txn_backup(backup_root, txnid);
	assert(backup_dir);

	/* perform moving */
	/* first we should retrieve the SUB handle of "current" to
	 * ensure the consistency of operation context */
	struct txnfs_fsal_obj_handle *txn_cur =
	    container_of(cur, struct txnfs_fsal_obj_handle, obj_handle);
	struct fsal_obj_handle *sub_cur = txn_cur->sub_handle;
	status = sub_cur->obj_ops->rename(sub_cur, backup_dir, backup_name,
					  sub_cur, real_name);

	gsh_free(real_name);

	/* switch the context back */
	op_ctx->fsal_export = &exp->export;

	return status.major;
}

/**
 * @brief Retrieve the size of a file
 */
static inline uint64_t get_file_size(struct fsal_obj_handle *f)
{
	struct attrlist attrs = {0};
	fsal_status_t status;
	status = f->obj_ops->getattrs(f, &attrs);
	if (FSAL_IS_ERROR(status)) {
		LogWarn(COMPONENT_FSAL,
			"get_file_size: getattr failed. "
			"err = %d, fileid = %lu",
			status.major, f->fileid);
		return 0;
	}
	return attrs.filesize;
}

/**
 * @brief Truncate a file by setting its size to 0.
 *
 * The lower-level FSAL_VFS will finally take care of this by calling
 * @c ftruncate().
 */
static inline void truncate_file(struct fsal_obj_handle *f)
{
	fsal_status_t ret;
	struct fsal_obj_handle *new_hdl;
	ret = fsal_open2(f, NULL, FSAL_O_TRUNC, FSAL_NO_CREATE, NULL, NULL,
			 NULL, &new_hdl, NULL);
	if (FSAL_IS_ERROR(ret)) {
		LogWarn(COMPONENT_FSAL, "can't open file: %d", ret.major);
	}
	fsal_close(f);
}

/**
 * @brief Undo file write/copy
 *
 * This function should apply to both write and copy operations, on condition
 * that they are not inter-server communications.
 *
 * Basically this will rewrite the file with the backup.
 */
static int undo_write(struct fsal_obj_handle *cur, uint64_t txnid, int opidx)
{
	return restore_data(cur, txnid, opidx, true);
}

static int dispatch_undoer(struct op_vector *vec)
{
	struct op_desc *el = NULL;
	int i, ret = 0;

	opvec_iter_back(i, vec, el)
	{
		switch (vec->op) {
			case NFS4_OP_CREATE:
				ret = undo_create(el->arg, el->cwh);
				break;

			case NFS4_OP_LINK:
				ret = undo_link(el->arg, el->cwh);
				break;

			case NFS4_OP_REMOVE:
				ret = undo_remove(el->arg, el->cwh, vec->txnid,
						  el->opidx);
				break;

			case NFS4_OP_RENAME:
				break;

			case NFS4_OP_WRITE:
			case NFS4_OP_COPY:
			case NFS4_OP_CLONE:
				ret =
				    undo_write(el->cwh, vec->txnid, el->opidx);
				break;

			default:
				ret = ERR_FSAL_NOTSUPP;
				break;
		}

		if (ret != 0) {
			LogWarn(COMPONENT_FSAL,
				"Encountered error: %d, idx: "
				"%d, opcode: %d - cannot undo.",
				ret, el->opidx, el->opcode);
			break;
		}
	}
	return ret;
}

/**
 * @brief Execute the rollback task
 *
 * This function is called by txnfs_compound_restore and intended for rollback
 * partially done compound transaction. At this moment we have the following
 * assumptions:
 *
 * - The client should not assume that the current file handle won't change, so
 *   each compound is supposed to come with @c PUTFH or @c PUTROOTFH operations
 *   to explicitly set the file or directory the client wants to work on.
 *
 * - The client user uses vNFS or traditional POSIX api. That is to say, each
 *   compound should be homogeneous (i.e. does not contain multiple different
 *   "substantial" operations). Therefore we are not considering complex
 *   scenario such as creating, writing and removing a file in one compound.
 *   So, in this implementation we iterate the compound in forward order.
 */
int do_txn_rollback(uint64_t txnid, COMPOUND4res *res)
{
	COMPOUND4args *args = get_compound_args();
	int i, ret = 0;
	struct fsal_obj_handle *root = NULL;
	struct fsal_obj_handle *current = NULL;
	struct fsal_obj_handle *saved = NULL;
	fsal_status_t status;
	struct attrlist cur_attr = {0};
	struct op_vector vector;

	/* initialize op vector */
	opvec_init(&vector, txnid);

	/* initialize handle hash set */
	op_ctx->txn_hdl_set = hashtable_init(&undoer_hdl_set_param);

	/* let's start from ROOT */
	get_txn_root(&root, &cur_attr);
	exchange_cfh(&current, root);

	for (i = 0; i < res->resarray.resarray_len; i++) {
		struct nfs_resop4 *curop_res = &res->resarray.resarray_val[i];
		struct nfs_argop4 *curop_arg = &args->argarray.argarray_val[i];

		/* if we encounter failed op, we can stop */
		if (!is_op_ok(curop_res)) break;

		/* real payload here */
		int op = curop_res->resop;
		struct fsal_obj_handle *temp;
		nfs_fh4 *fh = NULL;

		switch (op) {
			case NFS4_OP_PUTFH:
				/* update current file handle */
				fh = &curop_arg->nfs_argop4_u.opputfh.object;

				temp = fh_to_obj_handle(fh, &cur_attr);
				exchange_cfh(&current, temp);
				if (!current) {
					LogWarn(
					    COMPONENT_FSAL,
					    "Can't find obj_handle from fh.");
					ret = ERR_FSAL_NOENT;
				}
				break;

			case NFS4_OP_PUTROOTFH:
				/* update current fh to root */
				exchange_cfh(&current, root);
				break;

			case NFS4_OP_SAVEFH:
				save_cfh(&saved, current);
				break;

			case NFS4_OP_RESTOREFH:
				if (!saved) {
					ret = ERR_FSAL_FAULT;
					break;
				}
				restore_cfh(&current, saved);
				break;

			case NFS4_OP_LOOKUP:
				/* update current fh to the queried one */
				ret = replay_lookup(curop_arg, &current,
						    &cur_attr);
				break;

			case NFS4_OP_LOOKUPP:
				/* update current fh to its parent */
				status =
				    my_lookup(current, "..", &temp, &cur_attr);
				ret = status.major;
				exchange_cfh(&current, temp);
				break;

			/* we should treat OPEN in a different way, because
			 * OPEN will change the current file handle and may also
			 * generate new file which should be undone here.
			 *
			 * Since OPEN only creates REGULAR files, it's safe to
			 * undo them in forward order.*/
			case NFS4_OP_OPEN:
				ret = undo_open(curop_arg, &current, txnid, i);
				break;

				/* The following operations are substantial ones
				 * that we would like to rollback. Let's put
				 * them to the vector for later process. */

			case NFS4_OP_CREATE:
			case NFS4_OP_LINK:
			case NFS4_OP_REMOVE:
			case NFS4_OP_RENAME:
			case NFS4_OP_WRITE:
			case NFS4_OP_COPY:
			case NFS4_OP_CLONE:
				ret = opvec_push(&vector, i, op, curop_arg,
						 curop_res, current, saved);
				break;

			default:
				LogWarn(COMPONENT_FSAL,
					"Operation %d will not be"
					" counted for rollback",
					op);
				break;
		}

		if (ret != 0) {
			LogWarn(COMPONENT_FSAL,
				"Error %d occurred when"
				"analyzing the compound. Txn rollback will"
				"not be supported. opidx=%d",
				ret, i);
			break;
		}
	}

	if (!ret) {
		ret = dispatch_undoer(&vector);
		if (ret)
			LogWarn(COMPONENT_FSAL,
				"Error %d occurred when"
				" executing rollback.\n",
				ret);
	}

	release_all_handles();
	opvec_destroy(&vector);
	return ret;
}
