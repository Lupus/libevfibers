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

#include "async-wait.h"

struct fiber_arg {
	ev_async *async;
	fbr_id_t *fibers;
};

static int async_recvd = 0;

static void async_sender(FBR_P_ void *_arg)
{
	struct fiber_arg *arg = _arg;
	ev_async *async = arg->async;

	/* wait briefly to make sure other fiber blocks */
	fbr_sleep(FBR_A_ 1);

	/* send async event in libev to wake up other fiber */
	ev_async_send(fctx->__p->loop, async);
	return;
}

static void async_waiter(FBR_P_ void *_arg)
{
	struct fiber_arg *arg = _arg;
	ev_async *async = arg->async;

	fbr_async_wait(FBR_A_ async);

	async_recvd = 1;

	return;
}

static void async_handler_cb(EV_P_ ev_async *w, int revents)
{
	/* hush warnings */
	(void)w;
	(void)revents;

	async_recvd = 1;
	/* This is kind of a hack for testing purposes; make sure we break out of
	 * the underlying event loop
	 */
	ev_break(EV_A_ EVBREAK_ONE);

	return;
}

START_TEST(test_async_wait)
{
	struct fbr_context context;
	fbr_id_t fibers[2] = {
		FBR_ID_NULL,
		FBR_ID_NULL,
	};
	ev_async async;
	int retval;
	struct fiber_arg arg;

	fbr_init(&context, EV_DEFAULT);

	ev_async_init(&async, async_handler_cb);
	ev_async_start(EV_DEFAULT, &async);
	
	arg.fibers = fibers;
	arg.async = &async;

	fibers[0] = fbr_create(&context, "async_sender", async_sender, &arg, 0);
	fail_if(fbr_id_isnull(fibers[0]), NULL);
	fibers[1] = fbr_create(&context, "async_waiter", async_waiter, &arg, 0);
	fail_if(fbr_id_isnull(fibers[1]), NULL);

	retval = fbr_transfer(&context, fibers[1]);
	fail_unless(0 == retval, NULL);
	/* it should not have recieved async event yet; the next fiber will
	 * do that
	 */
	fail_unless(0 == async_recvd);

	retval = fbr_transfer(&context, fibers[0]);
	fail_unless(0 == retval, NULL);

	/* Run the event loop */
	ev_run(EV_DEFAULT, 0);

	fail_unless(1 == async_recvd);

	fbr_destroy(&context);
}
END_TEST

TCase * async_wait_tcase(void)
{
	TCase *tc_async_wait = tcase_create ("Async_wait");
	tcase_add_test(tc_async_wait, test_async_wait);
	return tc_async_wait;
}

