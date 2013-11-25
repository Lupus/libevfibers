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

#include <evfibers/config.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>
#include <ev.h>
#ifdef FBR_EIO_ENABLED
# include <evfibers/eio.h>
#endif
#include <evfibers_private/fiber.h>

char small_msg[] = "Small test line\n\n";
char big_msg[] =
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

#define PROF_START \
	ev_now_update(EV_DEFAULT); \
	tm = ev_now(EV_DEFAULT);

#define PROF_PRINT(name) \
	ev_now_update(EV_DEFAULT); \
	printf("%s %f\n", name, ev_now(EV_DEFAULT) - tm);

#define PROF_ADD(var) \
	ev_now_update(EV_DEFAULT); \
	var += ev_now(EV_DEFAULT) - tm;

static ev_tstamp job_sync_open_trunc;
static ev_tstamp job_sync_small_msg;
static ev_tstamp job_sync_big_msg;
static ev_tstamp job_sync_close;

void do_job_sync()
{
	ssize_t retval;
	size_t offt;
	FILE *file;
	void *buf;
	ev_tstamp tm;

	PROF_START;
	file = fopen("/tmp/async.test", "w+");
	assert(file);
	retval = ftruncate(fileno(file), 0);
	assert(0 == retval);
	PROF_ADD(job_sync_open_trunc);


	/* Small message test */
	PROF_START;
	retval = fwrite(small_msg, sizeof(small_msg), 1, file);
	assert(1 == retval);

	retval = fsync(fileno(file));
	assert(0 == retval);

	retval = fseek(file, 0, SEEK_SET);
	assert(0 == retval);

	buf = malloc(sizeof(small_msg));
	assert(buf);

	retval = fread(buf, sizeof(small_msg), 1, file);
	assert(1 == retval);
	assert(!memcmp(small_msg, buf, sizeof(small_msg)));

	free(buf);
	PROF_ADD(job_sync_small_msg);


	/* Big message test */
	PROF_START;
	retval = ftell(file);
	assert(-1 != retval);

	offt = retval;

	retval = fwrite(big_msg, sizeof(big_msg), 1, file);
	assert(1 == retval);

	retval = fdatasync(fileno(file));
	assert(0 == retval);

	retval = fseek(file, offt, SEEK_SET);
	assert(0 == retval);

	buf = malloc(sizeof(big_msg));
	assert(buf);

	retval = fread(buf, sizeof(big_msg), 1, file);
	assert(1 == retval);
	assert(!memcmp(big_msg, buf, sizeof(big_msg)));

	free(buf);
	PROF_ADD(job_sync_big_msg);

	PROF_START;
	retval = fclose(file);
	assert(0 == retval);
	PROF_ADD(job_sync_close);
}

static ev_tstamp job_eio_open_trunc;
static ev_tstamp job_eio_small_msg;
static ev_tstamp job_eio_big_msg;
static ev_tstamp job_eio_close;

void do_job_eio(FBR_P)
{
	ssize_t retval;
	int fd;
	void *buf;
	ev_tstamp tm;

	PROF_START;
	fd = fbr_eio_open(FBR_A_ "/tmp/async.test", O_RDWR, 0, 0);
	assert(0 <= fd);
	retval = fbr_eio_ftruncate(FBR_A_ fd, 0, 0);
	assert(0 == retval);
	PROF_ADD(job_eio_open_trunc);


	/* Small message test */
	PROF_START;
	retval = fbr_eio_write(FBR_A_ fd, small_msg, sizeof(small_msg), 0, 0);
	assert(0 < retval);

	retval = fbr_eio_fsync(FBR_A_ fd, 0);
	assert(0 == retval);

	buf = malloc(sizeof(small_msg));
	assert(buf);

	retval = fbr_eio_read(FBR_A_ fd, buf, sizeof(small_msg), 0, 0);
	assert(0 <= retval);
	assert(!memcmp(small_msg, buf, sizeof(small_msg)));

	free(buf);
	PROF_ADD(job_eio_small_msg);


	/* Big message test */
	PROF_START;
	retval = fbr_eio_write(FBR_A_ fd, big_msg, sizeof(big_msg),
			sizeof(small_msg), 0);
	assert(0 < retval);

	retval = fbr_eio_fdatasync(FBR_A_ fd, 0);
	assert(0 == retval);

	buf = malloc(sizeof(big_msg));
	assert(buf);

	retval = fbr_eio_read(FBR_A_ fd, buf, sizeof(big_msg),
			sizeof(small_msg), 0);
	assert(0 < retval);
	assert(!memcmp(big_msg, buf, sizeof(big_msg)));

	free(buf);
	PROF_ADD(job_eio_big_msg);

	PROF_START;
	retval = fbr_eio_close(FBR_A_ fd, 0);
	assert(0 == retval);
	PROF_ADD(job_eio_close);
}

