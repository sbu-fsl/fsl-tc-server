/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) CohortFS LLC, 2014
 * Author: Daniel Gryniewicz dang@cohortfs.com
 *
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
 *
 * -------------
 */

/* subfsal.h
 * CoWFS Sub-FSAL API
 */

#ifndef SUBFSAL_H
#define SUBFSAL_H

#include "config_parsing.h"

/* Export parameters */
extern struct config_block *cowfs_sub_export_param;


/** Routines for sub-FSALS
 */

void cowfs_sub_fini(struct cowfs_fsal_export *myself);

void cowfs_sub_init_export_ops(struct cowfs_fsal_export *myself,
			      const char *export_path);

int cowfs_sub_init_export(struct cowfs_fsal_export *myself);

/**
 * @brief Allocate the SubFSAL object handle
 *
 * Allocate the SubFSAL object handle.  It must be large enough to hold a
 * vfs_file_handle_t after the end of the normal handle, and the @a handle field
 * of the cowfs_fsal_obj_handle must point to the correct location for the
 * vfs_file_handle_t.
 *
 * @return CoWFS object handle on success, NULL on failure
 */
struct cowfs_fsal_obj_handle *cowfs_sub_alloc_handle(void);

int cowfs_sub_init_handle(struct cowfs_fsal_export *myself,
		struct cowfs_fsal_obj_handle *hdl,
		const char *path);
#endif /* SUBFSAL_H */
