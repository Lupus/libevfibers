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

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <err.h>

#include <evfibers_private/fiber.h>
#include <evfibers_private/worker.pb-c.h>

struct {
	FILE *file;
} ctx;

static void send_reply(ReqResult *result)
{
	void *msg_buf;
	size_t size;
	ssize_t retval;
	size = req_result__get_packed_size(result);
	msg_buf = malloc(size);
	if (NULL == msg_buf)
		err(EXIT_FAILURE, "malloc");
	req_result__pack(result, msg_buf);
	retval = write(STDOUT_FILENO, &size, sizeof(size));
	if (retval < (ssize_t)sizeof(size))
		err(EXIT_FAILURE, "write");
	retval = write(STDOUT_FILENO, msg_buf, size);
	if (retval < (ssize_t)size)
		err(EXIT_FAILURE, "write");
	free(msg_buf);
}

#define syserror(literal_msg) ({                                          \
		snprintf(msg_buf, sizeof(msg_buf), "%s: %s", literal_msg, \
			strerror(errno));                                 \
		msg_buf;                                                  \
		})

#define assert_file do {                            \
	if (NULL == ctx.file) {                     \
		result.error = "no file is opened"; \
		send_reply(&result);                \
		break;                              \
	}                                           \
} while (0)                                         \

static void process_file_req(FileReq *file_req)
{
	void *buf;
	ssize_t retval;
	ReqResult result = REQ_RESULT__INIT;
	char msg_buf[256];
	switch (file_req->type) {
	case FILE_REQ_TYPE__Open:
		if (ctx.file) {
			result.error = "another file is already opened";
			send_reply(&result);
			break;
		}
		ctx.file = fopen(file_req->open->name, file_req->open->mode);
		if (NULL == ctx.file) {
			result.error = syserror("unable to open a file");
			send_reply(&result);
			break;
		}
		send_reply(&result);
		break;
	case FILE_REQ_TYPE__Close:
		assert_file;
		retval = fclose(ctx.file);
		if (retval) {
			result.error = syserror("unable to close the file");
			send_reply(&result);
			break;
		}
		send_reply(&result);
		break;
	case FILE_REQ_TYPE__Read:
		assert_file;
		buf = malloc(file_req->read->size);
		if (NULL == buf)
			err(EXIT_FAILURE, "malloc");
		retval = fread(buf, 1, file_req->read->size, ctx.file);
		if (retval > 0) {
			result.has_content = 1;
			result.content.data = (uint8_t *)buf;
			result.content.len = retval;
		}
		if (retval < (ssize_t)file_req->read->size) {
			if (feof(ctx.file)) {
				result.has_eof = 1;
				result.eof = 1;
			} else if (ferror(ctx.file)) {
				result.error = strerror(errno);
			}
		}
		send_reply(&result);
		free(buf);
		break;
	case FILE_REQ_TYPE__Write:
		assert_file;
		retval = fwrite(file_req->write->content.data,
				1, file_req->write->content.len, ctx.file);
		result.has_retval = 1;
		result.retval = retval;
		if (retval < (ssize_t)file_req->write->content.len) {
			if (ferror(ctx.file))
				result.error = syserror("write failed");
			else
				result.error = "write failed";
		}
		send_reply(&result);
		break;
	case FILE_REQ_TYPE__Flush:
		assert_file;
		retval = fflush(ctx.file);
		if (retval)
			result.error = syserror("flush failed");
		send_reply(&result);
		break;
	case FILE_REQ_TYPE__Seek:
		assert_file;
		retval = fseek(ctx.file, file_req->seek->offset,
				file_req->seek->whence);
		if (retval)
			result.error = syserror("seek failed");
		send_reply(&result);
		break;
	case FILE_REQ_TYPE__Sync:
		assert_file;
		retval = fsync(fileno(ctx.file));
		if (-1 == retval)
			result.error = syserror("sync failed");
		send_reply(&result);
		break;
	case FILE_REQ_TYPE__DataSync:
		assert_file;
		retval = fdatasync(fileno(ctx.file));
		if (-1 == retval)
			result.error = syserror("datasync failed");
		send_reply(&result);
		break;
	case FILE_REQ_TYPE__Truncate:
		assert_file;
		retval = ftruncate(fileno(ctx.file), file_req->truncate->size);
		if (-1 == retval)
			result.error = syserror("truncate failed");
		send_reply(&result);
		break;
	case FILE_REQ_TYPE__Tell:
		assert_file;
		retval = ftell(ctx.file);
		result.has_retval = 1;
		result.retval = retval;
		if (-1 == retval)
			result.error = syserror("tell failed");
		send_reply(&result);
		break;
	}
}

static void process_req(void *buf, size_t size)
{
	ReqResult result = REQ_RESULT__INIT;
	volatile int loop = 1;
	Req *req;
	req = req__unpack(NULL, size, buf);
	if (NULL == req)
		errx(EXIT_FAILURE, "unable to unpack req");
	switch (req->type) {
	case REQ_TYPE__File:
		process_file_req(req->file);
		break;
	case REQ_TYPE__Debug:
		fprintf(stderr, "Worker pid is %d, waiting for a debugger...\n",
				getpid());
		while(loop)
			sleep(1);
		send_reply(&result);
		break;
	default:
		errx(EXIT_FAILURE, "unknown req type: %d", req->type);
	}
	req__free_unpacked(req, NULL);
}

int main(void)
{
	size_t size;
	ssize_t retval;
	void *buf;
	size_t buf_size;
	buf_size = BUFSIZ;
	buf = malloc(buf_size);
	assert(buf);
	for (;;) {
		retval = read(STDIN_FILENO, &size, sizeof(size));
		if (-1 == retval)
			err(EXIT_FAILURE, "error reading stdin");
		if (0 == retval)
			exit(0);
		if (size > buf_size) {
			while (size > buf_size)
				buf_size *= 2;
			buf = realloc(buf, buf_size);
			assert(buf);
		}
		retval = read(STDIN_FILENO, buf, size);
		if (-1 == retval)
			err(EXIT_FAILURE, "error reading stdin");
		if (0 == retval)
			exit(0);
		process_req(buf, size);
	}
}
