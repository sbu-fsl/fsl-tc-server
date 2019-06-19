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
static inline fsal_obj_handle* fh_to_obj_handle(nfs_fh4 *fh)
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
	fsal_obj_handle *current = NULL;

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
			current = fh_to_obj_handle(fh);
			if (!current) {
				LogWarn(COMPONENT_FSAL,
					"Can't find obj_handle from fh.");
				ret = ERR_FSAL_IO;
			}
			break;

		default:
			break;
		}

	}

	return ret;
}
