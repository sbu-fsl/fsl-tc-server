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

/* export_cowfs.c
 * CoWFS Sub-FSAL export object
 */

#include "config.h"
#include "fsal_types.h"
#include "fsal_api.h"
#include "../cowfs_methods.h"
#include "../subfsal.h"
#ifdef ENABLE_CoWFS_DEBUG_ACL
#include "attrs.h"
#endif /* ENABLE_CoWFS_DEBUG_ACL */

/* Export */

static struct config_item_list fsid_types[] = {
	CONFIG_LIST_TOK("None", FSID_NO_TYPE),
	CONFIG_LIST_TOK("One64", FSID_ONE_UINT64),
	CONFIG_LIST_TOK("Major64", FSID_MAJOR_64),
	CONFIG_LIST_TOK("Two64", FSID_TWO_UINT64),
	CONFIG_LIST_TOK("uuid", FSID_TWO_UINT64),
	CONFIG_LIST_TOK("Two32", FSID_TWO_UINT32),
	CONFIG_LIST_TOK("Dev", FSID_DEVICE),
	CONFIG_LIST_TOK("Device", FSID_DEVICE),
	CONFIG_LIST_EOL
};

static struct config_item export_params[] = {
	CONF_ITEM_NOOP("name"),
	CONF_ITEM_TOKEN("fsid_type", FSID_NO_TYPE,
			fsid_types,
			cowfs_fsal_export, fsid_type),
	CONFIG_EOL
};

static struct config_block export_param_block = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.cowfs-export%d",
	.blk_desc.name = "FSAL",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = export_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

struct config_block *cowfs_sub_export_param = &export_param_block;

/* Handle syscalls */

void cowfs_sub_fini(struct cowfs_fsal_export *myself)
{
}

void cowfs_sub_init_export_ops(struct cowfs_fsal_export *myself,
			      const char *export_path)
{
}

int cowfs_sub_init_export(struct cowfs_fsal_export *myself)
{
#ifdef ENABLE_CoWFS_DEBUG_ACL
	cowfs_acl_init();
#endif /* ENABLE_CoWFS_DEBUG_ACL */
	return 0;
}

struct cowfs_fsal_obj_handle *cowfs_sub_alloc_handle(void)
{
	struct cowfs_fsal_obj_handle *hdl;

	hdl = gsh_calloc(1,
			 (sizeof(struct cowfs_fsal_obj_handle) +
			  sizeof(vfs_file_handle_t)));

	hdl->handle = (vfs_file_handle_t *) &hdl[1];

	return hdl;
}


struct cowfs_subfsal_obj_ops cowfs_obj_subops = {
#ifdef ENABLE_CoWFS_DEBUG_ACL
	cowfs_sub_getattrs,
	cowfs_sub_setattrs,
#endif /* ENABLE_CoWFS_DEBUG_ACL */
};

int cowfs_sub_init_handle(struct cowfs_fsal_export *myself,
		struct cowfs_fsal_obj_handle *hdl,
		const char *path)
{
	hdl->sub_ops = &cowfs_obj_subops;
	return 0;
}
