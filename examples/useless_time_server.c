#include <stdio.h>
#include <time.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <err.h>
#include <stdlib.h>
#include <ev.h>
#include <evfibers/fiber.h>

static void conn_handler(struct fbr_context *fctx, void *_arg)
{
	int fd = *(int *)_arg;
	time_t now;
	char *ct;

	now = time(NULL);
	ct = ctime(&now);
	fbr_write(fctx, fd, ct, strlen(ct));
	close(fd);
}

static void ticker(struct fbr_context *fctx, void *_arg)
{
	time_t now;
	char *ct;

	for (;;) {
		fbr_sleep(fctx, 5);
		now = time(NULL);
		ct = ctime(&now);
		ct[strlen(ct)-1] = '\0';
		printf("ticker: time: %s\n", ct);
	}
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
	fbr_id_t ticker_id;
	fbr_id_t acceptor_id;

	fbr_init(&fbr, EV_DEFAULT);
	signal(SIGPIPE, SIG_IGN);

	ticker_id = fbr_create(&fbr, "ticker", ticker, NULL, 0);
	if (fbr_id_isnull(ticker_id))
		errx(EXIT_FAILURE, "unable to create a fiber");
	fbr_transfer(&fbr, ticker_id);

	pe = getprotobyname("tcp");
	sa = socket(AF_INET, SOCK_STREAM, pe->p_proto);
	sar.sin_family = AF_INET;
	sar.sin_addr.s_addr = INADDR_ANY;
	sar.sin_port = htons(12345);
	bind(sa, (struct sockaddr *)&sar, sizeof(struct sockaddr_in));
	listen(sa, 10);

	acceptor_id = fbr_create(&fbr, "acceptor", acceptor, &sa, 0);
	if (fbr_id_isnull(acceptor_id))
		errx(EXIT_FAILURE, "unable to create a fiber");
	fbr_transfer(&fbr, acceptor_id);

	ev_run(EV_DEFAULT, 0);
}
