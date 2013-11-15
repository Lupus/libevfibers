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
#include <limits.h>
#include <err.h>

#include <evfibers_private/fiber.h>
#include <evfibers_private/worker.pb-c.h>

/* #define WORKER_DEBUG */

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

#define syserror(result, literal_msg)                                     \
		do {                                                      \
		snprintf(msg_buf, sizeof(msg_buf), "%s: %s", literal_msg, \
			strerror(errno));                                 \
		result.error = msg_buf;                                   \
		result.sys_errno = errno;                                 \
		} while (0)

#define assert_file do {                            \
	if (NULL == ctx.file) {                     \
		result.error = "no file is opened"; \
		send_reply(&result);                \
		break;                              \
	}                                           \
} while (0)                                         \

#ifdef WORKER_DEBUG
#define worker_dump(format, ...) \
	fprintf(stderr, "WORKER DUMP: " format "\n", ##__VA_ARGS__)
#else
#define worker_dump(format, ...) ((void)0)
#endif

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
		retval = chdir(file_req->open->cwd);
		if (-1 == retval) {
			syserror(result, "unable to chdir to given cwd");
			send_reply(&result);
			break;
		}
		ctx.file = fopen(file_req->open->name, file_req->open->mode);
		if (NULL == ctx.file) {
			syserror(result, "unable to open a file");
			send_reply(&result);
			break;
		}
		worker_dump("opened file %s", file_req->open->name);
		send_reply(&result);
		break;
	case FILE_REQ_TYPE__Close:
		assert_file;
		retval = fclose(ctx.file);
		if (retval) {
			syserror(result, "unable to close the file");
			send_reply(&result);
			break;
		}
		ctx.file = NULL;
		worker_dump("closed file");
		send_reply(&result);
		break;
	case FILE_REQ_TYPE__Read:
		assert_file;
		buf = malloc(file_req->read->size);
		if (NULL == buf)
			err(EXIT_FAILURE, "malloc");
		retval = fread(buf, file_req->read->size, 1, ctx.file);
		if (1 == retval) {
			result.has_content = 1;
			result.content.data = (uint8_t *)buf;
			result.content.len = file_req->read->size;
		} else if (retval < 1) {
			if (feof(ctx.file)) {
				result.has_eof = 1;
				result.eof = 1;
			} else if (ferror(ctx.file)) {
				result.error = strerror(errno);
			}
		}
		worker_dump("read %zd blocks of size %zd, eof: %d,"
				" error: %s)", retval, file_req->read->size,
				result.eof, result.error);
		send_reply(&result);
		free(buf);
		break;
	case FILE_REQ_TYPE__Write:
		assert_file;
		retval = fwrite(file_req->write->content.data,
				file_req->write->content.len, 1, ctx.file);
		if (1 != retval) {
			if (ferror(ctx.file))
				syserror(result, "write failed");
			else
				result.error = "write failed";
		}
		worker_dump("wrote %zd blocks of size %zd, error: %s",
				retval, file_req->write->content.len,
				result.error);
		send_reply(&result);
		break;
	case FILE_REQ_TYPE__Flush:
		assert_file;
		retval = fflush(ctx.file);
		if (retval)
			syserror(result, "flush failed");
		worker_dump("fflushed the stream");
		send_reply(&result);
		break;
	case FILE_REQ_TYPE__Seek:
		assert_file;
		retval = fseek(ctx.file, file_req->seek->offset,
				file_req->seek->whence);
		if (retval)
			syserror(result, "seek failed");
		worker_dump("seeked offset %zd whence %d",
				file_req->seek->offset,
				file_req->seek->whence);
		send_reply(&result);
		break;
	case FILE_REQ_TYPE__Sync:
		assert_file;
		retval = fsync(fileno(ctx.file));
		if (-1 == retval)
			syserror(result, "sync failed");
		worker_dump("synced the stream");
		send_reply(&result);
		break;
	case FILE_REQ_TYPE__DataSync:
		assert_file;
		retval = fdatasync(fileno(ctx.file));
		if (-1 == retval)
			syserror(result, "datasync failed");
		worker_dump("datasynced the stream");
		send_reply(&result);
		break;
	case FILE_REQ_TYPE__Truncate:
		assert_file;
		retval = ftruncate(fileno(ctx.file), file_req->truncate->size);
		if (-1 == retval)
			syserror(result, "truncate failed");
		worker_dump("truncated to size %zd", file_req->truncate->size);
		send_reply(&result);
		break;
	case FILE_REQ_TYPE__Tell:
		assert_file;
		retval = ftell(ctx.file);
		result.has_retval = 1;
		result.retval = retval;
		if (-1 == retval)
			syserror(result, "tell failed");
		worker_dump("ftelled (pos: %zd)", retval);
		send_reply(&result);
		break;
	}
}

static void process_fs_req(FilesystemReq *fs_req)
{
	ssize_t retval;
	ReqResult result = REQ_RESULT__INIT;
	char msg_buf[256];
	char path_buf[PATH_MAX + 1] = {0};
	struct stat stat_buf;
	switch (fs_req->type) {
	case FILESYSTEM_REQ_TYPE__Stat:
		retval = chdir(fs_req->cwd);
		if (-1 == retval) {
			syserror(result, "unable to chdir to given cwd");
			send_reply(&result);
			break;
		}
		memset(&stat_buf, 0x00, sizeof(stat_buf));
		retval = stat(fs_req->path, &stat_buf);
		if (-1 == retval) {
			syserror(result, "unable to stat a file");
			send_reply(&result);
			break;
		}
		worker_dump("statted file %s", fs_req->path);
		result.content.data = (uint8_t *)&stat_buf;
		result.content.len = sizeof(stat_buf);
		result.has_content = 1;
		send_reply(&result);
		break;
	case FILESYSTEM_REQ_TYPE__RealPath:
		retval = chdir(fs_req->cwd);
		if (-1 == retval) {
			syserror(result, "unable to chdir to given cwd");
			send_reply(&result);
			break;
		}
		if (NULL == realpath(fs_req->path, path_buf)) {
			syserror(result, "unable to perform realpath");
			send_reply(&result);
			break;
		}
		worker_dump("realpathed file %s", fs_req->path);
		result.content.data = (uint8_t *)path_buf;
		result.content.len = strlen(path_buf) + 1;
		result.has_content = 1;
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
	case REQ_TYPE__FileSystem:
		process_fs_req(req->fs);
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
	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, SIG_IGN);
	signal(SIGHUP, SIG_IGN);
	buf_size = BUFSIZ;
	buf = malloc(buf_size);
	assert(buf);
	for (;;) {
		retval = read(STDIN_FILENO, &size, sizeof(size));
		if (-1 == retval)
			err(EXIT_FAILURE, "error reading stdin");
		if (0 == retval) {
			if (ctx.file)
				fclose(ctx.file);
			exit(0);
		}
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
