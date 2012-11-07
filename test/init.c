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

#include "init.h"

START_TEST(test_init)
{
	struct fbr_context context;
	context.__p = NULL;
	fbr_init(&context, EV_DEFAULT);
	fail_if(NULL == context.__p, NULL);
	fbr_destroy(&context);
}
END_TEST

START_TEST(test_init_evloop)
{
	struct fbr_context context;
	fbr_init(&context, EV_DEFAULT);
	ev_run(EV_DEFAULT, 0);
	//Should return if we do not set up any unnecessary watchers
	fbr_destroy(&context);
}
END_TEST

TCase * init_tcase(void)
{
	TCase *tc_init = tcase_create ("Init");
	tcase_add_test(tc_init, test_init);
	tcase_add_test(tc_init, test_init_evloop);
	return tc_init;
}
