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

int main()
{
	int retval;
	struct fbr_context context;
	fbr_init(&context, EV_DEFAULT);
	const size_t count = 10000;
	const size_t repeats = 100;
	struct fbr_buffer *buffers;
	size_t i, j;

	signal(SIGPIPE, SIG_IGN);
	buffers = calloc(count, sizeof(struct fbr_buffer));

	for (j = 0; j < repeats; j++) {
		printf("Repeat #%zd...", j);
		for (i = 0; i < count; i++) {
			retval = fbr_buffer_init(&context, buffers + i, 0);
			if (retval) {
				fprintf(stderr, "error at count = %zd\n", i);
				fprintf(stderr, "fbr_buffer_init: %s\n",
						fbr_strerror(&context,
							context.f_errno));
				fprintf(stderr, "errno: %s\n", strerror(errno));
				exit(-1);
			}
		}

		for (i = 0; i < count; i++) {
			fbr_buffer_destroy(&context, buffers + i);
		}
		printf(" Done!\n");
	}

	fbr_destroy(&context);
	return 0;
}
