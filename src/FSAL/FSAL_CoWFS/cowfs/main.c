/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Jim Lieb jlieb@panasas.com
 *
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */

/* main.c
 * Module core functions
 */

#include "config.h"

#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include "gsh_list.h"
#include "fsal.h"
#include "FSAL/fsal_init.h"
#include "fsal_handle_syscalls.h"

/* CoWFS FSAL module private storage
 */

/* defined the set of attributes supported with POSIX */
#define CoWFS_SUPPORTED_ATTRIBUTES (                                       \
		ATTR_TYPE     | ATTR_SIZE     |				\
		ATTR_FSID     | ATTR_FILEID   |				\
		ATTR_MODE     | ATTR_NUMLINKS | ATTR_OWNER     |	\
		ATTR_GROUP    | ATTR_ATIME    | ATTR_RAWDEV    |	\
		ATTR_CTIME    | ATTR_MTIME    | ATTR_SPACEUSED |	\
		ATTR_CHGTIME)

struct cowfs_fsal_module {
	struct fsal_module fsal;
	struct fsal_staticfsinfo_t fs_info;
	bool only_one_user;
};

const char myname[] = "CoWFS";

/* filesystem info for CoWFS */
static struct fsal_staticfsinfo_t default_posix_info = {
	.maxfilesize = UINT64_MAX,
	.maxlink = _POSIX_LINK_MAX,
	.maxnamelen = 1024,
	.maxpathlen = 1024,
	.no_trunc = true,
	.chown_restricted = true,
	.case_insensitive = false,
	.case_preserving = true,
	.lock_support = false,
	.lock_support_owner = true,
	.lock_support_async_block = false,
	.named_attr = true,
	.unique_handles = true,
	.lease_time = {10, 0},
	.acl_support = FSAL_ACLSUPPORT_ALLOW,
	.homogenous = true,
	.supported_attrs = CoWFS_SUPPORTED_ATTRIBUTES,
	.maxread = FSAL_MAXIOSIZE,
	.maxwrite = FSAL_MAXIOSIZE,
	.link_supports_permission_checks = false,
};

static struct config_item cowfs_params[] = {
	CONF_ITEM_BOOL("link_support", true,
		       fsal_staticfsinfo_t, link_support),
	CONF_ITEM_BOOL("symlink_support", true,
		       fsal_staticfsinfo_t, symlink_support),
	CONF_ITEM_BOOL("cansettime", true,
		       fsal_staticfsinfo_t, cansettime),
	CONF_ITEM_UI64("maxread", 512, FSAL_MAXIOSIZE, FSAL_MAXIOSIZE,
		       fsal_staticfsinfo_t, maxread),
	CONF_ITEM_UI64("maxwrite", 512, FSAL_MAXIOSIZE, FSAL_MAXIOSIZE,
		       fsal_staticfsinfo_t, maxwrite),
	CONF_ITEM_MODE("umask", 0,
		       fsal_staticfsinfo_t, umask),
	CONF_ITEM_BOOL("auth_xdev_export", false,
		       fsal_staticfsinfo_t, auth_exportpath_xdev),
	CONF_ITEM_BOOL("only_one_user", false,
		       fsal_staticfsinfo_t, only_one_user),
	CONFIG_EOL
};

struct config_block cowfs_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.cowfs",
	.blk_desc.name = "CoWFS",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = cowfs_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

/* private helper for export object
 */

struct fsal_staticfsinfo_t *cowfs_staticinfo(struct fsal_module *hdl)
{
	struct cowfs_fsal_module *myself;

	myself = container_of(hdl, struct cowfs_fsal_module, fsal);
	return &myself->fs_info;
}

/* Module methods
 */

/* init_config
 * must be called with a reference taken (via lookup_fsal)
 */

static fsal_status_t init_config(struct fsal_module *fsal_hdl,
				 config_file_t config_struct,
				 struct config_error_type *err_type)
{
	struct cowfs_fsal_module *cowfs_me =
	    container_of(fsal_hdl, struct cowfs_fsal_module, fsal);
#ifdef F_OFD_GETLK
	int fd, rc;
	struct flock lock;
	char *temp_name;
#endif

	cowfs_me->fs_info = default_posix_info;	/* copy the consts */

#ifdef F_OFD_GETLK
	/* If on a system that might support OFD locks, verify them.
	 * Only if they exist will we declare lock support.
	 */
	LogInfo(COMPONENT_FSAL, "FSAL_CoWFS testing OFD Locks");
	temp_name = gsh_strdup("/tmp/ganesha.nfsd.locktestXXXXXX");
	fd = mkstemp(temp_name);
	if (fd >= 0) {
		lock.l_whence = SEEK_SET;
		lock.l_type = F_RDLCK;
		lock.l_start = 0;
		lock.l_len = 0;
		lock.l_pid = 0;

		rc = fcntl(fd, F_OFD_GETLK, &lock);

		if (rc == 0)
			cowfs_me->fs_info.lock_support = true;
		else
			LogInfo(COMPONENT_FSAL, "Could not use OFD locks");

		close(fd);
		unlink(temp_name);
	} else {
		LogCrit(COMPONENT_FSAL,
			"Could not create file %s to test OFD locks",
			temp_name);
	}
	gsh_free(temp_name);
#endif

	if (cowfs_me->fs_info.lock_support)
		LogInfo(COMPONENT_FSAL, "FSAL_CoWFS enabling OFD Locks");
	else
		LogInfo(COMPONENT_FSAL, "FSAL_CoWFS disabling lock support");

	(void) load_config_from_parse(config_struct,
				      &cowfs_param,
				      &cowfs_me->fs_info,
				      true,
				      err_type);
	if (!config_error_is_harmless(err_type))
		return fsalstat(ERR_FSAL_INVAL, 0);
	display_fsinfo(fsal_hdl);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes constant = 0x%" PRIx64,
		     (uint64_t) CoWFS_SUPPORTED_ATTRIBUTES);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes default = 0x%" PRIx64,
		     default_posix_info.supported_attrs);
	LogDebug(COMPONENT_FSAL,
		 "FSAL INIT: Supported attributes mask = 0x%" PRIx64,
		 cowfs_me->fs_info.supported_attrs);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* Internal CoWFS method linkage to export object
 */

fsal_status_t cowfs_create_export(struct fsal_module *fsal_hdl,
				void *parse_node,
				struct config_error_type *err_type,
				const struct fsal_up_vector *up_ops);


/* Module initialization.
 * Called by dlopen() to register the module
 * keep a private pointer to me in myself
 */

/* my module private storage
 */

static struct cowfs_fsal_module CoWFS;

/* linkage to the exports and handle ops initializers
 */

MODULE_INIT void cowfs_init(void)
{
	int retval;
	struct fsal_module *myself = &CoWFS.fsal;

	retval = register_fsal(myself, myname, FSAL_MAJOR_VERSION,
			       FSAL_MINOR_VERSION, FSAL_ID_CoWFS);
	if (retval != 0) {
		fprintf(stderr, "CoWFS module failed to register");
		return;
	}
	myself->m_ops.create_export = cowfs_create_export;
	myself->m_ops.init_config = init_config;
}

MODULE_FINI void cowfs_unload(void)
{
	int retval;

	retval = unregister_fsal(&CoWFS.fsal);
	if (retval != 0) {
		fprintf(stderr, "CoWFS module failed to unregister");
		return;
	}
}
