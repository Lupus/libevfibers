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

#include "reclaim.h"

static fbr_id_t new_parent;
static fbr_id_t test_fiber;

static void reclaim_fiber1(_unused_ FBR_P_ _unused_ void *_arg)
{
	fail_unless(fbr_id_isnull(fbr_parent(FBR_A)));
	return;
}

static void reclaim_fiber2(_unused_ FBR_P_ _unused_ void *_arg)
{
	fail_unless(fbr_id_eq(fbr_self(FBR_A), test_fiber));
	fbr_disown(FBR_A_ new_parent);
	fail_unless(fbr_id_eq(new_parent, fbr_parent(FBR_A)));
	for (;;)
		fbr_yield(FBR_A);
}

static void reclaim_fiber3(_unused_ FBR_P_ _unused_ void *_arg)
{
	test_fiber = fbr_create(FBR_A_ "test_fiber", reclaim_fiber2, NULL, 0);
	fail_if(fbr_id_isnull(test_fiber));
	fbr_yield(FBR_A);
	return;
}


START_TEST(test_disown)
{
	struct fbr_context context;
	fbr_id_t fiber = FBR_ID_NULL;
	int retval;

	fbr_init(&context, EV_DEFAULT);

	fiber = fbr_create(&context, "reclaim_fiber", reclaim_fiber3, NULL, 0);
	fail_if(fbr_id_isnull(fiber), NULL);

	new_parent = fbr_create(&context, "new_fiber", reclaim_fiber1, NULL, 0);
	fail_if(fbr_id_isnull(new_parent), NULL);

	retval = fbr_transfer(&context, fiber);
	fail_unless(0 == retval, NULL);

	fail_if(fbr_id_isnull(test_fiber), NULL);
	fail_unless(0 == fbr_is_reclaimed(&context, test_fiber), NULL);

	retval = fbr_transfer(&context, test_fiber);
	fail_unless(0 == retval, NULL);

	retval = fbr_reclaim(&context, fiber);
	fail_unless(0 == retval, NULL);
	fail_unless(fbr_is_reclaimed(&context, fiber), NULL);

	retval = fbr_transfer(&context, test_fiber);
	fail_unless(0 == retval, NULL);

	retval = fbr_reclaim(&context, new_parent);
	fail_unless(0 == retval, NULL);
	fail_unless(fbr_is_reclaimed(&context, new_parent), NULL);
	fail_unless(fbr_is_reclaimed(&context, test_fiber), NULL);

	fbr_destroy(&context);
}
END_TEST

START_TEST(test_reclaim)
{
	struct fbr_context context;
	fbr_id_t fiber = FBR_ID_NULL;
	fbr_id_t new_fiber = FBR_ID_NULL;
	int retval;

	fbr_init(&context, EV_DEFAULT);
	fiber = fbr_create(&context, "reclaim_fiber", reclaim_fiber1, NULL, 0);
	fail_if(fbr_id_isnull(fiber), NULL);

	retval = fbr_transfer(&context, fiber);
	fail_unless(0 == retval, NULL);

	new_fiber = fbr_create(&context, "reclaim_fiber2", reclaim_fiber1, NULL, 0);
	fail_if(fbr_id_isnull(new_fiber), NULL);
	/* should be same pointer */
	fail_unless((0xFFFFFFFF & fiber) == (0xFFFFFFFF & new_fiber));
	/* old id should be invalid any more to avoid ABA problem */
	retval = fbr_transfer(&context, fiber);
	fail_unless(-1 == retval, NULL);
	fail_unless(FBR_ENOFIBER == context.f_errno, NULL);

	/* new fiber should be called without problems */
	retval = fbr_transfer(&context, new_fiber);
	fail_unless(0 == retval, NULL);
	/* but consequent call should fail as fiber is reclaimed */
	retval = fbr_transfer(&context, new_fiber);
	fail_unless(-1 == retval, NULL);
	fail_unless(FBR_ENOFIBER == context.f_errno, NULL);

	fbr_destroy(&context);
}
END_TEST

static void no_reclaim_fiber2(FBR_P_ _unused_ void *_arg)
{
	fbr_set_noreclaim(FBR_A_ fbr_self(FBR_A));
	fbr_sleep(FBR_A_ 1.5);
	fbr_set_reclaim(FBR_A_ fbr_self(FBR_A));
}

static void no_reclaim_fiber1(FBR_P_ _unused_ void *_arg)
{
	fbr_id_t fiber = FBR_ID_NULL;
	int retval;
	ev_tstamp ts1, ts2;

	fbr_sleep(FBR_A_ 0.1);

	fiber = fbr_create(FBR_A_ "no_reclaim_fiber2", no_reclaim_fiber2,
			NULL, 0);
	fail_if(fbr_id_isnull(fiber));

	retval = fbr_transfer(FBR_A_ fiber);
	fail_unless(0 == retval);

	ts1 = ev_now(EV_DEFAULT);
	retval = fbr_reclaim(FBR_A_ fiber);
	fail_unless(0 == retval);
	ts2 = ev_now(EV_DEFAULT);
	fbr_log_e(FBR_A_ "ts2 - ts1 = %f", ts2 - ts1);
	fail_unless(ts2 - ts1 > 1.5);
}

START_TEST(test_no_reclaim)
{
	struct fbr_context context;
	fbr_id_t fiber = FBR_ID_NULL;
	int retval;

	fbr_init(&context, EV_DEFAULT);
	fiber = fbr_create(&context, "no_reclaim_fiber", no_reclaim_fiber1,
			NULL, 0);
	fail_if(fbr_id_isnull(fiber));

	retval = fbr_transfer(&context, fiber);
	fail_unless(0 == retval, NULL);

	ev_run(EV_DEFAULT, 0);

	fbr_destroy(&context);
}
END_TEST

START_TEST(test_user_data)
{
	struct fbr_context context;
	fbr_id_t fiber = FBR_ID_NULL;
	int retval;
	void *ptr = (void *)0xdeadbeaf;

	fbr_init(&context, EV_DEFAULT);

	fiber = fbr_create(&context, "null_fiber", NULL, NULL, 0);
	fail_if(fbr_id_isnull(fiber));

	retval = fbr_set_user_data(&context, fiber, ptr);
	fail_unless(0 == retval);

	fail_if(ptr != fbr_get_user_data(&context, fiber));

	fbr_destroy(&context);
}
END_TEST


TCase * reclaim_tcase(void)
{
	TCase *tc_reclaim = tcase_create ("Reclaim");
	tcase_add_test(tc_reclaim, test_reclaim);
	tcase_add_test(tc_reclaim, test_no_reclaim);
	tcase_add_test(tc_reclaim, test_disown);
	tcase_add_test(tc_reclaim, test_user_data);
	return tc_reclaim;
}
