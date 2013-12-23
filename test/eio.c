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

#include <check.h>
#include <evfibers/config.h>
#ifdef FBR_EIO_ENABLED

#include <errno.h>
#include <ev.h>
#include <stdio.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <evfibers/eio.h>
#include <evfibers_private/fiber.h>

static char small_msg[] = "Small test line\n\n";
static char big_msg[] =
"Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed mi ante, elementum"
"ac pharetra id, porttitor eu leo. Phasellus quam tortor, cursus quis accumsan"
"eget, molestie ac leo. Donec a velit elit. Ut placerat leo arcu. Donec"
"consectetur convallis metus, ac varius ante elementum et. Vestibulum ligula"
"ligula, molestie in tincidunt ac, vulputate nec elit. Ut lacus felis, sagittis"
"ut porta eu, fermentum non nibh. Curabitur lacinia, eros eget cursus"
"vestibulum, dolor eros pretium felis, ac faucibus arcu purus ultricies magna."
"Etiam eleifend diam et mauris vehicula euismod. Suspendisse at lorem tellus."
"Nam in enim dui. Donec nec dui ac erat luctus gravida in venenatis nisl.\n"

"Vivamus quis turpis feugiat odio convallis interdum in quis magna. Sed nec"
"ipsum ligula. Etiam sit amet mi justo. Quisque at ante nibh. Class aptent"
"taciti sociosqu ad litora torquent per conubia nostra, per inceptos himenaeos."
"Aenean hendrerit porta enim quis pretium. Sed rhoncus bibendum orci, non"
"fermentum tellus dignissim vitae. Aenean fringilla tristique libero non"
"fringilla. Suspendisse congue fringilla ullamcorper.\n"

"Suspendisse potenti. Donec feugiat congue elit eu mollis. Maecenas tempus, mi"
"sed aliquet scelerisque, magna mi pharetra orci, ut scelerisque lacus elit a"
"nunc. Suspendisse odio lorem, commodo sed consectetur sed, congue in urna."
"Nullam nec libero id est pulvinar convallis. Donec id eros et risus mattis"
"tempor. Aenean sollicitudin blandit ligula et vulputate. Pellentesque"
"consectetur pulvinar augue non laoreet. Aliquam erat volutpat. Etiam eget justo"
"ipsum. Nulla ultrices odio quis ipsum tristique ornare. Praesent rhoncus porta"
"lectus, vel ultrices augue porttitor sed.\n"

"Etiam ac purus nisl. Donec viverra vestibulum lacus non sagittis. Vivamus ac"
"ornare mi. Quisque ac arcu lacus, id hendrerit erat. In vel augue tellus."
"Pellentesque mattis augue sed odio tristique sollicitudin. Sed quis elementum"
"quam. Proin placerat vestibulum nulla sit amet hendrerit.\n"

"Phasellus gravida ante et purus hendrerit rutrum fringilla nulla scelerisque."
"Aenean ut quam sed nisl commodo egestas sed eget ipsum. Curabitur condimentum"
"sollicitudin nulla id scelerisque. Donec aliquet nibh et neque rutrum posuere."
"Vestibulum in mauris urna, quis vehicula purus. Proin vulputate tortor non"
"ligula consequat dapibus. Quisque varius blandit risus sed imperdiet. Nam nec"
"orci ut ipsum blandit blandit vel a diam. Quisque eget sapien eros, at faucibus"
"velit. Vestibulum dapibus tempus fringilla. Vestibulum ante ipsum primis in"
"faucibus orci luctus et ultrices posuere cubilia Curae; Aenean congue dapibus"
"imperdiet. Cras fringilla dui quis elit pulvinar eleifend a sed nisl. Aenean"
"vel neque magna.\n"

"Integer ultrices lorem sed velit bibendum aliquet. In congue vestibulum"
"malesuada. Curabitur molestie venenatis felis sit amet ultrices. Phasellus"
"nulla dui, volutpat et mollis vel, placerat sed nulla. Donec vel nulla eu nisl"
"hendrerit mollis sed ut lacus. Nulla nisi arcu, pellentesque a ornare non,"
"tristique at libero. Ut elementum risus et ipsum varius sit amet lobortis dui"
"cursus. Proin consequat pellentesque lorem vel rhoncus.  Curabitur nec bibendum"
"nulla. Aliquam id justo nulla. Aenean eget urna non elit tincidunt posuere."
"Nunc eget mauris id ante hendrerit convallis.\n"

