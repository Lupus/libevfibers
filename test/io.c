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

#include "io.h"

static void reader_fiber(FBR_P_ void *_arg)
{
	int fd = *(int *)_arg;
	const int buf_size = 10;
	char buf[buf_size];
	size_t retval;
	size_t count = 0;
	for (;;) {
		retval = fbr_read(FBR_A_ fd, buf, buf_size);
		if (0 == retval) {
			fail_unless(1000 == count);
			return;
		}
		fail_unless(retval > 0, NULL);
		count += retval;
	}
}

static void writer_fiber(FBR_P_ void *_arg)
{
	int fd = *(int *)_arg;
	int i;
	int retval;
	const int buf_size = 100;
	char buf[buf_size];
	memset(buf, 0x00, buf_size);
	for(i = 0; i < 10; i++) {
		retval = fbr_write(FBR_A_ fd, buf, buf_size);
		fail_if(retval != buf_size);
	}
	close(fd);
}


START_TEST(test_read_write)
{
	struct fbr_context context;
	fbr_id_t reader = FBR_ID_NULL, writer = FBR_ID_NULL;
	int fds[2];
	int retval;

	retval = pipe(fds);
	fail_unless(0 == retval);
	retval = fbr_fd_nonblock(&context, fds[0]);
	fail_unless(0 == retval);
	retval = fbr_fd_nonblock(&context, fds[1]);
	fail_unless(0 == retval);

	fbr_init(&context, EV_DEFAULT);

	reader = fbr_create(&context, "reader", reader_fiber, fds + 0, 0);
	fail_if(fbr_id_isnull(reader), NULL);
	writer = fbr_create(&context, "writer", writer_fiber, fds + 1, 0);
	fail_if(fbr_id_isnull(reader), NULL);

	retval = fbr_transfer(&context, reader);
	fail_unless(0 == retval, NULL);
	retval = fbr_transfer(&context, writer);
	fail_unless(0 == retval, NULL);

	ev_run(EV_DEFAULT, 0);

	fail_unless(fbr_is_reclaimed(&context, reader));
	fail_unless(fbr_is_reclaimed(&context, writer));

	fbr_destroy(&context);
}
END_TEST

START_TEST(test_read_write_premature)
{
	struct fbr_context context;
	fbr_id_t reader = FBR_ID_NULL;
	int fds[2];
	int retval;

	retval = pipe(fds);
	fail_unless(0 == retval);
	retval = fbr_fd_nonblock(&context, fds[0]);
	fail_unless(0 == retval);
	retval = fbr_fd_nonblock(&context, fds[1]);
	fail_unless(0 == retval);

	fbr_init(&context, EV_DEFAULT);

	reader = fbr_create(&context, "reader", reader_fiber, fds + 0, 0);
	fail_if(fbr_id_isnull(reader), NULL);

	retval = fbr_transfer(&context, reader);
	fail_unless(0 == retval, NULL);

	close(fds[1]);

	retval = fbr_reclaim(&context, reader);
	fail_unless(0 == retval, NULL);

	ev_run(EV_DEFAULT, 0);

	fail_unless(fbr_is_reclaimed(&context, reader));

	fbr_destroy(&context);
}
END_TEST

#define buf_size (1 * 1024 * 1024)
static void all_reader_fiber(FBR_P_ void *_arg)
{
	int fd = *(int *)_arg;
	char *buf = malloc(buf_size);
	size_t retval;
	retval = fbr_read_all(FBR_A_ fd, buf, buf_size);
	fail_unless(buf_size == retval, NULL);
	free(buf);
}

static void all_writer_fiber(FBR_P_ void *_arg)
{
	int fd = *(int *)_arg;
	size_t retval;
	char *buf = calloc(buf_size, 1);
	retval = fbr_write_all(FBR_A_ fd, buf, buf_size);
	fail_if(retval != buf_size, NULL);
	close(fd);
	free(buf);
}
#undef buf_size


