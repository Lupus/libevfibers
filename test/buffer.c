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

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <ev.h>
#include <check.h>
#include <evfibers_private/fiber.h>

#include "buffer.h"

struct fiber_arg {
	struct fbr_buffer *buffer;
	size_t write_size;
	size_t count;
	uint64_t magic;
};

static void buffer_reader_fiber(FBR_P_ void *_arg)
{
	struct fiber_arg *arg = _arg;
	struct fbr_buffer *buffer = arg->buffer;
	size_t i;
	uint64_t *ptr;

	for (i = 0; i < 2 * arg->count; i++) {
		ptr = fbr_buffer_read_address(FBR_A_ buffer, arg->write_size);
		fail_if(NULL == ptr);
		fail_unless(*ptr == arg->magic);
		fbr_buffer_read_advance(FBR_A_ buffer);
	}

}

static void buffer_writer_fiber(FBR_P_ void *_arg)
{
	struct fiber_arg *arg = _arg;
	struct fbr_buffer *buffer = arg->buffer;
	size_t i;
	uint64_t *ptr;

	for (i = 0; i < arg->count; i++) {
		ptr = fbr_buffer_alloc_prepare(FBR_A_ buffer, arg->write_size);
		*ptr = arg->magic;
		fbr_buffer_alloc_commit(FBR_A_ buffer);
	}
}

START_TEST(test_buffer)
{
	struct fbr_context context;
	fbr_id_t reader = 0, writer1 = 0, writer2 = 0;
	int retval;
	struct fiber_arg arg;

	fbr_init(&context, EV_DEFAULT);

	arg.buffer = fbr_buffer_create(&context, 1);
	arg.write_size = fbr_buffer_free_bytes(&context, arg.buffer) / 3;
	arg.count = 1e3;
	arg.magic = 0xdeadbeef;

	reader = fbr_create(&context, "reader_buffer", buffer_reader_fiber, &arg, 0);
	fail_if(0 == reader, NULL);
	writer1 = fbr_create(&context, "writer_buffer_1", buffer_writer_fiber, &arg, 0);
	fail_if(0 == writer1, NULL);
	writer2 = fbr_create(&context, "writer_buffer_2", buffer_writer_fiber, &arg, 0);
	fail_if(0 == writer2, NULL);


	retval = fbr_transfer(&context, reader);
	fail_unless(0 == retval, NULL);
	retval = fbr_transfer(&context, writer1);
	fail_unless(0 == retval, NULL);
	retval = fbr_transfer(&context, writer2);
	fail_unless(0 == retval, NULL);


	ev_run(EV_DEFAULT, 0);

	fail_unless(fbr_is_reclaimed(&context, writer1));
	fail_unless(fbr_is_reclaimed(&context, writer2));
	fail_unless(fbr_is_reclaimed(&context, reader));

	fbr_destroy(&context);
}
END_TEST


START_TEST(test_buffer_basic)
{
	struct fbr_context context;
	struct fbr_buffer *buffer;
	const uint64_t count = 1e3;
	uint64_t i;
	uint64_t *ptr;

	fbr_init(&context, EV_DEFAULT);

	buffer = fbr_buffer_create(&context, count * sizeof(uint64_t));

	ptr = fbr_buffer_alloc_prepare(&context, buffer, 10 * sizeof(uint64_t));
	fail_if(NULL == ptr);
	fbr_buffer_alloc_abort(&context, buffer);

	for (i = 0; i < count; i++) {
		ptr = fbr_buffer_alloc_prepare(&context, buffer, sizeof(uint64_t));
		*ptr = i;
		fbr_buffer_alloc_commit(&context, buffer);
	}

	ptr = fbr_buffer_read_address(&context, buffer, sizeof(uint64_t));
	fail_if(NULL == ptr);
	fbr_buffer_read_discard(&context, buffer);

	for (i = 0; i < count; i++) {
		ptr = fbr_buffer_read_address(&context, buffer, sizeof(uint64_t));
		fail_if(NULL == ptr);
		fail_unless(*ptr == i);
		fbr_buffer_read_advance(&context, buffer);
	}

	fbr_buffer_free(&context, buffer);
	fbr_destroy(&context);
}
END_TEST


TCase * buffer_tcase(void)
{
	TCase *tc_buffer = tcase_create ("Buffer");
	tcase_add_test(tc_buffer, test_buffer_basic);
	tcase_add_test(tc_buffer, test_buffer);
	return tc_buffer;
}
