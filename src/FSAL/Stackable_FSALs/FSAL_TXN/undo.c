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

static inline COMPOUND4args* get_compoud_args()
{
	return op_ctx->op_args;
}

/**
 * @brief Convert NFS4 file handle into @c fsal_obj_handle
 *
 * @param[in] fh	NFSv4 file handle presented in compound args
 *
 * @return corresponding fsal_obj_handle
 */
static inline fsal_obj_handle* fh_to_obj_handle(nfs_fh4 *fh,
						struct attrlist *attrs)
{
	struct gsh_buffdesc buf = {
		.addr = (void *)fh->nfs_fh4_val,
		.len = fh->nfs_fh4_len
	};
	struct fsal_obj_handle *handle = NULL;
	struct attrlist _attrs;
	fsal_status_t ret = txnfs_create_handle(op_ctx->fsal_export,
						&buf, &handle, &_attrs);
	if (ret == 0) {
		if (attrs)
			*attrs = _attrs;
		return handle;
	} else {
		LogWarn(COMPONENT_FSAL, "can't get obj handle from fh: %d",
			ret);
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

	if (status.major != ERR_FSAL_NO_ERROR) {
		LogWarn(COMPONENT_FSAL, "replay lookup failed:");
		LogWarn(COMPONENT_FSAL, msg_fsal_err(status.major));
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
 * @brief Extract the creation type from CREATE operation's args
 */
static inline nfs_ftype4 extract_create_type(struct nfs_argop4 *arg)
{
	return arg->nfs_argop4_u.opcreate.objtype.type;
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
	char *name = extract_create_name(curop_arg);
	nfs_fhtype type = extract_create_type(curop_arg);
	fsal_statrus_t status;
	if (type == NF4DIR) {
		/* TODO: we may have to deal with directories differently
		 * considering dependency issues */
	} else {
		/* otherwise, just delete it using unlink*/
		fsal_obj_handle *created;
		status = cur->obj_ops->lookup(cur, name, &created, NULL);
		assert(status.major == ERR_FSAL_NO_ERROR);
		status = cur->obj_ops->unlink(cur, created, name);
		if (status.major != 0) {
			LogWarn(COMPONENT_FSAL, "undo create failed:");
			LogWarn(COMPONENT_FSAL, msg_fsal_err(status.major));
			return status.major;
		}
	}

	return 0;
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
	/* TODO: do we need to update current file handle? */
	/* 1 = OPEN4_CREATE */
	bool is_create = arg->nfs_argop4_u.opopen.openhow.opentype == 1;
	int ret = 0;
	createmode4 mode = arg->nfs_argop4_u.opopen.openhow.openflag4_u.how;
	utf8string *str = &arg->nfs_argop4_u.opopen.claim.open_claim4_u.file;
	char *name;
	struct fsal_obj_handle *created;
	struct fsal_obj_handle *current = *cur;
	fsal_status_t status;

	/* don't do anything for non-creation mode */
	if (!is_create)
		return 0;

	/* if mode is GUARDED4, then it's guaranteed that a successful open
	 * operation indicates a newly created file */
	if (mode == GUARDED4) {
		nfs4_utf8string2dynamic(str, UTF8_SCAN_ALL, &name);
		status = current->obj_ops->lookup(current, name, &created,
						  NULL);
		ret = status.major;
		if (status.major == 0) {
			status = current->obj_ops->unlink(current, created,
							  name);
			ret = status.major;
		}
		
	} else {
		/* TODe: discussion needed */
	}
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
		LogWarn(COMPONENT_FSAL, msg_fsal_err(status.major));
	}
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
	nfs4_utf8string2dynamic(str, UTF8_SCAN_ALL, &name);

	/* lookup backup directory */
	get_txn_root(&root, NULL);
	backup_root = query_backup_root(root);
	backup_dir = query_txn_backup(backup_root, txnid);

	/* perform moving */
	status = cur->obj_ops->rename(cur, backup_dir, backup_name,
				      current, real_name);
	return status.major;
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

		case NF4_OP_REMOVE:
			ret = undo_remove(el->arg, el->cwh, el->cwh,
					  vec->txnid, opidx);
			break;

		case NFS_OP_RENAME:
			break;

		case NFS4_OP_WRITE:
			break;

		case NFS4_OP_COPY:
			break;

		case NFS4_OP_CLONE:
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
	fsal_obj_handle *root = NULL;
	fsal_obj_handle *current = NULL;
	fsal_obj_handle *saved = NULL;
	fsal_status_t status;
	struct attrlist cur_attr;
	struct op_vector vector;

	/* initialize op vector */
	opvec_init(&vector, txnid);

	/* let's start from ROOT */
	get_txn_root(&root, &cur_attr);
	current = root;

	for (i = 0; i < res->resarray.resarray_len; i++) {
		struct nfs_resop4 *curop_res = &res->resarray.resarray_val[i];
		struct nfs_argop4 *curop_arg = &args->argarray.argarray_val[i];

		/* if we encounter failed op, we can stop */
		if (!is_op_ok(curop_res))
			break;
		
		/* real payload here */
		int op = curop_res->resop;
		nfs_fh4 *fh = NULL;

		switch (op) {
		case NFS4_OP_PUTFH:
			/* update current file handle */
			fh = &curop_arg->nfs_argop4_u.opputfh.object;
			current = fh_to_obj_handle(fh, &cur_attr);
			if (!current) {
				LogWarn(COMPONENT_FSAL,
					"Can't find obj_handle from fh.");
				ret = ERR_FSAL_NOENT;
			}
			break;

		case NFS4_OP_PUTROOTFH:
			/* update current fh to root */
			get_txn_root(&current, &cur_attr);
			assert(current);
			break;

		case NFS4_OP_SAVEFH:
			saved = current;
			break;

		case NFS4_OP_RESTOREFH:
			if (!saved) {
				ret = ERR_FSAL_FAULT;
				break;
			}
			current = saved;
			break;

		case NFS4_OP_LOOKUP:
			/* update current fh to the queried one */
			ret = replay_lookup(curop_arg, &current, &cur_attr);
			break;

		case NFS4_OP_LOOKUPP:
			/* update current fh to its parent */
			current->obj_ops->lookup(current, "..", &current,
						 &cur_attr);
			break;

		/* we should treat OPEN in a different way */
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
