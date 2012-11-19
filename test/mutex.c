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
	fbr_id_t fibers[5] = {0};
	struct fbr_mutex *mutex = NULL;
	int flag = 0;
	int *flag_ptr = &flag;
	int retval;

	fbr_init(&context, EV_DEFAULT);

	mutex = fbr_mutex_create(&context);
	fail_if(NULL == mutex, NULL);

	fibers[0] = fbr_create(&context, "mutex1", mutex_fiber1, 0);
	fail_if(0 == fibers[0], NULL);
	fibers[1] = fbr_create(&context, "mutex2", mutex_fiber2, 0);
	fail_if(0 == fibers[1], NULL);
	fibers[2] = fbr_create(&context, "mutex3", mutex_fiber3, 0);
	fail_if(0 == fibers[2], NULL);
	fibers[3] = fbr_create(&context, "mutex4", mutex_fiber4, 0);
	fail_if(0 == fibers[3], NULL);

	/* ``mutex1'' fiber will aquire the mutex and yield */
	retval = fbr_call(&context, fibers[0], 1, fbr_arg_v(mutex));
	fail_unless(0 == retval, NULL);
	/* so we make sure that it holds the mutex */
	fail_unless(mutex->locked_by == fibers[0], NULL);

	/* ``mutex2'' fiber tries to lock and yields */
	retval = fbr_call(&context, fibers[1], 1, fbr_arg_v(mutex));
	fail_unless(0 == retval, NULL);
	/* so we make sure that ``mutex1'' still holds the mutex */
	fail_unless(mutex->locked_by == fibers[0], NULL);

	/* ``mutex3'' fiber blocks on mutex lock and yields */
	retval = fbr_call(&context, fibers[2], 2, fbr_arg_v(mutex),
			fbr_arg_v(flag_ptr));
	fail_unless(0 == retval, NULL);
	/* we still expect ``mutex1'' to hold the mutex */
	fail_unless(mutex->locked_by == fibers[0], NULL);

	/* ``mutex4'' fiber blocks on mutex lock as well */
	retval = fbr_call(&context, fibers[3], 1, fbr_arg_v(mutex));
	fail_unless(0 == retval, NULL);
	/* ``mutex'' shoud still hold the mutex */
	fail_unless(mutex->locked_by == fibers[0], NULL);

	/* ``mutex1'' releases the mutex */
	retval = fbr_call(&context, fibers[0], 0);
	fail_unless(0 == retval, NULL);
	/* now mutex should be acquired by the next fiber in the queue:
	 * ``mutex3''
	 */
	fail_unless(mutex->locked_by == fibers[2], NULL);

	/* Call callback twice to check it's reentrance */
	context.__p->mutex_async.cb(EV_DEFAULT, &context.__p->mutex_async, 0);
	context.__p->mutex_async.cb(EV_DEFAULT, &context.__p->mutex_async, 0);
	/* ensure that it's still locked by ``mutex3'' */
	fail_unless(mutex->locked_by == fibers[2], NULL);
	fail_if(0 == flag, NULL);

	context.__p->mutex_async.cb(EV_DEFAULT, &context.__p->mutex_async, 0);
	fail_unless(mutex->locked_by == fibers[2], NULL);
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
		fibers[i] = fbr_create(&context, "fiber_i", mutex_fiber5, 0);
		fail_if(0 == fibers[i], NULL);
		retval = fbr_call(&context, fibers[i], 2, fbr_arg_v(mutex),
				fbr_arg_v(flag_ptr));
		fail_unless(0 == retval, NULL);
	}

	extra = fbr_create(&context, "fiber_extra", mutex_fiber6, 0);
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
	tcase_add_test(tc_mutex, test_mutex);
	tcase_add_test(tc_mutex, test_mutex_evloop);
	return tc_mutex;
}