START_TEST(test_read_write_all)
{
	struct fbr_context context;
	fbr_id_t reader = FBR_ID_NULL, writer = FBR_ID_NULL;
	int fds[2];
	int retval;

	retval = pipe(fds);
	fail_unless(0 == retval);
	retval = fbr_fd_nonblock(&context, fds[0]);
	fail_unless(0 == retval);
	retval = fbr_fd_nonblock(&context, fds[1]);
	fail_unless(0 == retval);

	fbr_init(&context, EV_DEFAULT);

	reader = fbr_create(&context, "reader_all", all_reader_fiber, fds + 0,
			0);
	fail_if(fbr_id_isnull(reader), NULL);
	writer = fbr_create(&context, "writer_all", all_writer_fiber, fds + 1,
			0);
	fail_if(fbr_id_isnull(reader), NULL);

	retval = fbr_transfer(&context, reader);
	fail_unless(0 == retval, NULL);
	retval = fbr_transfer(&context, writer);
	fail_unless(0 == retval, NULL);

	ev_run(EV_DEFAULT, 0);

	fail_unless(fbr_is_reclaimed(&context, reader));
	fail_unless(fbr_is_reclaimed(&context, writer));

	fbr_destroy(&context);
}
END_TEST

static void line_reader_fiber(FBR_P_ void *_arg)
{
	int fd = *(int *)_arg;
	const int buf_size = 34;
	char buf[buf_size];
	char *expected;
	size_t retval;

	expected = "Lorem ipsum dolor sit amet,\n";
	retval = fbr_readline(FBR_A_ fd, buf, buf_size);
	fail_unless(retval > 0);
	fail_unless(0 == strcmp(expected, buf), "``%s'' != ``%s''", expected, buf);

	expected = "consectetur adipiscing elit.\n";
	retval = fbr_readline(FBR_A_ fd, buf, buf_size);
	fail_unless(retval > 0);
	fail_unless(0 == strcmp(expected, buf), "``%s'' != ``%s''", expected, buf);

	expected = "Phasellus pharetra turpis eros,\n";
	retval = fbr_readline(FBR_A_ fd, buf, buf_size);
	fail_unless(retval > 0);
	fail_unless(0 == strcmp(expected, buf), "``%s'' != ``%s''", expected, buf);

	expected = "eu blandit nulla.\n";
	retval = fbr_readline(FBR_A_ fd, buf, buf_size);
	fail_unless(retval > 0);
	fail_unless(0 == strcmp(expected, buf), "``%s'' != ``%s''", expected, buf);

	expected = "Cras placerat egestas tortor,\n";
	retval = fbr_readline(FBR_A_ fd, buf, buf_size);
	fail_unless(retval > 0);
	fail_unless(0 == strcmp(expected, buf), "``%s'' != ``%s''", expected, buf);

	/* buffer is shorter than the whole line */
	expected = "vel ullamcorper turpis commodo vi";
	retval = fbr_readline(FBR_A_ fd, buf, buf_size);
	fail_unless(retval > 0);
	fail_unless(0 == strcmp(expected, buf), "``%s'' != ``%s''", expected, buf);

	expected = "In.";
	retval = fbr_readline(FBR_A_ fd, buf, buf_size);
	fail_unless(retval > 0);
	fail_unless(0 == strcmp(expected, buf), "``%s'' != ``%s''", expected, buf);
}

static void line_writer_fiber(FBR_P_ _unused_ void *_arg)
{
	int fd = *(int *)_arg;
	size_t retval;
	char *buf = "Lorem ipsum dolor sit amet,\n"
		"consectetur adipiscing elit.\n"
		"Phasellus pharetra turpis eros,\n"
		"eu blandit nulla.\n"
		"Cras placerat egestas tortor,\n"
		"vel ullamcorper turpis commodo vitae.\n"
		"In.";
	retval = fbr_write_all(FBR_A_ fd, buf, strlen(buf));
	fail_unless(retval > 0, NULL);
	close(fd);
}

