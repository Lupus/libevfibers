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

#include "cond.h"

struct fiber_arg {
	struct fbr_mutex *mutex;
	struct fbr_cond_var *cond;
	int *flag_ptr;
};

static void cond_fiber1(FBR_P_ void *_arg)
{
	struct fiber_arg *arg = _arg;
	struct fbr_mutex *mutex = arg->mutex;
	struct fbr_cond_var *cond = arg->cond;
	int *flag_ptr = arg->flag_ptr;
	fbr_mutex_lock(FBR_A_ mutex);
	fbr_cond_wait(FBR_A_ cond, mutex);
	*flag_ptr += 1;
	fbr_mutex_unlock(FBR_A_ mutex);
}

START_TEST(test_cond_broadcast)
{
	struct fbr_context context;
	fbr_id_t fiber = 0;
	struct fbr_mutex *mutex = NULL;
	struct fbr_cond_var *cond = NULL;
	int flag = 0;
	int i;
	const int num_fibers = 100;
	int retval;
	struct fiber_arg arg = {
		.flag_ptr = &flag
	};

	fbr_init(&context, EV_DEFAULT);

	mutex = fbr_mutex_create(&context);
	fail_if(NULL == mutex, NULL);
	arg.mutex = mutex;

	cond = fbr_cond_create(&context);
	fail_if(NULL == cond, NULL);
	arg.cond = cond;

	for(i = 0; i < num_fibers; i++) {
		fiber = fbr_create(&context, "cond_i", cond_fiber1, &arg, 0);
		fail_if(0 == fiber);
		retval = fbr_transfer(&context, fiber);
		fail_unless(0 == retval, NULL);
	}

	fail_unless(flag == 0, NULL);

	fbr_cond_broadcast(&context, cond);
	fbr_cond_broadcast(&context, cond);

	ev_run(EV_DEFAULT, 0);

	fail_unless(flag == num_fibers, NULL);

	fbr_cond_destroy(&context, cond);
	fbr_mutex_destroy(&context, mutex);
	fbr_destroy(&context);
}
END_TEST

START_TEST(test_cond_signal)
{
	struct fbr_context context;
	fbr_id_t fiber = 0;
	struct fbr_mutex *mutex = NULL;
	struct fbr_cond_var *cond = NULL;
	int flag = 0;
	int i;
	const int num_fibers = 100;
	int retval;
	struct fiber_arg arg = {
		.flag_ptr = &flag
	};

	fbr_init(&context, EV_DEFAULT);

	mutex = fbr_mutex_create(&context);
	fail_if(NULL == mutex, NULL);
	arg.mutex = mutex;

	cond = fbr_cond_create(&context);
	fail_if(NULL == cond, NULL);
	arg.cond = cond;

	for(i = 0; i < num_fibers; i++) {
		fiber = fbr_create(&context, "cond_i", cond_fiber1, &arg, 0);
		fail_if(0 == fiber);
		retval = fbr_transfer(&context, fiber);
		fail_unless(0 == retval, NULL);
	}

	fail_unless(flag == 0, NULL);

	for(i = 0; i <= num_fibers; i++)
		fbr_cond_signal(&context, cond);

	ev_run(EV_DEFAULT, 0);

	fail_unless(flag == num_fibers, NULL);

	fbr_cond_destroy(&context, cond);
	fbr_mutex_destroy(&context, mutex);
	fbr_destroy(&context);
}
END_TEST

START_TEST(test_cond_bad_mutex)
{
	struct fbr_context context;
	struct fbr_mutex *mutex = NULL;
	struct fbr_cond_var *cond = NULL;
	int retval;

	fbr_init(&context, EV_DEFAULT);

	mutex = fbr_mutex_create(&context);
	fail_if(NULL == mutex, NULL);

	cond = fbr_cond_create(&context);
	fail_if(NULL == cond, NULL);

	retval = fbr_cond_wait(&context, cond, mutex);
	fail_unless(retval == -1., NULL);
	fail_unless(context.f_errno == FBR_EINVAL);

	fbr_cond_destroy(&context, cond);
	fbr_mutex_destroy(&context, mutex);
	fbr_destroy(&context);
}
END_TEST

struct fiber_arg2 {
	struct fbr_cond_var *cond1;
	struct fbr_mutex *mutex1;
	struct fbr_cond_var *cond2;
	struct fbr_mutex *mutex2;
};

static void cond_fiber_waiter(FBR_P_ void *_arg)
{
	struct fiber_arg2 *arg = _arg;
	struct fbr_ev_cond_var ev_c1;
	struct fbr_ev_cond_var ev_c2;
	struct fbr_ev_base *fb_events[3];
	int n_events;

	fbr_ev_cond_var_init(FBR_A_ &ev_c1, arg->cond1, arg->mutex1);
	fbr_ev_cond_var_init(FBR_A_ &ev_c2, arg->cond2, arg->mutex2);

	fb_events[0] = &ev_c1.ev_base;
	fb_events[1] = &ev_c2.ev_base;
	fb_events[2] = NULL;

	fbr_mutex_lock(FBR_A_ arg->mutex1);
	fbr_mutex_lock(FBR_A_ arg->mutex2);

	n_events = fbr_ev_wait(FBR_A_ fb_events);
	fail_unless(n_events > 0);
	fail_unless(ev_c1.ev_base.arrived);
	fail_unless(ev_c2.ev_base.arrived);

	fbr_mutex_unlock(FBR_A_ arg->mutex1);
	fbr_mutex_unlock(FBR_A_ arg->mutex2);
}

static void cond_fiber_signaller(FBR_P_ void *_arg)
{
	struct fiber_arg2 *arg = _arg;

	fbr_sleep(FBR_A_ 0.3);
	fbr_cond_signal(FBR_A_ arg->cond1);
	fbr_cond_signal(FBR_A_ arg->cond2);
}

START_TEST(test_two_conds)
{
	struct fbr_context context;
	fbr_id_t fiber_waiter = 0, fiber_signaller = 0;
	int retval;
	struct fiber_arg2 arg;

	fbr_init(&context, EV_DEFAULT);
	arg.cond1 = fbr_cond_create(&context);
	arg.mutex1 = fbr_mutex_create(&context);
	arg.cond2 = fbr_cond_create(&context);
	arg.mutex2 = fbr_mutex_create(&context);

	fiber_waiter = fbr_create(&context, "cond_waiter", cond_fiber_waiter, &arg, 0);
	fail_if(0 == fiber_waiter);
	retval = fbr_transfer(&context, fiber_waiter);
	fail_unless(0 == retval, NULL);

	fiber_signaller = fbr_create(&context, "cond_signaller", cond_fiber_signaller, &arg, 0);
	fail_if(0 == fiber_signaller);
	retval = fbr_transfer(&context, fiber_signaller);
	fail_unless(0 == retval, NULL);

	ev_run(EV_DEFAULT, 0);

	fbr_cond_destroy(&context, arg.cond1);
	fbr_mutex_destroy(&context, arg.mutex1);
	fbr_cond_destroy(&context, arg.cond2);
	fbr_mutex_destroy(&context, arg.mutex2);
	fbr_destroy(&context);
}
END_TEST

TCase * cond_tcase(void)
{
	TCase *tc_cond = tcase_create ("Cond");
	tcase_add_test(tc_cond, test_cond_broadcast);
	tcase_add_test(tc_cond, test_cond_signal);
	tcase_add_test(tc_cond, test_cond_bad_mutex);
	tcase_add_test(tc_cond, test_two_conds);
	return tc_cond;
}


