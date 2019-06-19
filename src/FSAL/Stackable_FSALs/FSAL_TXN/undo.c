#include "txnfs_methods.h"
#include "log.h"
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
static int undo_create(struct nfs_argop4 *arg, struct fsal_obj_handle **cur)
{
	struct fsal_obj_handle *current = *cur;
	char *name = extract_create_name(curop_arg);
	nfs_fhtype type = extract_create_type(curop_arg);
	if (type == NF4DIR) {
		/* TODO: we may have to deal with directories differently
		 * considering dependency issues */
	} else {
		/* otherwise, just delete it using unlink*/
		fsal_obj_handle *created;
		created = current->obj_ops->lookup(current, name,
						   &created, NULL);
		assert(created);
		status = current->obj_ops->unlink(current, created, name);
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
	fsal_status_t status;
	struct attrlist cur_attr;

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
				ret = ERR_FSAL_IO;
			}
			break;

		case NFS4_OP_PUTROOTFH:
			/* update current fh to root */
			get_txn_root(&current, &cur_attr);
			assert(current);
			break;

		case NFS4_OP_SAVEFH:
		case NFS4_OP_RESTOREFH:
			/* save and restore current fh:
			 * how should we deal with this? 
			 */
			break;

		case NFS4_OP_LOOKUP:
		case NFS4_OP_LOOKUPP:
			/* we don't have to be concerned with these two
			 * operations: the client have to use GETFH and then
			 * PUTFH anyway if it wants to use the queried handle.
			 */
			break;

		/* The following operations are substantial ones that we would
		 * like to rollback
		 */
		case NFS4_OP_CREATE:
			ret = undo_create(curop_arg, &current);
			break;

		case NFS4_OP_OPEN:
			ret = undo_open(curop_arg, &current);
			break;

		case NFS4_OP_LINK:
			/* just unlink the created link */
			break;

		case NFS4_OP_REMOVE:
			/* just copy the backup file back */
			break;

		case NFS4_OP_RENAME:
			/* restore the name: may need to consider
			 * trivial renaming or file moving? */
			break;

		case NFS4_OP_WRITE:
			/* just copy the backup file back */
			break;

		case NFS4_OP_COPY:
			/* remove/unlink the copies */
			break;

		case NFS4_OP_CLONE:
			/* remove/unlink the copies */
			break;

		case NFS4_OP_WRITE_SAME:
			/* copy the backup file back */
			break;

		default:
			break;
		}

		if (ret != 0) {
			LogWarn(COMPONENT_FSAL, "undo executor meet error.");
			break;
		}
	}
	return ret;
}
