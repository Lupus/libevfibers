#include <stdio.h>
#include <time.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <err.h>
#include <stdlib.h>
#include <ev.h>
#include <evfibers/fiber.h>

#include "http_parser.h"

struct parser_arg {
	struct fbr_context *fctx;
	int fd;
	int finish;
};

static char response[] =
	"HTTP/1.1 200 OK\n"
	"Content-Type: text/plain\n"
	"\n"
	"Hello World";

int on_headers_complete(http_parser* parser)
{
	struct parser_arg *arg = parser->data;
	ssize_t retval;
	size_t size = strlen(response);
	retval = fbr_write_all(arg->fctx, arg->fd, response, size);
	if (retval != size)
		err(EXIT_FAILURE, "fbr_write_all");
	arg->finish = 1;
	return 0;
}

static void conn_handler(struct fbr_context *fctx, void *_arg)
{
	int fd = *(int *)_arg;
	time_t now;
	char *ct;
	http_parser_settings settings;
	http_parser parser;
	char buf[BUFSIZ];
	size_t nparsed;
	ssize_t retval;
	struct parser_arg parg;

	memset(&settings, 0x00, sizeof(settings));
	settings.on_headers_complete = on_headers_complete;
	http_parser_init(&parser, HTTP_REQUEST);
	parg.fctx = fctx;
	parg.fd = fd;
	parg.finish = 0;
	parser.data = &parg;

	for (;;) {
		retval = fbr_read(fctx, fd, buf, sizeof(buf));
		if (0 > retval)
			err(EXIT_FAILURE, "fbr_read");
		if (0 == retval)
			break;

		nparsed = http_parser_execute(&parser, &settings, buf, retval);

		if (nparsed != retval)
			errx(EXIT_FAILURE, "error parsing HTTP");

		if (parg.finish)
			break;
	}

	shutdown(fd, SHUT_RDWR);
	close(fd);
}

static void acceptor(struct fbr_context *fctx, void *_arg)
{
	int fd = *(int *)_arg;
	int client_fd;
	socklen_t peer_len;
	struct sockaddr_in peer_addr;
	fbr_id_t id;
	peer_len = sizeof(peer_addr);
	for (;;) {
		client_fd = fbr_accept(fctx, fd, (struct sockaddr *)&peer_addr,
				&peer_len);
		if (client_fd < 0)
			err(EXIT_FAILURE, "accept failed");
		id = fbr_create(fctx, "handler", conn_handler, &client_fd, 0);
		if (fbr_id_isnull(id))
			errx(EXIT_FAILURE, "unable to create a fiber");
		fbr_transfer(fctx, id);

	}
}

int main(int argc, char *argv[])
{
	struct sockaddr_in sar;
	struct protoent *pe;
	int sa, sw;
	int port;
	struct fbr_context fbr;
	int retval;
	int yes = 1;
	fbr_id_t ticker_id;
	fbr_id_t acceptor_id;

	fbr_init(&fbr, EV_DEFAULT);
	signal(SIGPIPE, SIG_IGN);

	pe = getprotobyname("tcp");
	sa = socket(AF_INET, SOCK_STREAM, pe->p_proto);
	if (0 > sa)
		err(EXIT_FAILURE, "socket");
	retval = setsockopt(sa, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	if (retval)
		err(EXIT_FAILURE, "setsockopt");
	sar.sin_family = AF_INET;
	sar.sin_addr.s_addr = INADDR_ANY;
	sar.sin_port = htons(12345);
	retval = bind(sa, (struct sockaddr *)&sar, sizeof(struct sockaddr_in));
	if (retval)
		err(EXIT_FAILURE, "bind");
	retval = listen(sa, 64);
	if (retval)
		err(EXIT_FAILURE, "listen");

	acceptor_id = fbr_create(&fbr, "acceptor", acceptor, &sa, 0);
	if (fbr_id_isnull(acceptor_id))
		errx(EXIT_FAILURE, "unable to create a fiber");
	fbr_transfer(&fbr, acceptor_id);

	ev_run(EV_DEFAULT, 0);
}