START_TEST(test_read_line)
{
	struct fbr_context context;
	fbr_id_t reader = FBR_ID_NULL, writer = FBR_ID_NULL;
	int fds[2];
	int retval;

	retval = pipe(fds);
	fail_unless(0 == retval);
	retval = fbr_fd_nonblock(&context, fds[0]);
	fail_unless(0 == retval);
	retval = fbr_fd_nonblock(&context, fds[1]);
	fail_unless(0 == retval);

	fbr_init(&context, EV_DEFAULT);

	reader = fbr_create(&context, "reader_line", line_reader_fiber, fds + 0, 0);
	fail_if(fbr_id_isnull(reader), NULL);
	writer = fbr_create(&context, "writer_line", line_writer_fiber, fds + 1, 0);
	fail_if(fbr_id_isnull(reader), NULL);

	retval = fbr_transfer(&context, reader);
	fail_unless(0 == retval, NULL);
	retval = fbr_transfer(&context, writer);
	fail_unless(0 == retval, NULL);

	ev_run(EV_DEFAULT, 0);

	fail_unless(fbr_is_reclaimed(&context, reader));
	fail_unless(fbr_is_reclaimed(&context, writer));

	fbr_destroy(&context);
}
END_TEST

#define buf_size 1200
#define ADDRESS "127.0.0.1"
#define PORT 12345
#define count 10
static void udp_reader_fiber(FBR_P_ _unused_ void *_arg)
{
	int fd;
	int i;
	char *buf = malloc(buf_size);
	size_t retval;
	struct sockaddr_in addr;
	socklen_t addrlen;

	addr.sin_family = AF_INET;
	retval = inet_aton(ADDRESS, &addr.sin_addr);
	fail_if(0 == retval);
	addr.sin_port = PORT;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	fail_if(fd < 0);

	retval = fbr_fd_nonblock(FBR_A_ fd);
	fail_unless(0 == retval);

	retval = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (int[]){1}, sizeof(int));
	fail_unless(0 == retval);

	retval = bind(fd, (struct sockaddr *) &addr, sizeof(addr));
	fail_unless(0 == retval);

	for (i = 0; i < count; i++) {
		retval = fbr_recvfrom(FBR_A_ fd, buf, buf_size, 0,
				(struct	sockaddr *)&addr, &addrlen);
		fail_unless(retval == buf_size);
	}
	free(buf);
}

static void udp_writer_fiber(FBR_P_ _unused_ void *_arg)
{
	int fd;
	int i;
	char *buf = calloc(buf_size, 1);
	size_t retval;
	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(addr);

	addr.sin_family = AF_INET;
	retval = inet_aton(ADDRESS, &addr.sin_addr);
	fail_if(0 == retval);
	addr.sin_port = PORT;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	fail_if(fd < 0);

	retval = fbr_fd_nonblock(FBR_A_ fd);
	fail_unless(0 == retval);

	for (i = 0; i < count; i++) {
		retval = fbr_sendto(FBR_A_ fd, buf, buf_size, 0,
				(struct sockaddr *)&addr, addrlen);
		fail_unless(retval == buf_size);
	}
	close(fd);
	free(buf);
}

static void tcp_reader_fiber(FBR_P_ _unused_ void *_arg)
{
	int fd, client_fd;
	char *buf = malloc(buf_size);
	size_t retval;
	struct sockaddr_in addr, peer_addr;
	socklen_t addrlen;

	addr.sin_family = AF_INET;
	retval = inet_aton(ADDRESS, &addr.sin_addr);
	fail_if(0 == retval);
	addr.sin_port = PORT;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	fail_if(fd < 0);

	retval = fbr_fd_nonblock(FBR_A_ fd);
	fail_unless(0 == retval);

	retval = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (int[]){1}, sizeof(int));
	fail_unless(0 == retval);

	retval = bind(fd, (struct sockaddr *) &addr, sizeof(addr));
	fail_unless(0 == retval);

	retval = listen(fd, 10);
	fail_unless(0 == retval);

	addrlen = sizeof(peer_addr);
	client_fd = fbr_accept(FBR_A_ fd, (struct sockaddr *) &peer_addr, &addrlen);
	fail_if(0 > client_fd);

	retval = fbr_fd_nonblock(FBR_A_ client_fd);
	fail_unless(0 == retval);

	retval = fbr_read_all(FBR_A_ client_fd, buf, buf_size);
	fail_unless(retval == buf_size);

	close(client_fd);
	close(fd);
	free(buf);
}

