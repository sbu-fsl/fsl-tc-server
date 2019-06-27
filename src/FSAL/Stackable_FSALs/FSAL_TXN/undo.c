#include <fsal_api.h>
#include <nfs_proto_tools.h>
#include "txnfs_methods.h"
#include "log.h"
#include "opvec.h"
#include <assert.h>

/**
 * @page Compound transaction undo executor payloads
 *
 * @section Summary
 *
 * This file contains a series of helper functions that performs the rollback
 * of partially completed transactions
 *
 */

static inline COMPOUND4args* get_compound_args()
{
	return op_ctx->op_args;
}

/**
 * @brief Exchange current file handle with a new one / Save current fh
 * / Restore saved fh to current
 *
 * These wrappers are here because we need to take care of ref count.
 * NOTE: We are assuming that the @c new object already has ref count
 * incremented (e.g. @c fsal_obj_handle objects created by @c lookup or
 * @c txnfs_create_handle methods are already ref counted), so here we don't
 * call @c new->obj_ops->get method.
 */
static inline void exchange_cfh(struct fsal_obj_handle **current,
				struct fsal_obj_handle *new)
{
	if (!new)
		return;
	if (*current)
		(*current)->obj_ops->put_ref(*current);
	*current = new;
}

static inline void save_cfh(struct fsal_obj_handle **saved,
			    struct fsal_obj_handle *current)
{
	if (!current)
		return;
	if (*saved)
		(*saved)->obj_ops->put_ref(*saved);
	*saved = current;
	current->obj_ops->get_ref(current);
}

static inline void restore_cfh(struct fsal_obj_handle **current,
			       struct fsal_obj_handle *saved)
{
	if (!saved)
		return;
	if (*current)
		(*current)->obj_ops->put_ref(*current);
	saved->obj_ops->get_ref(saved);
	*current = saved;
}

/**
 * @brief Convert NFS4 file handle into @c fsal_obj_handle
 *
 * @param[in] fh	NFSv4 file handle presented in compound args
 *
 * @return corresponding fsal_obj_handle
 */
static inline struct fsal_obj_handle*
fh_to_obj_handle(nfs_fh4 *fh, struct attrlist *attrs)
{
	struct gsh_buffdesc buf = {
		.addr = (void *)fh->nfs_fh4_val,
		.len = fh->nfs_fh4_len
	};
	struct fsal_obj_handle *handle = NULL;
	struct attrlist _attrs;
	fsal_status_t ret = txnfs_create_handle(op_ctx->fsal_export,
						&buf, &handle, &_attrs);
	if (ret.major == 0) {
		if (attrs)
			*attrs = _attrs;
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
	struct attrlist queried_attrs;
	struct fsal_obj_handle *queried;
	fsal_status_t status;

	nfs4_utf8string2dynamic(str, UTF8_SCAN_ALL, &name);
	status = (*current)->obj_ops->lookup(*current, name, &queried,
					     &queried_attrs);

	/* cleanup *name after use */
	gsh_free(name);

	if (status.major != ERR_FSAL_NO_ERROR) {
		LogWarn(COMPONENT_FSAL, "replay lookup failed:");
		LogWarn(COMPONENT_FSAL, "%s", msg_fsal_err(status.major));
		return status.major;
	}

	*current = queried;
	if (attrs)
		*attrs = queried_attrs;
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
				UTF8_SCAN_ALL,
				&name);
	assert(name);
	return name;
}

