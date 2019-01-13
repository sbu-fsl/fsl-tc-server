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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/* handle.c
 * CoWFS object (file|dir) handle object
 */

#include "config.h"

#include "fsal.h"
#include "fsal_handle_syscalls.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "cowfs_methods.h"
#include <os/subr.h>

int cowfs_readlink(struct cowfs_fsal_obj_handle *myself,
		 fsal_errors_t *fsal_error)
{
	int retval = 0;
	int fd = -1;
	ssize_t retlink;
	struct stat st;
	#ifndef __FreeBSD__
	int flags = O_PATH | O_NOACCESS | O_NOFOLLOW;
	#endif

	if (myself->u.symlink.link_content != NULL) {
		gsh_free(myself->u.symlink.link_content);
		myself->u.symlink.link_content = NULL;
		myself->u.symlink.link_size = 0;
	}

	#ifndef __FreeBSD__
	fd = cowfs_fsal_open(myself, flags, fsal_error);
	if (fd < 0)
		return fd;

	retval = vfs_stat_by_handle(fd, &st);
	if (retval < 0)
		goto error;
	#else
	struct fhandle *handle = v_to_fhandle(myself->handle->handle_data);

	retval = fhstat(handle, &st);
	if (retval < 0)
		goto error;
	#endif

	myself->u.symlink.link_size = st.st_size + 1;
	myself->u.symlink.link_content =
	    gsh_malloc(myself->u.symlink.link_size);

	retlink =
	    vfs_readlink_by_handle(myself->handle, fd, "",
				   myself->u.symlink.link_content,
				   myself->u.symlink.link_size);
	if (retlink < 0)
		goto error;
	myself->u.symlink.link_content[retlink] = '\0';
	#ifndef __FreeBSD__
	close(fd);
	#endif

	return retval;

 error:
	retval = -errno;
	*fsal_error = posix2fsal_error(errno);
	#ifndef __FreeBSD__
	close(fd);
	#endif
	if (myself->u.symlink.link_content != NULL) {
		gsh_free(myself->u.symlink.link_content);
		myself->u.symlink.link_content = NULL;
		myself->u.symlink.link_size = 0;
	}
	return retval;
}

int cowfs_get_root_handle(struct cowfs_filesystem *cowfs_fs,
			struct cowfs_fsal_export *exp)
{
	int retval = 0;

	cowfs_fs->root_fd = open(cowfs_fs->fs->path, O_RDONLY | O_DIRECTORY);

	if (cowfs_fs->root_fd < 0) {
		retval = errno;
		LogMajor(COMPONENT_FSAL,
			 "Could not open CoWFS mount point %s: rc = %s (%d)",
			 cowfs_fs->fs->path, strerror(retval), retval);
		return retval;
	}

	/* Check if we have to re-index the fsid based on config */
	if (exp->fsid_type != FSID_NO_TYPE &&
	    exp->fsid_type != cowfs_fs->fs->fsid_type) {
		retval = -change_fsid_type(cowfs_fs->fs, exp->fsid_type);
		if (retval != 0) {
			LogCrit(COMPONENT_FSAL,
				"Can not change fsid type of %s to %d, error %s",
				cowfs_fs->fs->path, exp->fsid_type,
				strerror(retval));
			return retval;
		}

		LogInfo(COMPONENT_FSAL,
			"Reindexed filesystem %s to fsid=0x%016"
			PRIx64".0x%016"PRIx64,
			cowfs_fs->fs->path,
			cowfs_fs->fs->fsid.major,
			cowfs_fs->fs->fsid.minor);
	}

	/* May reindex for some platforms */
	return cowfs_re_index(cowfs_fs, exp);
}

