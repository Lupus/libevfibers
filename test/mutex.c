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

#include <ev.h>
#include <check.h>
#include <evfibers_private/fiber.h>

#include "mutex.h"

static void mutex_fiber5(FBR_P_ _unused_ void *_arg)
{
	struct fbr_mutex *mutex;
	struct fbr_call_info *info = NULL;
	int i, old = -1;
	const int repeat = 10;
	const ev_tstamp sleep_interval = 0.01;
	int *flag_ptr;
	fail_unless(fbr_next_call_info(FBR_A_ &info), NULL);
	fail_unless(2 == info->argc, NULL);
	mutex = info->argv[0].v;
	flag_ptr = info->argv[1].v;
	fbr_mutex_lock(FBR_A_ mutex);
	for(i = 0; i < 2 * repeat; i++) {
		if(old >= 0)
			fail_unless(*flag_ptr == old, NULL);
		if(i < repeat)
			*flag_ptr += 1;
		else
			*flag_ptr -= 1;
		old = *flag_ptr;
		fbr_sleep(FBR_A_ sleep_interval);
	}
	fbr_mutex_unlock(FBR_A_ mutex);
}

static void mutex_fiber6(FBR_P_ _unused_ void *_arg)
{
	fbr_id_t *fibers;
	int count;
	struct fbr_call_info *info = NULL;
	int i;
	const ev_tstamp sleep_interval = 0.01;
	int retval;
	fail_unless(fbr_next_call_info(FBR_A_ &info), NULL);
	fail_unless(2 == info->argc, NULL);
	fibers = info->argv[0].v;
	count = info->argv[1].i;
	for(;;) {
		for(i = 0; i < count; i++) {
			if(fbr_is_reclaimed(FBR_A_ fibers[i]))
				goto finish;
			else {
				retval = fbr_transfer(FBR_A_ fibers[i]);
				fail_unless(0 == retval, NULL);
			}
		}
		fbr_sleep(FBR_A_ sleep_interval);
	}
finish:
	return;
}

START_TEST(test_mutex_evloop)
{
#define fiber_count 10
	int i;
	struct fbr_context context;
	fbr_id_t fibers[fiber_count] = {0};
	fbr_id_t extra = 0;
	struct fbr_mutex *mutex = NULL;
	int flag = 0;
	int *flag_ptr = &flag;
	int retval;

	mutex = fbr_mutex_create(&context);
	fail_if(NULL == mutex, NULL);

	fbr_init(&context, EV_DEFAULT);
	for(i = 0; i < fiber_count; i++) {
		fibers[i] = fbr_create(&context, "fiber_i", mutex_fiber5, NULL, 0);
		fail_if(0 == fibers[i], NULL);
		retval = fbr_call(&context, fibers[i], 2, fbr_arg_v(mutex),
				fbr_arg_v(flag_ptr));
		fail_unless(0 == retval, NULL);
	}

	extra = fbr_create(&context, "fiber_extra", mutex_fiber6, NULL, 0);
	fail_if(0 == extra, NULL);
	retval = fbr_call(&context, extra, 2, fbr_arg_v(fibers),
			fbr_arg_i(fiber_count));
	fail_unless(0 == retval, NULL);

	ev_run(EV_DEFAULT, 0);

	fbr_mutex_destroy(&context, mutex);
	fbr_destroy(&context);
#undef fiber_count
}
END_TEST

TCase * mutex_tcase(void)
{
	TCase *tc_mutex = tcase_create ("Mutex");
	tcase_add_test(tc_mutex, test_mutex_evloop);
	return tc_mutex;
}
