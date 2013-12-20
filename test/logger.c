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