static void tcp_writer_fiber(FBR_P_ _unused_ void *_arg)
{
	int fd;
	char *buf = calloc(buf_size, 1);
	size_t retval;
	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(addr);

	addr.sin_family = AF_INET;
	retval = inet_aton(ADDRESS, &addr.sin_addr);
	fail_if(0 == retval);
	addr.sin_port = PORT;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	fail_if(fd < 0);

	retval = fbr_fd_nonblock(FBR_A_ fd);
	fail_unless(0 == retval);

	retval = fbr_connect(FBR_A_ fd, (struct sockaddr *) &addr, addrlen);
	fail_unless(0 == retval);

	retval = fbr_write_all(FBR_A_ fd, buf, buf_size);
	fail_unless(retval == buf_size);

	close(fd);
	free(buf);
}
#undef PORT
#undef ADDRESS
#undef buf_size
#undef count


START_TEST(test_udp)
{
	struct fbr_context context;
	fbr_id_t reader = FBR_ID_NULL, writer = FBR_ID_NULL;
	int retval;

	fbr_init(&context, EV_DEFAULT);

	reader = fbr_create(&context, "reader_udp", udp_reader_fiber, NULL, 0);
	fail_if(fbr_id_isnull(reader), NULL);
	writer = fbr_create(&context, "writer_udp", udp_writer_fiber, NULL, 0);
	fail_if(fbr_id_isnull(reader), NULL);

	retval = fbr_transfer(&context, reader);
	fail_unless(0 == retval, NULL);
	retval = fbr_transfer(&context, writer);
	fail_unless(0 == retval, NULL);

	ev_run(EV_DEFAULT, 0);

	fail_unless(fbr_is_reclaimed(&context, reader));
	fail_unless(fbr_is_reclaimed(&context, writer));

	fbr_destroy(&context);
}
END_TEST

START_TEST(test_tcp)
{
	struct fbr_context context;
	fbr_id_t reader = FBR_ID_NULL, writer = FBR_ID_NULL;
	int retval;

	fbr_init(&context, EV_DEFAULT);

	reader = fbr_create(&context, "reader_tcp", tcp_reader_fiber, NULL, 0);
	fail_if(fbr_id_isnull(reader), NULL);
	writer = fbr_create(&context, "writer_tcp", tcp_writer_fiber, NULL, 0);
	fail_if(fbr_id_isnull(reader), NULL);

	retval = fbr_transfer(&context, reader);
	fail_unless(0 == retval, NULL);
	retval = fbr_transfer(&context, writer);
	fail_unless(0 == retval, NULL);

	ev_run(EV_DEFAULT, 0);

	fail_unless(fbr_is_reclaimed(&context, reader));
	fail_unless(fbr_is_reclaimed(&context, writer));

	fbr_destroy(&context);
}
END_TEST


TCase * io_tcase(void)
{
	TCase *tc_io = tcase_create ("IO");
	tcase_add_test(tc_io, test_read_write);
	tcase_add_test(tc_io, test_read_write_all);
	tcase_add_test(tc_io, test_read_line);
	tcase_add_test(tc_io, test_udp);
	tcase_add_test(tc_io, test_tcp);
	tcase_add_test(tc_io, test_read_write_premature);
	return tc_io;
}
