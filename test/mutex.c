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

struct fiber_arg {
	struct fbr_mutex *mutex;
	int *flag_ptr;
	fbr_id_t *fibers;
	int count;
};

static void mutex_fiber1(FBR_P_ void *_arg)
{
	struct fiber_arg *arg = _arg;
	struct fbr_mutex *mutex = arg->mutex;
	fail_unless(fbr_mutex_trylock(FBR_A_ mutex), NULL);
	fbr_yield(FBR_A);
	fbr_mutex_unlock(FBR_A_ mutex);
	fbr_yield(FBR_A);
}

static void mutex_fiber2(FBR_P_ void *_arg)
{
	struct fiber_arg *arg = _arg;
	struct fbr_mutex *mutex = arg->mutex;
	fail_if(fbr_mutex_trylock(FBR_A_ mutex), NULL);
	fbr_yield(FBR_A);
}

static void mutex_fiber3(FBR_P_ void *_arg)
{
	struct fiber_arg *arg = _arg;
	struct fbr_mutex *mutex = arg->mutex;
	int *flag_ptr = arg->flag_ptr;
	fbr_mutex_lock(FBR_A_ mutex);
	*flag_ptr = 1;
	fbr_yield(FBR_A);
}

static void mutex_fiber4(FBR_P_ void *_arg)
{
	struct fiber_arg *arg = _arg;
	struct fbr_mutex *mutex = arg->mutex;
	fbr_mutex_lock(FBR_A_ mutex);
	fail("Should never get here");
}

START_TEST(test_mutex)
{
	struct fbr_context context;
	fbr_id_t fibers[5] = {
		FBR_ID_NULL,
		FBR_ID_NULL,
		FBR_ID_NULL,
		FBR_ID_NULL,
		FBR_ID_NULL,
	};
	struct fbr_mutex mutex;
	int flag = 0;
	int retval;
	struct fiber_arg arg = {
		.flag_ptr = &flag
	};

	fbr_init(&context, EV_DEFAULT);

	fbr_mutex_init(&context, &mutex);
	arg.mutex = &mutex;

	fibers[0] = fbr_create(&context, "mutex1", mutex_fiber1, &arg, 0);
	fail_if(fbr_id_isnull(fibers[0]), NULL);
	fibers[1] = fbr_create(&context, "mutex2", mutex_fiber2, &arg, 0);
	fail_if(fbr_id_isnull(fibers[1]), NULL);
	fibers[2] = fbr_create(&context, "mutex3", mutex_fiber3, &arg, 0);
	fail_if(fbr_id_isnull(fibers[2]), NULL);
	fibers[3] = fbr_create(&context, "mutex4", mutex_fiber4, &arg, 0);
	fail_if(fbr_id_isnull(fibers[3]), NULL);

	/* ``mutex1'' fiber will aquire the mutex and yield */
	retval = fbr_transfer(&context, fibers[0]);
	fail_unless(0 == retval, NULL);
	/* so we make sure that it holds the mutex */
	fail_unless(fbr_id_eq(mutex.locked_by, fibers[0]), NULL);

	/* ``mutex2'' fiber tries to lock and yields */
	retval = fbr_transfer(&context, fibers[1]);
	fail_unless(0 == retval, NULL);
	/* so we make sure that ``mutex1'' still holds the mutex */
	fail_unless(fbr_id_eq(mutex.locked_by, fibers[0]), NULL);

	/* ``mutex3'' fiber blocks on mutex lock and yields */
	retval = fbr_transfer(&context, fibers[2]);
	fail_unless(0 == retval, NULL);
	/* we still expect ``mutex1'' to hold the mutex */
	fail_unless(fbr_id_eq(mutex.locked_by, fibers[0]), NULL);

	/* ``mutex4'' fiber blocks on mutex lock as well */
	retval = fbr_transfer(&context, fibers[3]);
	fail_unless(0 == retval, NULL);
	/* ``mutex'' shoud still hold the mutex */
	fail_unless(fbr_id_eq(mutex.locked_by, fibers[0]), NULL);

	/* ``mutex1'' releases the mutex */
	retval = fbr_transfer(&context, fibers[0]);
	fail_unless(0 == retval, NULL);
	/* now mutex should be acquired by the next fiber in the queue:
	 * ``mutex3''
	 */
	fail_unless(fbr_id_eq(mutex.locked_by, fibers[2]), NULL);

	/* Run the event loop once */
	ev_run(EV_DEFAULT, EVRUN_ONCE);

	/* ensure that it's still locked by ``mutex3'' */
	fail_unless(fbr_id_eq(mutex.locked_by, fibers[2]), NULL);
	fail_if(0 == flag, NULL);

	/* Run event loot to make sure async watcher stops itself */
	ev_run(EV_DEFAULT, 0);

	fbr_mutex_destroy(&context, &mutex);
	fbr_destroy(&context);
}
END_TEST

static void mutex_fiber5(FBR_P_ void *_arg)
{
	struct fiber_arg *arg = _arg;
	struct fbr_mutex *mutex = arg->mutex;
	int *flag_ptr = arg->flag_ptr;
	int i, old = -1;
	const int repeat = 10;
	const ev_tstamp sleep_interval = 0.01;
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

static void mutex_fiber6(FBR_P_ void *_arg)
{
	struct fiber_arg *arg = _arg;
	fbr_id_t *fibers = arg->fibers;
	int count = arg->count;
	int i;
	const ev_tstamp sleep_interval = 0.01;
	int retval;
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
	fbr_id_t fibers[fiber_count];
	fbr_id_t extra = FBR_ID_NULL;
	struct fbr_mutex mutex;
	int flag = 0;
	int retval;
	struct fiber_arg arg = {
		.flag_ptr = &flag,
		.fibers = fibers,
		.count = fiber_count
	};

	for (i = 0; i < fiber_count; i++)
		fibers[i] = FBR_ID_NULL;

	fbr_mutex_init(&context, &mutex);
	arg.mutex = &mutex;

	fbr_init(&context, EV_DEFAULT);
	for(i = 0; i < fiber_count; i++) {
		fibers[i] = fbr_create(&context, "fiber_i", mutex_fiber5, &arg, 0);
		fail_if(fbr_id_isnull(fibers[i]), NULL);
		retval = fbr_transfer(&context, fibers[i]);
		fail_unless(0 == retval, NULL);
	}

	extra = fbr_create(&context, "fiber_extra", mutex_fiber6, &arg, 0);
	fail_if(fbr_id_isnull(extra), NULL);
	retval = fbr_transfer(&context, extra);
	fail_unless(0 == retval, NULL);

	ev_run(EV_DEFAULT, 0);

	fbr_mutex_destroy(&context, &mutex);
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
