/*
 *   Copyright (C) International Business Machines  Corp., 2010
 *   Author(s): Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/**
 * @file FSAL/FSAL_CoWFS/cowfs_methods.h
 * @brief System calls for the FreeBSD handle calls
 */

/* CoWFS methods for handles
 */

#ifndef CoWFS_METHODS_H
#define CoWFS_METHODS_H

#include "fsal_handle_syscalls.h"
#include "fsal_api.h"

struct cowfs_fsal_obj_handle;
struct cowfs_fsal_export;
struct cowfs_filesystem;

/*
 * CoWFS internal module
 */
struct cowfs_fsal_module {
	struct fsal_module module;
	struct fsal_obj_ops handle_ops;
	bool only_one_user;
};

/*
 * CoWFS internal export
 */
struct cowfs_fsal_export {
	struct fsal_export export;
	struct fsal_filesystem *root_fs;
	struct glist_head filesystems;
	int fsid_type;
	bool async_hsm_restore;
};

#define EXPORT_CoWFS_FROM_FSAL(fsal) \
	container_of((fsal), struct cowfs_fsal_export, export)

/*
 * CoWFS internal filesystem
 */
struct cowfs_filesystem {
	struct fsal_filesystem *fs;
	int root_fd;
	struct glist_head exports;
};

/*
 * Link CoWFS file systems and exports
 * Supports a many-to-many relationship
 */
struct cowfs_filesystem_export_map {
	struct cowfs_fsal_export *exp;
	struct cowfs_filesystem *fs;
	struct glist_head on_exports;
	struct glist_head on_filesystems;
};

void cowfs_unexport_filesystems(struct cowfs_fsal_export *exp);

/* private helpers from export
 */

void cowfs_handle_ops_init(struct fsal_obj_ops *ops);

int cowfs_get_root_fd(struct fsal_export *exp_hdl);

/* method proto linkage to handle.c for export
 */

fsal_status_t cowfs_lookup_path(struct fsal_export *exp_hdl,
			      const char *path,
			      struct fsal_obj_handle **handle,
			      struct attrlist *attrs_out);

fsal_status_t cowfs_create_handle(struct fsal_export *exp_hdl,
				struct gsh_buffdesc *hdl_desc,
				struct fsal_obj_handle **handle,
				struct attrlist *attrs_out);

struct cowfs_subfsal_obj_ops {
/**
 * @brief Gte sub-fsal attributes from an object
 *
 * @param[in] cowfs_hdl    The CoWFS object to modify
 * @param[in] fd		 Open filehandle on object
 *
 * @return FSAL status.
 */
	fsal_status_t (*getattrs)(struct cowfs_fsal_obj_handle *cowfs_hdl,
				  int fd, attrmask_t request_mask,
				  struct attrlist *attrs);
/**
 * @brief Set sub-fsal attributes on an object
 *
 * @param[in] cowfs_hdl    The CoWFS object to modify
 * @param[in] fd		 Open filehandle on object
 * @param[in] attrib_set Attributes to set
 *
 * @return FSAL status.
 */
	fsal_status_t (*setattrs)(struct cowfs_fsal_obj_handle *cowfs_hdl,
				  int fd, attrmask_t request_mask,
				  struct attrlist *attrib_set);
};

struct cowfs_fd {
	/** The open and share mode etc. */
	fsal_openflags_t openflags;
	/* rw lock to protect the file descriptor */
	pthread_rwlock_t fdlock;
	/** The kernel file descriptor. */
	int fd;
};

struct cowfs_state_fd {
	struct state_t state;
	struct cowfs_fd fd;
};

/*
 * CoWFS internal object handle
 * handle is a pointer because
 *  a) the last element of file_handle is a char[] meaning variable len...
 *  b) we cannot depend on it *always* being last or being the only
 *     variable sized struct here...  a pointer is safer.
 * wrt locks, should this be a lock counter??
 * AF_UNIX sockets are strange ducks.  I personally cannot see why they
 * are here except for the ability of a client to see such an animal with
 * an 'ls' or get rid of one with an 'rm'.  You can't open them in the
 * usual file way so open_by_handle_at leads to a deadend.  To work around
 * this, we save the args that were used to mknod or lookup the socket.
 */

