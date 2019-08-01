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
#include "cleanup.h"

/* the global queue data */
struct cleanup_queue cqueue;

pthread_t worker_tid;

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
	q->txnid_vec = gsh_calloc(capacity, sizeof(uint64_t));
	q->head = 0;
	q->tail = 0;
	q->size = 0;
	q->capacity = capacity;
	
	int err = pthread_spin_init(&q->lock, PTHREAD_PROCESS_SHARED);
	if (err)
		goto fail;
	return 0;

fail:
	gsh_free(q->txnid_vec);
	q->capacity = 0;
	return err;
}

/**
 * @brief Push txnid to the cleanup task queue
 * 
 * @param[in] q		The pointer to the task queue
 * @param[in] txnid	The transaction ID
 * 
 * @return 0 for success, ENOSPC if the queue is full
 */
int cleanup_push_txnid(struct cleanup_queue *q, uint64_t txnid)
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
	q->txnid_vec[q->head] = txnid;
	q->head = (q->head + 1) % q->capacity;
	q->size += 1;
	pthread_spin_unlock(&q->lock);
	return 0;
}

/**
 * @brief Pop an txnid from the cleanup task queue
 * 
 * @param[in] q		The pointer to the task queue
 * @param[out] txnid	The pointer to the output variable
 * 
 * @return 0 for success, otherwise for error (ENODATA if the queue is empty)
 */
int cleanup_pop_txnid(struct cleanup_queue *q, uint64_t *txnid)
{
	int err = 0;
	err = pthread_spin_lock(&q->lock);
	if (err)
		return err;

	if (q->size == 0) {
		pthread_spin_unlock(&q->lock);
		return ENODATA;
	}

	*txnid = q->txnid_vec[q->tail];
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
ssize_t cleanup_pop_many(struct cleanup_queue *q, size_t num, uint64_t *buf)
{
	ssize_t ret = 0;
	ret = pthread_spin_lock(&q->lock);
	if (ret)
		return -ret;
	size_t remaining = num;
	while (remaining > 0 && q->size > 0) {
		buf[num - remaining] = q->txnid_vec[q->tail];
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
 */
void cleanup_queue_destroy(struct cleanup_queue *q)
{
	pthread_spin_destroy(&q->lock);
	gsh_free(q->txnid_vec);
	q->capacity = 0;
	gsh_free(q);
}
