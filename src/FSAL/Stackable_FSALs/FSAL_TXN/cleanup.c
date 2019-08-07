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

#include "txnfs_methods.h"
#include <signal.h>

/**
 * @brief Initialize the circular queue for cleanup tasks
 *
 * @param[in] q     	The pointer to the queue struct to initialize
 * @param[in] capacity	The capacity of the queue. If 0 is used, it will
 * 			use CLEANUP_QUEUE_LEN as default value.
 *
 * @return 0 for success, otherwise error is indicated
 */
int cleanup_queue_init(struct cleanup_queue *q, size_t capacity)
{
	/* If this fails, the whole system will terminate */
	q->vec = gsh_calloc(capacity, sizeof(*q->vec));
	q->head = 0;
	q->tail = 0;
	q->size = 0;
	q->capacity = capacity;

	int err = pthread_spin_init(&q->lock, PTHREAD_PROCESS_SHARED);
	if (err) goto fail;
	return 0;

fail:
	gsh_free(q->vec);
	q->capacity = 0;
	return err;
}

/**
 * @brief Push txnid to the cleanup task queue
 *
 * @param[in] q		The pointer to the task queue
 * @param[in] txnid	The transaction ID
 * @param[in] bkp_folder The fsal_obj_handle of backup folder
 *
 * @return 0 for success, ENOSPC if the queue is full
 */
int cleanup_push_txnid(struct cleanup_queue *q, uint64_t txnid,
		       struct fsal_obj_handle *bkp_folder)
{
	int err = 0;
	err = pthread_spin_lock(&q->lock);
	if (err) {
		/* EDEADLOCK? */
		return err;
	}
	if (q->size >= q->capacity) {
		pthread_spin_unlock(&q->lock);
		return ENOSPC;
	}
	q->vec[q->head].txnid = txnid;
	q->vec[q->head].bkp_folder = bkp_folder;
	q->head = (q->head + 1) % q->capacity;
	q->size += 1;
	pthread_spin_unlock(&q->lock);
	return 0;
}

/**
 * @brief Pop an txnid from the cleanup task queue
 *
 * @param[in] q		The pointer to the task queue
 * @param[out] arg	The pointer to the output variable
 *
 * @return 0 for success, otherwise for error (ENODATA if the queue is empty)
 */
int cleanup_pop_txnid(struct cleanup_queue *q, struct cleanup_arg *arg)
{
	int err = 0;
	err = pthread_spin_lock(&q->lock);
	if (err) return err;

	if (q->size == 0) {
		pthread_spin_unlock(&q->lock);
		return ENODATA;
	}

	*arg = q->vec[q->tail];
	q->tail += (q->tail + 1) % q->capacity;
	q->size -= 1;
	pthread_spin_unlock(&q->lock);
	return 0;
}

/**
 * @brief Pop a number of txnids from the cleanup task queue
 *
 * @param[in] q		The pointer to the task queue
 * @param[in] num	Number of txnids to get
 * @param[out] buf	The buffer to output the txnids
 *
 * @return A positive number indicates the actual number of txnids
 * 	retrieved; 0 indicates that the queue is empty; negative number
 * 	indicates error.
 */
ssize_t cleanup_pop_many(struct cleanup_queue *q, size_t num,
			 struct cleanup_arg *buf)
{
	ssize_t ret = 0;
	ret = pthread_spin_lock(&q->lock);
	if (ret) return -ret;
	size_t remaining = num;
	while (remaining > 0 && q->size > 0) {
		buf[num - remaining] = q->vec[q->tail];
		q->tail = (q->tail + 1) % q->capacity;
		q->size -= 1;
		remaining -= 1;
	}
	pthread_spin_unlock(&q->lock);
	return num - remaining;
}

/**
 * @brief Destroy the queue
 *
 * @param[in] q		The pointer to the task queue
 *
 * Be aware: This function does NOT call release for leftover
 * object handles pointed to backup folders!
 */
void cleanup_queue_destroy(struct cleanup_queue *q)
{
	pthread_spin_destroy(&q->lock);
	gsh_free(q->vec);
	q->capacity = 0;
	gsh_free(q);
}

static enum fsal_dir_result record_dirent(const char *name,
					  struct fsal_obj_handle *obj,
					  struct attrlist *attrs,
					  void *dir_state, fsal_cookie_t cookie)
{
	struct glist_head *flist = (struct glist_head *)dir_state;
	struct txnfs_file_entry *entry = gsh_malloc(sizeof(*entry));

	entry->name = strndup(name, NAME_MAX);
	entry->obj = obj;
	glist_add(flist, &entry->glist);

	return DIR_CONTINUE;
}

/**
 * @brief the actual payload code to cleanup backup files
 */
static void txnfs_cleanup_backup(uint64_t txnid,
				 struct fsal_obj_handle *bkp_folder)
{
	struct fsal_obj_handle *txn_root = NULL;
	struct fsal_obj_handle *bkp_root = NULL;
	struct fsal_export *exp = op_ctx->fsal_export;
	fsal_status_t status = {0};
	struct glist_head file_list = {0}, *node, *tmp;
	struct txnfs_file_entry *ent;
	char name[BKP_FN_LEN] = {'\0'};
	bool eof;

	glist_init(&file_list);

	get_txn_root(&txn_root, NULL);
	assert(txn_root);

	/* ---- switch export ---- */
	op_ctx->fsal_export = exp->sub_export;