struct cowfs_fsal_obj_handle {
	struct fsal_obj_handle obj_handle;
	fsal_dev_t dev;
	vfs_file_handle_t *handle;
#ifdef ENABLE_CoWFS_DEBUG_ACL
	uint32_t mode;		/*< POSIX access mode */
#endif
	struct cowfs_subfsal_obj_ops *sub_ops;	/*< Optional subfsal ops */
	const struct fsal_up_vector *up_ops;	/*< Upcall operations */
	union {
		struct {
			struct fsal_share share;
			struct cowfs_fd fd;
		} file;
		struct {
			unsigned char *link_content;
			int link_size;
		} symlink;
		struct {
			vfs_file_handle_t *dir;
			char *name;
		} unopenable;
	} u;
};

#define OBJ_CoWFS_FROM_FSAL(fsal) \
	container_of((fsal), struct cowfs_fsal_obj_handle, obj_handle)

/* default vex ops */
int cowfs_fd_to_handle(int fd, struct fsal_filesystem *fs,
		     vfs_file_handle_t *fh);

int cowfs_name_to_handle(int atfd,
		       struct fsal_filesystem *fs,
		       const char *name,
		       vfs_file_handle_t *fh);

int cowfs_open_by_handle(struct cowfs_filesystem *fs,
		       vfs_file_handle_t *fh, int openflags,
		       fsal_errors_t *fsal_error);

int cowfs_encode_dummy_handle(vfs_file_handle_t *fh,
			    struct fsal_filesystem *fs);

bool cowfs_is_dummy_handle(vfs_file_handle_t *fh);

fsal_status_t cowfs_check_handle(struct fsal_export *exp_hdl,
			       struct gsh_buffdesc *hdl_desc,
			       struct fsal_filesystem **fs,
			       vfs_file_handle_t *fh,
			       bool *dummy);

bool cowfs_valid_handle(struct gsh_buffdesc *desc);

int cowfs_readlink(struct cowfs_fsal_obj_handle *myself,
		 fsal_errors_t *fsal_error);

int cowfs_extract_fsid(vfs_file_handle_t *fh,
		     enum fsid_type *fsid_type,
		     struct fsal_fsid__ *fsid);

int cowfs_get_root_handle(struct cowfs_filesystem *cowfs_fs,
			struct cowfs_fsal_export *exp);

int cowfs_re_index(struct cowfs_filesystem *cowfs_fs,
		 struct cowfs_fsal_export *exp);

/*
 * CoWFS structure to tell subfunctions wether they should close the
 * returned fd or not
 */
struct closefd {
	int fd;
	int close_fd;
};

int cowfs_fsal_open(struct cowfs_fsal_obj_handle *hdl,
		  int openflags,
		  fsal_errors_t *fsal_error);

struct cowfs_fsal_obj_handle *alloc_handle(int dirfd,
					 vfs_file_handle_t *fh,
					 struct fsal_filesystem *fs,
					 struct stat *stat,
					 struct cowfs_fsal_obj_handle *parent_hdl,
					 const char *path,
					 struct fsal_export *exp_hdl);

static inline bool cowfs_unopenable_type(object_file_type_t type)
{
	if ((type == SOCKET_FILE) || (type == CHARACTER_FILE)
	    || (type == BLOCK_FILE)) {
		return true;
	} else {
		return false;
	}
}

struct closefd cowfs_fsal_open_and_stat(struct fsal_export *exp,
				      struct cowfs_fsal_obj_handle *myself,
				      struct stat *stat,
				      fsal_openflags_t flags,
				      fsal_errors_t *fsal_error);

/* State storage */
void cowfs_state_init(void);
void cowfs_state_release(struct gsh_buffdesc *key);
struct state_hdl *cowfs_state_locate(struct fsal_obj_handle *obj);

	/* I/O management */
fsal_status_t cowfs_close_my_fd(struct cowfs_fd *my_fd);

fsal_status_t cowfs_close(struct fsal_obj_handle *obj_hdl);

/* Multiple file descriptor methods */
struct state_t *cowfs_alloc_state(struct fsal_export *exp_hdl,
				enum state_type state_type,
				struct state_t *related_state);
void vfs_free_state(struct fsal_export *exp_hdl, struct state_t *state);

fsal_status_t cowfs_merge(struct fsal_obj_handle *orig_hdl,
			struct fsal_obj_handle *dupe_hdl);

fsal_status_t cowfs_open2(struct fsal_obj_handle *obj_hdl,
			struct state_t *state,
			fsal_openflags_t openflags,
			enum fsal_create_mode createmode,
			const char *name,
			struct attrlist *attrib_set,
			fsal_verifier_t verifier,
			struct fsal_obj_handle **new_obj,
			struct attrlist *attrs_out,
			bool *caller_perm_check);