static ev_tstamp job_open_trunc;
static ev_tstamp job_small_msg;
static ev_tstamp job_big_msg;
static ev_tstamp job_close;

void do_job(FBR_P)
{
	ssize_t retval;
	size_t offt;
	struct fbr_async *as;
	void *buf;
	ev_tstamp tm;
	as = fbr_async_create(FBR_A);
#if 0
	retval = fbr_async_debug(FBR_A_ as);
	fail_unless(0 == retval);
#endif
	PROF_START;
	retval = fbr_async_fopen(FBR_A_ as, "/tmp/async.test", "w+");
	assert(0 == retval);
	retval = fbr_async_ftruncate(FBR_A_ as, 0);
	assert(0 == retval);
	PROF_ADD(job_open_trunc);


	/* Small message test */
	PROF_START;
	retval = fbr_async_fwrite(FBR_A_ as, small_msg, sizeof(small_msg));
	assert(1 == retval);

	retval = fbr_async_fsync(FBR_A_ as);
	assert(0 == retval);

	retval = fbr_async_fseek(FBR_A_ as, 0, SEEK_SET);
	assert(0 == retval);

	buf = malloc(sizeof(small_msg));
	assert(buf);

	retval = fbr_async_fread(FBR_A_ as, buf, sizeof(small_msg));
	assert(1 == retval);
	assert(!memcmp(small_msg, buf, sizeof(small_msg)));

	free(buf);
	PROF_ADD(job_small_msg);


	/* Big message test */
	PROF_START;
	retval = fbr_async_ftell(FBR_A_ as);
	assert(-1 != retval);

	offt = retval;

	retval = fbr_async_fwrite(FBR_A_ as, big_msg, sizeof(big_msg));
	assert(1 == retval);

	retval = fbr_async_fdatasync(FBR_A_ as);
	assert(0 == retval);

	retval = fbr_async_fseek(FBR_A_ as, offt, SEEK_SET);
	assert(0 == retval);

	buf = malloc(sizeof(big_msg));
	assert(buf);

	retval = fbr_async_fread(FBR_A_ as, buf, sizeof(big_msg));
	assert(1 == retval);
	assert(!memcmp(big_msg, buf, sizeof(big_msg)));

	free(buf);
	PROF_ADD(job_big_msg);

	PROF_START;
	retval = fbr_async_fclose(FBR_A_ as);
	assert(0 == retval);

	fbr_async_destroy(FBR_A_ as);
	PROF_ADD(job_close);
}

void do_job_stat_sync()
{
	ssize_t retval;
	struct stat stat_buf;
	struct stat stat_buf2;

	/* Stat test */
	memset(&stat_buf, 0x00, sizeof(stat_buf));
	memset(&stat_buf2, 0x00, sizeof(stat_buf));
	retval = stat("/tmp/async.test", &stat_buf);
	assert(0 == retval);
	memcpy(&stat_buf2, &stat_buf, sizeof(stat_buf));
}

