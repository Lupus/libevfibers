/********************************************************************

  Copyright 2012 Konstantin Olkhovskiy <lupus@oxnull.net>

  This file is part of libevfibers.

  libevfibers is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, either version 3 of the
  License, or any later version.

  libevfibers is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General
  Public License along with libevfibers.  If not, see
  <http://www.gnu.org/licenses/>.

 ********************************************************************/

#ifndef _FBR_EIO_H_
#define _FBR_EIO_H_
/**
 * @file evfibers/eio.h
 * This file contains API for libeio fiber wrappers.
 */
#include <evfibers/config.h>
#ifndef FBR_EIO_ENABLED
# error "This build of libevfibers lacks support for libeio"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/statvfs.h>
#include <eio.h>
#include <evfibers/fiber.h>

/**
 * eio event.
 *
 * This event struct can represent an eio event.
 * @see fbr_ev_upcast
 * @see fbr_ev_wait
 */
struct fbr_ev_eio {
	eio_req *req; /*!< the libeio request itself */
	struct fbr_ev_base ev_base;
};

/**
 * Initializer for eio event.
 *
 * This functions properly initializes fbr_ev_eio struct. You should not do
 * it manually.
 * @see fbr_ev_eio
 * @see fbr_ev_wait
 */
void fbr_ev_eio_init(FBR_P_ struct fbr_ev_eio *ev, eio_req *req);

void fbr_eio_init();
int fbr_eio_open(FBR_P_ const char *path, int flags, mode_t mode, int pri);
int fbr_eio_truncate(FBR_P_ const char *path, off_t offset, int pri);
int fbr_eio_chown(FBR_P_ const char *path, uid_t uid, gid_t gid, int pri);
int fbr_eio_chmod(FBR_P_ const char *path, mode_t mode, int pri);
int fbr_eio_mkdir(FBR_P_ const char *path, mode_t mode, int pri);
int fbr_eio_rmdir(FBR_P_ const char *path, int pri);
int fbr_eio_unlink(FBR_P_ const char *path, int pri);
int fbr_eio_utime(FBR_P_ const char *path, eio_tstamp atime, eio_tstamp mtime,
		int pri);
int fbr_eio_mknod(FBR_P_ const char *path, mode_t mode, dev_t dev, int pri);
int fbr_eio_link(FBR_P_ const char *path, const char *new_path, int pri);
int fbr_eio_symlink(FBR_P_ const char *path, const char *new_path, int pri);
int fbr_eio_rename(FBR_P_ const char *path, const char *new_path, int pri);
int fbr_eio_mlock(FBR_P_ void *addr, size_t length, int pri);
int fbr_eio_close(FBR_P_ int fd, int pri);
int fbr_eio_sync(FBR_P_ int pri);
int fbr_eio_fsync(FBR_P_ int fd, int pri);
int fbr_eio_fdatasync(FBR_P_ int fd, int pri);
int fbr_eio_futime(FBR_P_ int fd, eio_tstamp atime, eio_tstamp mtime, int pri);
int fbr_eio_ftruncate(FBR_P_ int fd, off_t offset, int pri);
int fbr_eio_fchmod(FBR_P_ int fd, mode_t mode, int pri);
int fbr_eio_fchown(FBR_P_ int fd, uid_t uid, gid_t gid, int pri);
int fbr_eio_dup2(FBR_P_ int fd, int fd2, int pri);
ssize_t fbr_eio_read(FBR_P_ int fd, void *buf, size_t length, off_t offset,
		int pri);
ssize_t fbr_eio_write(FBR_P_ int fd, void *buf, size_t length, off_t offset,
		int pri);
int fbr_eio_mlockall(FBR_P_ int flags, int pri);
int fbr_eio_msync(FBR_P_ void *addr, size_t length, int flags, int pri);
int fbr_eio_readlink(FBR_P_ const char *path, int pri, char *buf, size_t size);
int fbr_eio_realpath(FBR_P_ const char *path, int pri, char *buf, size_t size);
int fbr_eio_stat(FBR_P_ const char *path, int pri, EIO_STRUCT_STAT *statdata);
int fbr_eio_lstat(FBR_P_ const char *path, int pri, EIO_STRUCT_STAT *statdata);
int fbr_eio_fstat(FBR_P_ int fd, int pri, EIO_STRUCT_STAT *statdata);
int fbr_eio_statvfs(FBR_P_ const char *path, int pri,
		EIO_STRUCT_STATVFS *statdata);
int fbr_eio_fstatvfs(FBR_P_ int fd, int pri, EIO_STRUCT_STATVFS *statdata);

#endif