fsal_status_t cowfs_reopen2(struct fsal_obj_handle *obj_hdl,
			  struct state_t *state,
			  fsal_openflags_t openflags);

void vfs_read2(struct fsal_obj_handle *obj_hdl,
	       bool bypass,
	       fsal_async_cb done_cb,
	       struct fsal_io_arg *read_arg,
	       void *caller_arg);

void vfs_write2(struct fsal_obj_handle *obj_hdl,
		bool bypass,
		fsal_async_cb done_cb,
		struct fsal_io_arg *write_arg,
		void *caller_arg);

fsal_status_t cowfs_copy(struct fsal_obj_handle *src_hdl, uint64_t src_offset,
		       struct fsal_obj_handle *dst_hdl, uint64_t dst_offset,
		       uint64_t count, uint64_t *copied);

fsal_status_t cowfs_commit2(struct fsal_obj_handle *obj_hdl,
			  off_t offset,
			  size_t len);

fsal_status_t cowfs_lock_op2(struct fsal_obj_handle *obj_hdl,
			   struct state_t *state,
			   void *owner,
			   fsal_lock_op_t lock_op,
			   fsal_lock_param_t *request_lock,
			   fsal_lock_param_t *conflicting_lock);

fsal_status_t getattr2(struct fsal_obj_handle *obj_hdl);

fsal_status_t cowfs_getattr2(struct fsal_obj_handle *obj_hdl,
			   struct attrlist *attrs);

fsal_status_t cowfs_setattr2(struct fsal_obj_handle *obj_hdl,
			   bool bypass,
			   struct state_t *state,
			   struct attrlist *attrib_set);

fsal_status_t cowfs_close2(struct fsal_obj_handle *obj_hdl,
			 struct state_t *state);

/* extended attributes management */
fsal_status_t cowfs_list_ext_attrs(struct fsal_obj_handle *obj_hdl,
				 unsigned int cookie,
				 fsal_xattrent_t *xattrs_tab,
				 unsigned int xattrs_tabsize,
				 unsigned int *p_nb_returned, int *end_of_list);
fsal_status_t cowfs_getextattr_id_by_name(struct fsal_obj_handle *obj_hdl,
					const char *xattr_name,
					unsigned int *pxattr_id);
fsal_status_t cowfs_getextattr_value_by_name(struct fsal_obj_handle *obj_hdl,
					   const char *xattr_name,
					   caddr_t buffer_addr,
					   size_t buffer_size,
					   size_t *p_output_size);
fsal_status_t cowfs_getextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					 unsigned int xattr_id,
					 caddr_t buffer_addr,
					 size_t buffer_size,
					 size_t *p_output_size);
fsal_status_t cowfs_setextattr_value(struct fsal_obj_handle *obj_hdl,
				   const char *xattr_name, caddr_t buffer_addr,
				   size_t buffer_size, int create);
fsal_status_t cowfs_setextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					 unsigned int xattr_id,
					 void *buffer_addr,
					 size_t buffer_size);
fsal_status_t cowfs_remove_extattr_by_id(struct fsal_obj_handle *obj_hdl,
				       unsigned int xattr_id);
fsal_status_t cowfs_remove_extattr_by_name(struct fsal_obj_handle *obj_hdl,
					 const char *xattr_name);

fsal_status_t check_hsm_by_fd(int fd);

/* transaction related function */
fsal_status_t cowfs_clone(struct fsal_obj_handle *src_hdl,
			  char **dst_name,
			  struct fsal_obj_handle *dir_hdl,
			  char *file_uuid);

fsal_status_t cowfs_get_fs_locations(struct cowfs_fsal_obj_handle *hdl,
				   struct attrlist *attrs_out);

static inline bool cowfs_set_credentials(const struct user_cred *creds,
					  const struct fsal_module *fsal_module)
{
	bool only_one_user = container_of(fsal_module,
				struct cowfs_fsal_module, module)->only_one_user;

	if (only_one_user)
		return fsal_set_credentials_only_one_user(creds);
	else {
		fsal_set_credentials(creds);
		return true;
	}
}

static inline void cowfs_restore_ganesha_credentials(const struct fsal_module
							*fsal_module)
{
	bool only_one_user = container_of(fsal_module,
				struct cowfs_fsal_module, module)->only_one_user;

	if (!only_one_user)
		fsal_restore_ganesha_credentials();
}

#endif			/* CoWFS_METHODS_H */