"Sed adipiscing nisl sit amet urna tincidunt laoreet. Nullam rhoncus nulla"
"velit. Fusce porta turpis ornare mi accumsan viverra. Vestibulum sed ipsum"
"eros. Duis tincidunt iaculis erat sed suscipit. Mauris id laoreet lorem. Nullam"
"molestie massa eu tellus dapibus pretium. Morbi eu odio arcu. Etiam posuere"
"elit nec nunc ultrices rutrum id vel neque. Phasellus iaculis turpis nulla, ut"
"hendrerit eros. Vestibulum justo turpis, consequat non facilisis non, iaculis"
"id urna.\n"

"Maecenas ac nibh libero. Nam lectus velit, fermentum eget blandit in, molestie"
"et nunc. Nam mollis adipiscing dictum. Vestibulum congue mollis odio sed"
"dapibus. Aenean et dictum felis. Donec convallis orci sed lectus rutrum aliquet"
"tempus vitae nunc. Quisque pellentesque leo vel turpis vulputate sodales."
"Vestibulum eu erat neque. Sed aliquet, eros vel turpis duis.\n";

static void io_fiber(FBR_P_ _unused_ void *_arg)
{
	ssize_t retval;
	int fd;
	void *buf;
	off_t offt;
	char path_buf[PATH_MAX + 1];
	EIO_STRUCT_STAT statdata;
	EIO_STRUCT_STATVFS statvdata;

	memset(&statvdata, 0x00, sizeof(statvdata));
	retval = fbr_eio_statvfs(FBR_A_ "/tmp", &statvdata, 0);
	assert(0 == retval);

	fd = fbr_eio_open(FBR_A_ "/tmp/async.test", O_RDWR | O_CREAT | O_TRUNC,
			0644, 0);
	fail_unless(0 <= fd);

	retval = fbr_eio_ftruncate(FBR_A_ fd, 0, 0);
	fail_unless(0 == retval);

	retval = fbr_eio_syncfs(FBR_A_ fd, 0);
	fail_unless(0 == retval || ENOSYS == errno);

	retval = fbr_eio_fallocate(FBR_A_ fd, EIO_FALLOC_FL_KEEP_SIZE, 0,
			sizeof(big_msg), 0);
	fail_unless(0 == retval || ENOSYS == errno);

	memset(&statdata, 0x00, sizeof(statdata));
	retval = fbr_eio_fstat(FBR_A_ fd, &statdata, 0);
	assert(0 == retval);

	memset(&statvdata, 0x00, sizeof(statvdata));
	retval = fbr_eio_fstatvfs(FBR_A_ fd, &statvdata, 0);
	assert(0 == retval);

	retval = fbr_eio_futime(FBR_A_ fd, ev_now(fctx->__p->loop),
			ev_now(fctx->__p->loop), 0);
	fail_unless(0 == retval);

	retval = fbr_eio_fchmod(FBR_A_ fd, 0644, 0);
	fail_unless(0 == retval);

	retval = fbr_eio_fchown(FBR_A_ fd, getuid(), getgid(), 0);
	fail_unless(0 == retval);

	/* Small message test */
	retval = fbr_eio_write(FBR_A_ fd, small_msg, sizeof(small_msg), -1, 0);
	fail_unless(0 < retval);

	retval = fbr_eio_fsync(FBR_A_ fd, 0);
	fail_unless(0 == retval);

	retval = fbr_eio_sync_file_range(FBR_A_ fd, 0, sizeof(small_msg),
			EIO_SYNC_FILE_RANGE_WRITE, 0);
	fail_unless(0 == retval);

	retval = fbr_eio_seek(FBR_A_ fd, 0, EIO_SEEK_SET, 0);
	fail_unless(0 == retval);

	buf = malloc(sizeof(small_msg));
	fail_unless(NULL != buf);

	retval = fbr_eio_readahead(FBR_A_ fd, 0, sizeof(small_msg), 0);
	fail_unless(NULL != buf);

	retval = fbr_eio_read(FBR_A_ fd, buf, sizeof(small_msg), -1, 0);
	fail_unless(0 <= retval);
	fail_unless(!memcmp(small_msg, buf, sizeof(small_msg)));

	free(buf);

	/* Big message test */
	retval = fbr_eio_seek(FBR_A_ fd, 0, EIO_SEEK_CUR, 0);
	fail_unless(0 < retval);

	offt = retval;

	retval = fbr_eio_write(FBR_A_ fd, big_msg, sizeof(big_msg), -1, 0);
	fail_unless(0 < retval);

	retval = fbr_eio_fdatasync(FBR_A_ fd, 0);
	fail_unless(0 == retval);

	retval = fbr_eio_seek(FBR_A_ fd, offt, EIO_SEEK_SET, 0);
	fail_unless(0 < retval);

	buf = malloc(sizeof(big_msg));
	fail_unless(NULL != buf);

	retval = fbr_eio_read(FBR_A_ fd, buf, sizeof(big_msg), -1, 0);
	fail_unless(0 < retval);
	fail_unless(!memcmp(big_msg, buf, sizeof(big_msg)));

	free(buf);

	retval = fbr_eio_sync(FBR_A_ 0);
	fail_unless(0 == retval);

	retval = fbr_eio_close(FBR_A_ fd, 0);
	fail_unless(0 == retval);


	retval = fbr_eio_chown(FBR_A_ "/tmp/async.test", getuid(), getgid(), 0);
	assert(0 == retval);

	retval = fbr_eio_chmod(FBR_A_ "/tmp/async.test", 0644, 0);
	assert(0 == retval);

	retval = fbr_eio_utime(FBR_A_ "/tmp/async.test",
			ev_now(fctx->__p->loop),
			ev_now(fctx->__p->loop), 0);
	assert(0 == retval);

	memset(&statdata, 0x00, sizeof(statdata));
	retval = fbr_eio_stat(FBR_A_ "/tmp/async.test", &statdata, 0);
	assert(0 == retval);

	memset(&statdata, 0x00, sizeof(statdata));
	retval = fbr_eio_lstat(FBR_A_ "/tmp/async.test", &statdata, 0);
	assert(0 == retval);

	retval = fbr_eio_truncate(FBR_A_ "/tmp/async.test", 0, 0);
	assert(0 == retval);

	retval = fbr_eio_unlink(FBR_A_ "/tmp/async.test", 0);
	assert(0 == retval);

	retval = fbr_eio_mkdir(FBR_A_ "/tmp/async.test.dir", 0755, 0);
	assert(0 == retval);

	retval = fbr_eio_rmdir(FBR_A_ "/tmp/async.test.dir", 0);
	assert(0 == retval);

	retval = fbr_eio_mknod(FBR_A_ "/tmp/async.node", 0644, S_IFREG, 0);
	assert(0 == retval);

	retval = fbr_eio_link(FBR_A_ "/tmp/async.node",
			"/tmp/async.node.link", 0);
	assert(0 == retval);

	retval = fbr_eio_symlink(FBR_A_ "/tmp/async.node",
			"/tmp/async.node.symlink", 0);
	assert(0 == retval);

	memset(path_buf, 0x00, sizeof(path_buf));
	retval = fbr_eio_readlink(FBR_A_ "/tmp/async.node.symlink", path_buf,
			sizeof(path_buf) - 1, 0);
	printf("path_buf: %s\n", path_buf);
	assert(retval > 0);
	fail_unless(!strcmp(path_buf, "/tmp/async.node"));

	retval = fbr_eio_realpath(FBR_A_ "/tmp/../tmp/./async.node",
			path_buf, sizeof(path_buf) - 1, 0);
	assert(retval > 0);
	fail_unless(!strcmp(path_buf, "/tmp/async.node"));

	retval = fbr_eio_rename(FBR_A_ "/tmp/async.node.link",
			"/tmp/async.node.symlink", 0);
	assert(0 == retval);

	retval = fbr_eio_unlink(FBR_A_ "/tmp/async.node.symlink", 0);
	assert(0 == retval);

	retval = fbr_eio_unlink(FBR_A_ "/tmp/async.node", 0);
	assert(0 == retval);
}

START_TEST(test_eio)
{
	int retval;
	fbr_id_t fiber = FBR_ID_NULL;
	struct fbr_context context;
	fbr_init(&context, EV_DEFAULT);
	fbr_eio_init();
	signal(SIGPIPE, SIG_IGN);

	fiber = fbr_create(&context, "io_fiber", io_fiber, NULL, 0);
	fail_if(fbr_id_isnull(fiber));
	retval = fbr_transfer(&context, fiber);
	fail_unless(0 == retval, NULL);

	ev_run(EV_DEFAULT, 0);
	fbr_destroy(&context);
}
END_TEST

TCase * eio_tcase(void)
{
	TCase *tc_eio = tcase_create("EIO");
	tcase_add_test(tc_eio, test_eio);
	return tc_eio;
}

#else

TCase * eio_tcase(void)
{
	TCase *tc_eio = tcase_create("EIO_DISABLED");
	return tc_eio;
}

#endif
