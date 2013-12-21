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
