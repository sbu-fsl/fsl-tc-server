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

#include <abstract_mem.h>
#include <fsal_types.h>
/**
 * @brief Operation Vector
 */

#ifndef _OPVEC_H_
#define _OPVEC_H_

#define VEC_INIT_SIZE 16

struct op_desc {
	nfs_opnum4 opcode;
	struct nfs_argop4 *arg;
	struct nfs_resop4 *res;
	/* current working handle */
	struct fsal_obj_handle *cwh;
	/* saved handle */
	struct fsal_obj_handle *savedh;
};

struct op_vector {
	struct op_desc *v;
	uint64_t txnid;
	unsigned int len;
	unsigned int max;
	nfs_opnum4 op;
};

void opvec_init(struct op_vector *vec, uint64_t txnid);
int opvec_push(struct op_vector *vec, nfs_opnum4 opcode, nfs_argop4 *arg,
	       nfs_resop4 *res, struct fsal_obj_handle *current,
	       struct fsal_obj_handle *saved);

void opvec_destroy(struct op_vector *vec);

/**
 * @brief Iterate a op vector in forward order
 *
 * @param[in] vec The op_vector object
 * @param[in] ptr An op_desc pointer
 */
#define opvec_iter(i, vec, ptr) \
	for (i = 0, ptr = vec->v; i < vec->len; ++i, ++ptr)

/**
 * @brief Iterate a op vector in reverse order
 */
#define opvec_iter_back(i, vec, ptr) \
	for (i = vec->len - 1, ptr = &vec->v[vec->len - 1]; i >= 0; --i, --ptr)

#endif
