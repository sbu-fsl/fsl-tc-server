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

#include <abstract_mem.h>
#include <fsal_types.h>
#include <log.h>
#include <pthread.h>
#include <errno.h>

#ifndef _CLEANUP_H_
#define _CLEANUP_H_

/* message queue data structure */
#define CLEANUP_QUEUE_LEN	131072

struct cleanup_queue {
	uint64_t *txnid_vec;
	size_t head;
	size_t tail;
	size_t size;
	size_t capacity;
	pthread_spinlock_t lock;
};
struct txnfs_fsal_export;

int cleanup_queue_init(struct cleanup_queue *q, size_t capacity);
int cleanup_push_txnid(struct cleanup_queue *q, uint64_t txnid);
int cleanup_pop_txnid(struct cleanup_queue *q, uint64_t *txnid);
ssize_t cleanup_pop_many(struct cleanup_queue *q, size_t num, uint64_t *buf);
void cleanup_queue_destroy(struct cleanup_queue *q);

/* the worker */
int init_backup_worker(struct txnfs_fsal_export *);
int submit_cleanup_task(struct txnfs_fsal_export *exp, uint64_t txnid);

#endif // _CLEANUP_H_