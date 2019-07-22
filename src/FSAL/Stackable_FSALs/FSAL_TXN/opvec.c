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

#include "opvec.h"

/**
 * @brief initialize a op vector object
 *
 * This function uses gsh_calloc api, so if memory allocation fails,
 * the system would abort.
 */
void opvec_init(struct op_vector *vec, uint64_t txnid)
{
	vec->txnid = txnid;
	vec->v = gsh_calloc(VEC_INIT_SIZE, sizeof(struct op_desc));
	vec->len = 0;
	vec->max = VEC_INIT_SIZE;
	vec->op = 0;
}

/**
 * @brief Add an operation descriptor to the vector
 *
 * @param[in] vec	The vector object
 * @param[in] opcode	The operation number
 * @param[in] arg	The nfs_argop4 argument
 * @param[in] res	The nfs_resop4 result
 * @param[in] current	Current FSAL object handle
 * @param[in] saved	Saved FSAL object handle
 *
 * @return Status code: If it's successful, return 0; if the operation type
 * does not match, return ERR_FSAL_BADTYPE. Note that if there isn't enough
 * memory, @c gsh_realloc will abort the system.
 */
int opvec_push(struct op_vector *vec, uint32_t opidx, nfs_opnum4 opcode,
	       nfs_argop4 *arg, nfs_resop4 *res,
	       struct fsal_obj_handle *current, struct fsal_obj_handle *saved)
{
	/* supply the operation type of this vector */
	if (vec->op == 0) vec->op = opcode;

	/* check if opcode matches */
	if (opcode != vec->op) return ERR_FSAL_BADTYPE;

	/* if the array is full,
	 * request for reallocation of a double sized one */
	if (vec->len == vec->max) {
		gsh_realloc(vec->v, 2 * vec->max * sizeof(struct op_desc));
		vec->max *= 2;
	}

	/* add element */
	vec->v[vec->len].opidx = opidx;
	vec->v[vec->len].opcode = opcode;
	vec->v[vec->len].arg = arg;
	vec->v[vec->len].res = res;
	vec->v[vec->len].cwh = current;
	vec->v[vec->len].savedh = saved;
	vec->len++;

	return 0;
}

/**
 * @brief Destroy a vector
 *
 * Basically it frees the memory
 */
void opvec_destroy(struct op_vector *vec)
{
	gsh_free(vec->v);
	vec->len = 0;
	vec->max = 0;
	vec->v = NULL;
	vec->op = -1;
}
