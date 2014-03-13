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
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ev.h>
#include <errno.h>
#include <evfibers_private/fiber.h>

struct fiber_arg {
	struct fbr_mutex mutex1;
	struct fbr_cond_var cond1;
	int cond1_set;
	struct fbr_mutex mutex2;
	struct fbr_cond_var cond2;
	int cond2_set;
	size_t count;
};

static void cond_fiber1(FBR_P_ void *_arg)
{
	struct fiber_arg *arg = _arg;
	for (;;) {
		arg->cond1_set = 1;
		fbr_cond_signal(FBR_A_ &arg->cond1);
		while (0 == arg->cond2_set) {
			fbr_mutex_lock(FBR_A_ &arg->mutex2);
			fbr_cond_wait(FBR_A_ &arg->cond2, &arg->mutex2);
			fbr_mutex_unlock(FBR_A_ &arg->mutex2);
		}
		arg->cond2_set = 0;
		arg->count++;
	}
}

static void cond_fiber2(FBR_P_ void *_arg)
{
	struct fiber_arg *arg = _arg;
	for (;;) {
		arg->cond2_set = 1;
		fbr_cond_signal(FBR_A_ &arg->cond2);
		while (0 == arg->cond1_set) {
			fbr_mutex_lock(FBR_A_ &arg->mutex1);
			fbr_cond_wait(FBR_A_ &arg->cond1, &arg->mutex1);
			fbr_mutex_unlock(FBR_A_ &arg->mutex1);
		}
		arg->cond1_set = 0;
		arg->count++;
	}
}

static void stats_fiber(FBR_P_ void *_arg)
{
	struct fiber_arg *arg = _arg;
	size_t last;
	size_t diff;
	int count = 0;
	int max_samples = 100;
	for (;;) {
		last = arg->count;
		fbr_sleep(FBR_A_ 1.0);
		diff = arg->count - last;
		printf("%zd\n", diff);
		if (count++ > max_samples) {
			ev_break(fctx->__p->loop, EVBREAK_ALL);
		}
	}
}

int main()
{
	struct fbr_context context;
	fbr_id_t fiber1, fiber2, fiber_stats;
	int retval;
	(void)retval;
	struct fiber_arg arg = {
		.count = 0
	};

	fbr_init(&context, EV_DEFAULT);

	fbr_mutex_init(&context, &arg.mutex1);
	fbr_mutex_init(&context, &arg.mutex2);

	fbr_cond_init(&context, &arg.cond1);
	fbr_cond_init(&context, &arg.cond2);

	fiber1 = fbr_create(&context, "fiber1", cond_fiber1, &arg, 0);
	assert(!fbr_id_isnull(fiber1));
	retval = fbr_transfer(&context, fiber1);
	assert(0 == retval);

	fiber2 = fbr_create(&context, "fiber2", cond_fiber2, &arg, 0);
	assert(!fbr_id_isnull(fiber2));
	retval = fbr_transfer(&context, fiber2);
	assert(0 == retval);

	fiber_stats = fbr_create(&context, "fiber_stats", stats_fiber, &arg, 0);
	assert(!fbr_id_isnull(fiber_stats));
	retval = fbr_transfer(&context, fiber_stats);
	assert(0 == retval);

	ev_run(EV_DEFAULT, 0);

	fbr_cond_destroy(&context, &arg.cond1);
	fbr_cond_destroy(&context, &arg.cond2);
	fbr_mutex_destroy(&context, &arg.mutex1);
	fbr_mutex_destroy(&context, &arg.mutex2);
	fbr_destroy(&context);
	return 0;
}
