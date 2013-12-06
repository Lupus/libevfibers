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
#include <evfibers/fiber.h>

#include "key.h"

int x;

START_TEST(test_key)
{
	struct fbr_context context;
	fbr_id_t fiber;
	int retval;
	fbr_key_t key;
	void *val;

	fbr_init(&context, EV_DEFAULT);

	fiber = fbr_create(&context, "key_fiber", NULL, NULL, 0);
	fail_if(fbr_id_isnull(fiber), NULL);

	retval = fbr_key_create(&context, &key);
	fail_unless(0 == retval);
	retval = fbr_key_set(&context, fiber, key, &x);
	fail_unless(0 == retval);
	val = fbr_key_get(&context, fiber, key);
	fail_unless(val == &x);
	retval = fbr_key_delete(&context, key);
	fail_unless(0 == retval);

	retval = fbr_key_set(&context, fiber, key, &x);
	fail_unless(-1 == retval);
	val = fbr_key_get(&context, fiber, key);
	fail_unless(NULL == val);
	retval = fbr_key_delete(&context, key);
	fail_unless(-1 == retval);

	fbr_destroy(&context);

}
END_TEST


TCase * key_tcase(void)
{
	TCase *tc_key = tcase_create ("Key");
	tcase_add_test(tc_key, test_key);
	return tc_key;
}
