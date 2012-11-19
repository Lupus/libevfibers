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

static void reclaim_fiber1(_unused_ FBR_P)
{
	fail_unless(0 == fbr_parent(FBR_A), NULL);
	return;
}

static void reclaim_fiber2(_unused_ FBR_P)
{
	fail_unless(fbr_self(FBR_A) == test_fiber);
	fbr_disown(FBR_A_ new_parent);
	fail_unless(new_parent == fbr_parent(FBR_A), NULL);
	for (;;)
		fbr_yield(FBR_A);
}

static void reclaim_fiber3(_unused_ FBR_P)
{
	test_fiber = fbr_create(FBR_A_ "test_fiber", reclaim_fiber2, 0);
	fail_if(0 == test_fiber, NULL);
	fbr_yield(FBR_A);
	return;
}


START_TEST(test_disown)
{
	struct fbr_context context;
	fbr_id_t fiber = 0;
	int retval;

	fbr_init(&context, EV_DEFAULT);

	fiber = fbr_create(&context, "reclaim_fiber", reclaim_fiber3, 0);
	fail_if(0 == fiber, NULL);

	new_parent = fbr_create(&context, "new_fiber", reclaim_fiber1, 0);
	fail_if(0 == new_parent, NULL);

	retval = fbr_transfer(&context, fiber);
	fail_unless(0 == retval, NULL);

	fail_if(0 == test_fiber, NULL);
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
	fbr_id_t fiber = 0;
	fbr_id_t new_fiber = 0;
	int retval;

	fbr_init(&context, EV_DEFAULT);
	fiber = fbr_create(&context, "reclaim_fiber", reclaim_fiber1, 0);
	fail_if(0 == fiber, NULL);

	retval = fbr_transfer(&context, fiber);
	fail_unless(0 == retval, NULL);

	new_fiber = fbr_create(&context, "reclaim_fiber2", reclaim_fiber1, 0);
	fail_if(0 == new_fiber, NULL);
	/* should be same pointer */
	fail_unless((uint64_t)fiber == (uint64_t)new_fiber);
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

TCase * reclaim_tcase(void)
{
	TCase *tc_reclaim = tcase_create ("Reclaim");
	tcase_add_test(tc_reclaim, test_reclaim);
	tcase_add_test(tc_reclaim, test_disown);
	return tc_reclaim;
}
