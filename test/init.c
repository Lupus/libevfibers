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
