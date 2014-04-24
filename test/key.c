/********************************************************************

   Copyright 2013 Konstantin Olkhovskiy <lupus@oxnull.net>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

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
