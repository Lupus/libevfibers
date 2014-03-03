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

#include <stdio.h>
#include <ev.h>
#include <check.h>
#include <evfibers_private/fiber.h>

#include "logger.h"

static void test_fiber(FBR_P_ _unused_ void *_arg)
{
	const char fname[] = "logget_test_fiber";
	fctx->logger->level = FBR_LOG_DEBUG;

	fprintf(stderr, "\n==== BEGIN LOGGER TEST ====\n\n");

	fbr_set_name(FBR_A_ fbr_self(FBR_A), fname);

	fbr_dump_stack(FBR_A_ fbr_log_d);
	fbr_dump_stack(FBR_A_ fbr_log_i);
	fbr_dump_stack(FBR_A_ fbr_log_n);
	fbr_dump_stack(FBR_A_ fbr_log_w);
	fbr_dump_stack(FBR_A_ fbr_log_e);

	fbr_log_d(FBR_A_ "%s", fbr_strerror(FBR_A_ FBR_SUCCESS));
	fbr_log_d(FBR_A_ "%s", fbr_strerror(FBR_A_ FBR_EINVAL));
	fbr_log_d(FBR_A_ "%s", fbr_strerror(FBR_A_ FBR_ENOFIBER));
	fbr_log_d(FBR_A_ "%s", fbr_strerror(FBR_A_ FBR_ESYSTEM));
	fbr_log_d(FBR_A_ "%s", fbr_strerror(FBR_A_ FBR_EBUFFERMMAP));
	fbr_log_d(FBR_A_ "%s", fbr_strerror(FBR_A_ FBR_ENOKEY));
	fbr_log_d(FBR_A_ "%s", fbr_strerror(FBR_A_ FBR_EPROTOBUF));
	fbr_log_d(FBR_A_ "%s", fbr_strerror(FBR_A_ FBR_EBUFFERNOSPACE));
	fbr_log_d(FBR_A_ "%s", fbr_strerror(FBR_A_ -1));

	fail_unless(!strcmp(fbr_get_name(FBR_A_ fbr_self(FBR_A)), fname));

	printf("\n==== END LOGGER TEST ====\n\n");
}

START_TEST(test_logger)
{
	struct fbr_context context;
	fbr_id_t test = FBR_ID_NULL;
	int retval;

	fbr_init(&context, EV_DEFAULT);

	fbr_enable_backtraces(&context, 0);
	fbr_enable_backtraces(&context, 1);

	test = fbr_create(&context, "test", test_fiber, NULL, 0);
	fail_if(fbr_id_isnull(test), NULL);

	retval = fbr_transfer(&context, test);
	fail_unless(0 == retval, NULL);

	fbr_destroy(&context);
}
END_TEST

TCase * logger_tcase(void)
{
	TCase *tc_logger = tcase_create ("Logger");
	tcase_add_test(tc_logger, test_logger);
	return tc_logger;
}
