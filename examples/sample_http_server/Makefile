all:
	gcc -O2 http_parser.c sample_http_server.c ../../build/libevfibers.a -o sample_http_server -lev -leio

clean:
	rm -f sample_http_server

run:
	./sample_http_server
