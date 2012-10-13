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

static void mutex_fiber1(FBR_P)
{
	struct fbr_mutex *mutex;
	struct fbr_call_info *info = NULL;
	fail_unless(fbr_next_call_info(FBR_A_ &info), NULL);
	fail_unless(1 == info->argc, NULL);
	mutex = info->argv[0].v;
	fail_unless(fbr_mutex_trylock(FBR_A_ mutex), NULL);
	fbr_yield(FBR_A);
	fail_unless(fbr_next_call_info(FBR_A_ &info), NULL);
	fail_unless(0 == info->argc, NULL);
	fbr_mutex_unlock(FBR_A_ mutex);
	fbr_yield(FBR_A);
}

static void mutex_fiber2(FBR_P)
{
	struct fbr_mutex *mutex;
	struct fbr_call_info *info = NULL;
	fail_unless(fbr_next_call_info(FBR_A_ &info), NULL);
	fail_unless(1 == info->argc, NULL);
	mutex = info->argv[0].v;
	fail_if(fbr_mutex_trylock(FBR_A_ mutex), NULL);
	fbr_yield(FBR_A);
}

static void mutex_fiber3(FBR_P)
{
	struct fbr_mutex *mutex;
	struct fbr_call_info *info = NULL;
	int *flag_ptr;
	fail_unless(fbr_next_call_info(FBR_A_ &info), NULL);
	fail_unless(2 == info->argc, NULL);
	mutex = info->argv[0].v;
	flag_ptr = info->argv[1].v;
	fbr_mutex_lock(FBR_A_ mutex);
	*flag_ptr = 1;
	fbr_yield(FBR_A);
}

static void mutex_fiber4(FBR_P)
{
	struct fbr_mutex *mutex;
	struct fbr_call_info *info = NULL;
	fail_unless(fbr_next_call_info(FBR_A_ &info), NULL);
	fail_unless(1 == info->argc, NULL);
	mutex = info->argv[0].v;
	fbr_mutex_lock(FBR_A_ mutex);
	fail("Should never get here");
}

START_TEST(test_mutex)
{
	struct fbr_context context;
	struct fbr_fiber *fibers[5] = {NULL};
	struct fbr_mutex *mutex = NULL;
	int flag = 0;
	int *flag_ptr = &flag;
	
	fbr_init(&context, EV_DEFAULT);
	
	mutex = fbr_mutex_create(&context);
	fail_if(NULL == mutex, NULL);
	
	fibers[0] = fbr_create(&context, "mutex1", mutex_fiber1, 0);
	fail_if(NULL == fibers[0], NULL);
	fibers[1] = fbr_create(&context, "mutex2", mutex_fiber2, 0);
	fail_if(NULL == fibers[1], NULL);
	fibers[2] = fbr_create(&context, "mutex3", mutex_fiber3, 0);
	fail_if(NULL == fibers[2], NULL);
	fibers[3] = fbr_create(&context, "mutex4", mutex_fiber4, 0);
	fail_if(NULL == fibers[3], NULL);

	fbr_call(&context, fibers[0], 1, fbr_arg_v(mutex));
	fail_unless(mutex->locked, NULL);
	
	fbr_call(&context, fibers[1], 1, fbr_arg_v(mutex));
	fail_unless(mutex->locked, NULL);
	
	fbr_call(&context, fibers[2], 2, fbr_arg_v(mutex), fbr_arg_v(flag_ptr));
	fail_unless(mutex->locked, NULL);
	fail_unless(mutex->pending->fiber == fibers[2], NULL);
	fail_unless(mutex->pending->next == NULL, NULL);
	fail_unless(mutex->next == NULL, NULL);
	fail_unless(mutex->prev == NULL, NULL);
	
	fbr_call(&context, fibers[3], 1, fbr_arg_v(mutex));
	fail_unless(mutex->locked, NULL);
	fail_unless(mutex->pending->fiber == fibers[2], NULL);
	fail_unless(mutex->pending->next->fiber == fibers[3], NULL);
	fail_unless(mutex->pending->next->next == NULL, NULL);
	fail_unless(mutex->next == NULL, NULL);
	fail_unless(mutex->prev == NULL, NULL);
	
	fbr_call(&context, fibers[0], 0);
	fail_unless(mutex->locked, NULL);
	fail_unless(mutex->pending->fiber == fibers[2], NULL);
	fail_unless(mutex->pending->next->fiber == fibers[3], NULL);
	fail_unless(mutex->pending->next->next == NULL, NULL);
	fail_unless(mutex->next == NULL, NULL);
	fail_if(mutex->prev == NULL, NULL);

	context.__p->mutex_async.cb(EV_DEFAULT, &context.__p->mutex_async, 0);
	fail_unless(mutex->locked, NULL);
	fail_unless(mutex->pending->fiber == fibers[3], NULL);
	fail_unless(mutex->pending->next == NULL, NULL);
	fail_unless(mutex->next == NULL, NULL);
	fail_if(mutex->prev == NULL, NULL);
	fail_if(0 == flag, NULL);

	fbr_mutex_destroy(&context, mutex);
	fbr_destroy(&context);
}
END_TEST

static void mutex_fiber5(FBR_P)
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

static void mutex_fiber6(FBR_P)
{
	struct fbr_fiber **fibers;
	int count;
	struct fbr_call_info *info = NULL;
	int i;
	const ev_tstamp sleep_interval = 0.01;
	fail_unless(fbr_next_call_info(FBR_A_ &info), NULL);
	fail_unless(2 == info->argc, NULL);
	fibers = info->argv[0].v;
	count = info->argv[1].i;
	for(;;) {
		for(i = 0; i < count; i++) {
			if(fbr_is_reclaimed(FBR_A_ fibers[i]))
				goto finish;
			else
				fbr_call_noinfo(FBR_A_ fibers[i], 0);
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
	struct fbr_fiber *fibers[fiber_count] = {NULL};
	struct fbr_fiber *extra = NULL;
	struct fbr_mutex *mutex = NULL;
	int flag = 0;
	int *flag_ptr = &flag;
	
	mutex = fbr_mutex_create(&context);
	fail_if(NULL == mutex, NULL);
	
	fbr_init(&context, EV_DEFAULT);
	for(i = 0; i < fiber_count; i++) {
		fibers[i] = fbr_create(&context, "fiber_i", mutex_fiber5, 0);
		fail_if(NULL == fibers[i], NULL);
		fbr_call(&context, fibers[i], 2, fbr_arg_v(mutex), fbr_arg_v(flag_ptr));
	}

	extra = fbr_create(&context, "fiber_extra", mutex_fiber6, 0);
	fail_if(NULL == extra, NULL);
	fbr_call(&context, extra, 2, fbr_arg_v(fibers), fbr_arg_i(fiber_count));
	
	ev_run(EV_DEFAULT, 0);

	fbr_mutex_destroy(&context, mutex);
	fbr_destroy(&context);
#undef fiber_count
}
END_TEST

TCase * mutex_tcase(void)
{
	TCase *tc_mutex = tcase_create ("Mutex");
	tcase_add_test(tc_mutex, test_mutex);
	tcase_add_test(tc_mutex, test_mutex_evloop);
	return tc_mutex;
}