void do_job_stat(FBR_P)
{
	ssize_t retval;
	struct fbr_async *as;
	struct stat stat_buf;

	as = fbr_async_create(FBR_A);
	/* Stat test */
	memset(&stat_buf, 0x00, sizeof(stat_buf));
	retval = fbr_async_fs_stat(FBR_A_ as, "/tmp/async.test", &stat_buf);
	assert(0 == retval);

	fbr_async_destroy(FBR_A_ as);
}

#ifdef FBR_EIO_ENABLED

void do_job_stat_eio(FBR_P)
{
	EIO_STRUCT_STAT statdata;
	int retval;

	retval = fbr_eio_stat(FBR_A_ "/tmp/async.test", 0, &statdata);
	assert(0 == retval);
}
#endif


static void io_fiber(FBR_P_ _unused_ void *_arg)
{
	int i;
	int repeats = 1000;
	ev_tstamp tm;

#if 1
	ev_now_update(EV_DEFAULT);
	tm = ev_now(EV_DEFAULT);
	for (i = 0; i < repeats; i++)
		do_job(FBR_A);
	ev_now_update(EV_DEFAULT);
	printf("do_job %f open_trunc: %f small_msg: %f, big_msg: %f, close: %f\n",
			ev_now(EV_DEFAULT) - tm, job_open_trunc, job_small_msg,
			job_big_msg, job_close);

	ev_now_update(EV_DEFAULT);
	tm = ev_now(EV_DEFAULT);
	for (i = 0; i < repeats; i++)
		do_job_eio(FBR_A);
	ev_now_update(EV_DEFAULT);
	printf("do_job_eio %f open_trunc: %f small_msg: %f, big_msg: %f, close: %f\n",
			ev_now(EV_DEFAULT) - tm, job_eio_open_trunc, job_eio_small_msg,
			job_eio_big_msg, job_eio_close);

	ev_now_update(EV_DEFAULT);
	tm = ev_now(EV_DEFAULT);
	for (i = 0; i < repeats; i++)
		do_job_sync();
	ev_now_update(EV_DEFAULT);
	printf("do_job_sync %f open_trunc: %f small_msg: %f, big_msg: %f, close: %f\n",
			ev_now(EV_DEFAULT) - tm, job_sync_open_trunc, job_sync_small_msg,
			job_sync_big_msg, job_sync_close);
#endif

	printf("\n");

#if 1
	ev_now_update(EV_DEFAULT);
	tm = ev_now(EV_DEFAULT);
	for (i = 0; i < repeats * 1000; i++)
		do_job_stat(FBR_A);
	ev_now_update(EV_DEFAULT);
	printf("do_job_stat %f\n", ev_now(EV_DEFAULT) - tm);
#endif

#if 1
# ifdef FBR_EIO_ENABLED
	ev_now_update(EV_DEFAULT);
	tm = ev_now(EV_DEFAULT);
	for (i = 0; i < repeats * 1000; i++)
		do_job_stat_eio(FBR_A);
	ev_now_update(EV_DEFAULT);
	printf("do_job_stat_eio %f\n", ev_now(EV_DEFAULT) - tm);
# endif
#endif

#if 1
	ev_now_update(EV_DEFAULT);
	tm = ev_now(EV_DEFAULT);
	for (i = 0; i < repeats * 1000; i++)
		do_job_stat_sync();
	ev_now_update(EV_DEFAULT);
	printf("do_job_stat_sync %f\n", ev_now(EV_DEFAULT) - tm);
#endif
}

int main()
{
	int retval;
	fbr_id_t fiber = FBR_ID_NULL;
	struct fbr_context context;
	signal(SIGPIPE, SIG_IGN);
	fbr_init(&context, EV_DEFAULT);
#ifdef FBR_EIO_ENABLED
	fbr_eio_init();
#endif

	fiber = fbr_create(&context, "io_fiber", io_fiber, NULL, 0);
	assert(!fbr_id_isnull(fiber));
	retval = fbr_transfer(&context, fiber);
	assert(0 == retval);

	ev_run(EV_DEFAULT, 0);
	fbr_destroy(&context);
	return 0;
}