	bkp_root = query_backup_root(txn_root);
	if (!bkp_root) {
		LogDebug(COMPONENT_FSAL, "backup root not created");
		goto end;
	}

	/* Use readdir to retrieve the list of files contained in bkp folder */
	status = bkp_folder->obj_ops->readdir(bkp_folder, NULL, &file_list,
					      record_dirent, 0, &eof);
	assert(FSAL_IS_SUCCESS(status));

	glist_for_each_safe(node, tmp, &file_list)
	{
		ent = glist_entry(node, struct txnfs_file_entry, glist);
		status = bkp_folder->obj_ops->unlink(bkp_folder, ent->obj,
						     ent->name);
		assert(FSAL_IS_SUCCESS(status));
		gsh_free(ent->name);
		ent->obj->obj_ops->release(ent->obj);
		glist_del(node);
		gsh_free(ent);
	};

	/* remove the backup folder */
	snprintf(name, BKP_FN_LEN, "%lu", txnid);
	status = bkp_root->obj_ops->unlink(bkp_root, bkp_folder, name);
	if (!FSAL_IS_SUCCESS(status)) {
		LogWarn(COMPONENT_FSAL, "cannot remove backup dir: %d",
			status.major);
	}
	/* Now we should release bkp_folder to prevent mem leak
	 * MDCACHE won't take care of this because we are operating under it */
	bkp_folder->obj_ops->release(bkp_folder);
end:
	/* ---- restore export ---- */
	op_ctx->fsal_export = exp;
}

struct worker_arg {
	struct req_op_context *context;
	struct cleanup_queue *queue;
};

static void *backup_worker(void *ptr)
{
	struct worker_arg *args = ptr;
	struct cleanup_queue *queue = args->queue;
	/* n = the max num of txnids to pop from queue */
	const size_t n = 1024;
	/* it's ok to put this array on stack because it's userland */
	struct cleanup_arg ids[n];
	op_ctx = args->context;
	while (true) {
		ssize_t count = cleanup_pop_many(queue, n, ids);
		if (count < 0) {
			LogFatal(COMPONENT_FSAL, "deadlock? (%ld)", count);
		}
		for (int i = 0; i < count; ++i) {
			txnfs_cleanup_backup(ids[i].txnid, ids[i].bkp_folder);
		}
		sleep(1);
	}
	return NULL;
}

int init_backup_worker(struct txnfs_fsal_export *myself)
{
	struct req_op_context *new_ctx;
	struct worker_arg *args;
	pthread_t tid;
	int err = 0;
	int n_callers = op_ctx->creds->caller_glen;

	/* initialize task queue */
	err = cleanup_queue_init(&myself->cqueue, CLEANUP_QUEUE_LEN);
	if (err) {
		LogWarn(COMPONENT_FSAL, "can't init cleanup task queue: %d",
			err);
		return err;
	}

	/* assemble a copy of op_ctx */
	new_ctx = gsh_malloc(sizeof(*new_ctx));
	memcpy(new_ctx, op_ctx, sizeof(*new_ctx));
	new_ctx->creds = gsh_malloc(sizeof(*op_ctx->creds));
	memcpy(new_ctx->creds, op_ctx->creds, sizeof(*op_ctx->creds));
	new_ctx->creds->caller_garray = gsh_calloc(n_callers, sizeof(gid_t));
	memcpy(new_ctx->creds->caller_garray, op_ctx->creds->caller_garray,
	       n_callers * sizeof(gid_t));

	/* assemble args for the worker thread */
	args = gsh_malloc(sizeof(*args));
	args->context = new_ctx;
	args->queue = &myself->cqueue;

	/* spawn the thread */
	err = pthread_create(&tid, NULL, backup_worker, args);

	if (err) {
		LogWarn(COMPONENT_FSAL, "backup worker thread failed: %d", err);
		gsh_free(args);
		gsh_free(new_ctx->creds->caller_garray);
		gsh_free(new_ctx->creds);
		gsh_free(new_ctx);
		cleanup_queue_destroy(&myself->cqueue);
	} else {
		myself->cleanup_worker_tid = tid;
		myself->cleaner_ctx = new_ctx;
	}
	return err;
}

/**
 * @brief Submit a backup cleanup request
 *
 * @param[in] exp	TXNFS's export structure
 * @param[in] txnid	Transaction ID
 * @param[in] bkp_folder The obj handle pointed to the backup folder
 *
 * @return 0 if the task is submitted successfully and the cleanup will be
 * 	   performed asynchronously. Otherwise there might be some error and
 * 	   the cleanup has been done by synchronous call.
 */
int submit_cleanup_task(struct txnfs_fsal_export *exp, uint64_t txnid,
			struct fsal_obj_handle *bkp_folder)
{
	int err = 0;

	if (txnid == 0) {
		return 0;
	}
	if (!bkp_folder) {
		LogDebug(COMPONENT_FSAL, "Empty bkp_folder for txnid=%#lx",
			 txnid);
		return EINVAL;
	}
	if (exp->cleanup_worker_tid == 0) {
		LogWarnOnce(COMPONENT_FSAL,
			    "backup worker thread is not up. "
			    "Fallback to sync call.");
		err = ENAVAIL;
		goto sync;
	}
	err = cleanup_push_txnid(&exp->cqueue, txnid, bkp_folder);
	if (err != 0) {
		LogWarn(COMPONENT_FSAL, "can't add txnid to queue: %d", err);
		goto sync;
	}
	return 0;
sync:
	txnfs_cleanup_backup(txnid, bkp_folder);
	return err;
}