/** 
 * @brief Undo CREATE operation
 *
 * NOTE: CREATE operation is intended for creating NON-regular files.
 * Supported types are: NF4LNK, NF4DIR, NF4SOCK, NF4FIFO, NF4CHR, NF4BLK.
 *
 * We need to discuss how to deal with NF4DIR, since dependency may occur.
 * (For example, CREATE(dir /foo) -> GETFH -> PUTFH -> CREATE(dir bar))
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
	/* otherwise, just delete it using unlink*/
	struct fsal_obj_handle *created;
	status = cur->obj_ops->lookup(cur, name, &created, NULL);
	assert(status.major == ERR_FSAL_NO_ERROR);
	status = cur->obj_ops->unlink(cur, created, name);

	/* cleanup name */
	gsh_free(name);

	if (status.major != 0) {
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
	switch(claim->claim) {
	case CLAIM_NULL:
		nfs4_utf8string2dynamic(&claim->open_claim4_u.file,
					UTF8_SCAN_ALL, &str);
		break;

	case CLAIM_DELEGATE_CUR:
		nfs4_utf8string2dynamic(&claim->open_claim4_u
						.delegate_cur_info
						.file,
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
 * A possible workaround: Make backup during open if the opentype is
 * OPEN4_CREATE and the file actually exists.
 */
static int undo_open(struct nfs_argop4 *arg, struct fsal_obj_handle **cur)
{
	bool is_create = arg->nfs_argop4_u.opopen.openhow.opentype == 1;
	int ret = 0;
	createmode4 mode = arg->nfs_argop4_u.opopen.openhow
			   .openflag4_u.how.mode;
	char *name = extract_open_name(&arg->nfs_argop4_u.opopen.claim);
	struct fsal_obj_handle *target;
	
	struct fsal_obj_handle *current;
	fsal_status_t status;

	/* update current file handle */
	if (name) {
		status = (*cur)->obj_ops->lookup(*cur, name, &target, NULL);
		if (status.major != 0) {
			LogWarn(COMPONENT_FSAL, "undo_open: lookup fail: "
				"%d, name=%s", status.major, name);
			ret = status.major;
			goto end;
		}
		exchange_cfh(cur, target);
	}
	/* if name is NULL then CURRENT is to be opened */
	current = *cur;

	/* to undo, let's close the file */
	current->obj_ops->close(current);

	/* if mode is GUARDED4, then it's guaranteed that a successful open
	 * operation indicates a newly created file. In this case we will
	 * remove that file*/
	if (is_create && mode == GUARDED4) {
		status = current->obj_ops->unlink(current, target, name);
		ret = status.major;
	}
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
	status = cur->obj_ops->lookup(cur, name, &created, NULL);
	assert(status.major == ERR_FSAL_NO_ERROR);
	status = cur->obj_ops->unlink(cur, created, name);
	if (status.major != ERR_FSAL_NO_ERROR) {
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
 */
static int undo_remove(struct nfs_argop4 *arg, struct fsal_obj_handle *cur,
		       uint64_t txnid, int opidx)
{
	char backup_name[20] = { '\0' };
	char *real_name;
	utf8string *str = &arg->nfs_argop4_u.opremove.target;
	struct fsal_obj_handle *root, *backup_root, *backup_dir;
	fsal_status_t status;

	/* construct names */
	sprintf(backup_name, "%d.bkp", opidx);
	nfs4_utf8string2dynamic(str, UTF8_SCAN_ALL, &real_name);

	/* lookup backup directory */
	get_txn_root(&root, NULL);
	backup_root = query_backup_root(root);
	backup_dir = query_txn_backup(backup_root, txnid);

	/* perform moving */
	status = cur->obj_ops->rename(cur, backup_dir, backup_name,
				      cur, real_name);

	gsh_free(real_name);
	root->obj_ops->put_ref(root);
	backup_root->obj_ops->put_ref(backup_root);
	backup_dir->obj_ops->put_ref(backup_dir);
	return status.major;
}

/**
 * @brief Retrieve the size of a file
 */
static inline uint64_t get_file_size(struct fsal_obj_handle *f)
{
	struct attrlist attrs;
	fsal_status_t status;
	status = f->obj_ops->getattrs(f, &attrs);
	if (status.major != 0) {
		LogWarn(COMPONENT_FSAL, "get_file_size: getattr failed. "
			"err = %d, fileid = %lu", status.major, f->fileid);
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
	struct attrlist attrs = {
		.filesize = 0
	};
	fsal_status_t ret = f->obj_ops->setattr2(f, true, NULL, &attrs);
	if (ret.major != 0)
		LogWarn(COMPONENT_FSAL, "truncate_file failed: error %d,"
			" fileid=%lu", ret.major, f->fileid);
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
	char backup_name[20] = { '\0' };
	struct fsal_obj_handle *root;
	struct fsal_obj_handle *backup_root, *backup_dir, *backup_file = NULL;
	fsal_status_t status;
	int ret = 0;
	loff_t in = 0, out = 0;

	/* construct names */
	sprintf(backup_name, "%d.bkp", opidx);
	
	/* lookup backup file */
	get_txn_root(&root, NULL);
	backup_root = query_backup_root(root);
	backup_dir = query_txn_backup(backup_root, txnid);
	status = backup_dir->obj_ops->lookup(backup_dir, backup_name,
					     &backup_file, NULL);
	if (status.major != 0) {
		ret = status.major;
		LogWarn(COMPONENT_FSAL, "undo_write: can't lookup backup. "
			"err=%d, txnid=%lu, opidx=%d", ret, txnid, opidx);
		goto end;
	}

	/* truncate the source file */
	truncate_file(cur);

	/* overwrite the source file. CFH is the file being written */
	status = backup_file->obj_ops->clone2(backup_file, &in, cur, &out,
					      get_file_size(backup_file), 0);
	ret = status.major;

end:
	root->obj_ops->put_ref(root);
	backup_root->obj_ops->put_ref(backup_root);
	backup_dir->obj_ops->put_ref(backup_dir);
	if (backup_file)
		backup_file->obj_ops->put_ref(backup_file);

	return ret;
}

static int dispatch_undoer(struct op_vector *vec)
{
	struct op_desc *el = NULL;
	int opidx, ret = 0;

	opvec_iter_back(opidx, vec, el) {
		switch(vec->op) {
		case NFS4_OP_CREATE:
			ret = undo_create(el->arg, el->cwh);
			break;

		case NFS4_OP_LINK:
			ret = undo_link(el->arg, el->cwh);
			break;

		case NFS4_OP_REMOVE:
			ret = undo_remove(el->arg, el->cwh, vec->txnid, opidx);
			break;

		case NFS4_OP_RENAME:
			break;

		case NFS4_OP_WRITE:
		case NFS4_OP_COPY:
		case NFS4_OP_CLONE:
			ret = undo_write(el->cwh, vec->txnid, opidx);
			break;

		default:
			ret = ERR_FSAL_NOTSUPP;
			break;
		}

		if (ret != 0) {
			LogWarn(COMPONENT_FSAL, "Encountered error: %d, idx: "
				"%d, opcode: %d - cannot undo.", ret, opidx,
				el->opcode);
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
	struct attrlist cur_attr;
	struct op_vector vector;

	/* initialize op vector */
	opvec_init(&vector, txnid);

	/* let's start from ROOT */
	get_txn_root(&root, &cur_attr);
	root->obj_ops->get_ref(root);
	exchange_cfh(&current, root);

	for (i = 0; i < res->resarray.resarray_len; i++) {
		struct nfs_resop4 *curop_res = &res->resarray.resarray_val[i];
		struct nfs_argop4 *curop_arg = &args->argarray.argarray_val[i];

		/* if we encounter failed op, we can stop */
		if (!is_op_ok(curop_res))
			break;
		
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
				LogWarn(COMPONENT_FSAL,
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
			ret = replay_lookup(curop_arg, &temp, &cur_attr);
			exchange_cfh(&current, temp);
			break;

		case NFS4_OP_LOOKUPP:
			/* update current fh to its parent */
			status = current->obj_ops->lookup(current, "..", &temp,
							  &cur_attr);
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
			ret = undo_open(curop_arg, &current);
			break;

		/* The following operations are substantial ones that we would
		 * like to rollback. Let's put them to the vector for later
		 * process. */

		case NFS4_OP_CREATE:
		case NFS4_OP_LINK:
		case NFS4_OP_REMOVE:
		case NFS4_OP_RENAME:
		case NFS4_OP_WRITE:
		case NFS4_OP_COPY:
		case NFS4_OP_CLONE:
			ret = opvec_push(&vector, op, curop_arg, curop_res,
					current, saved);
			break;

		default:
			LogWarn(COMPONENT_FSAL, "Operation %d will not be"
				" counted for rollback", op);
			break;
		}

		if (ret != 0) {
			LogWarn(COMPONENT_FSAL, "Error %d occurred when"
				"analyzing the compound. Txn rollback will"
				"not be supported. opidx=%d", ret, i);
			break;
		}
	}

	if (!ret) {
		ret = dispatch_undoer(&vector);
		if (ret)
			LogWarn(COMPONENT_FSAL, "Error %d occurred when"
				" executing rollback.\n", ret);

	}

	opvec_destroy(&vector);
	return ret;
}
