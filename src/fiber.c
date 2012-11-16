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

#include <sys/mman.h>
#include <assert.h>
#include <errno.h>
#include <utlist.h>
#include <stdio.h>
#include <err.h>
#include <valgrind/valgrind.h>

#include <evfibers_private/fiber.h>

#ifndef LIST_FOREACH_SAFE
#define LIST_FOREACH_SAFE(var, head, field, next_var)              \
	for ((var) = ((head)->lh_first);                           \
		(var) && ((next_var) = ((var)->field.le_next), 1); \
		(var) = (next_var))

#endif

#ifndef TAILQ_FOREACH_SAFE
#define TAILQ_FOREACH_SAFE(var, head, field, next_var)                 \
	for ((var) = ((head)->tqh_first);                              \
		(var) ? ({ (next_var) = ((var)->field.tqe_next); 1; }) \
		: 0;                                                   \
		(var) = (next_var))
#endif


#define ENSURE_ROOT_FIBER do {						\
	assert(fctx->__p->sp->fiber == &fctx->__p->root);		\
} while (0)

#define CURRENT_FIBER (fctx->__p->sp->fiber)
#define CURRENT_FIBER_ID (fbr_id_pack(CURRENT_FIBER))
#define CALLED_BY_ROOT ((fctx->__p->sp - 1)->fiber == &fctx->__p->root)

#define unpack_transfer_errno(ptr, id)					\
	do {								\
		if (-1 == fbr_id_unpack(fctx, ptr, id))			\
			return -1;					\
	} while (0)

#define return_success							\
	do {								\
		fctx->f_errno = FBR_SUCCESS;				\
		return 0;						\
	} while (0);

static fbr_id_t fbr_id_pack(struct fbr_fiber *fiber)
{
	return ((__uint128_t)fiber->id << 64) | (uint64_t)fiber;
}

static int fbr_id_unpack(FBR_P_ struct fbr_fiber **ptr, fbr_id_t id)
{
	struct fbr_fiber *fiber;
	uint64_t f_id;
	f_id = (uint64_t)(id >> 64);
	fiber = (struct fbr_fiber *)(uint64_t)id;
	if (fiber->id != f_id) {
		fctx->f_errno = FBR_ENOFIBER;
		return -1;
	}
	if (ptr)
		*ptr = fiber;
	return 0;
}

static void mutex_async_cb(EV_P_ ev_async *w, _unused_ int revents)
{
	struct fbr_context *fctx;
	struct fbr_mutex *mutex;
	int retval;
	fctx = (struct fbr_context *)w->data;

	ENSURE_ROOT_FIBER;
	TAILQ_FOREACH(mutex, &fctx->__p->mutexes, entries) {
		TAILQ_REMOVE(&fctx->__p->mutexes, mutex, entries);
		if (TAILQ_EMPTY(&fctx->__p->mutexes))
			ev_async_stop(EV_A_ &fctx->__p->mutex_async);

		retval = fbr_call_noinfo(FBR_A_ mutex->locked_by, 0);
		if (-1 == retval && FBR_ENOFIBER != fctx->f_errno) {
			fbr_log_e(FBR_A_ "libevfibers: unexpected error trying"
					" to call a fiber by id: %s",
					fbr_strerror(FBR_A_ fctx->f_errno));
		}
	}
}

static void pending_async_cb(EV_P_ ev_async *w, _unused_ int revents)
{
	struct fbr_context *fctx;
	struct fiber_id_tailq_i *item;
	fctx = (struct fbr_context *)w->data;
	int retval;

	ENSURE_ROOT_FIBER;

	if (TAILQ_EMPTY(&fctx->__p->pending_fibers)) {
		ev_async_stop(EV_A_ &fctx->__p->pending_async);
		return;
	}

	item = TAILQ_FIRST(&fctx->__p->pending_fibers);
	TAILQ_REMOVE(&fctx->__p->pending_fibers, item, entries);
	retval = fbr_call_noinfo(FBR_A_ item->id, 0);
	if (-1 == retval && FBR_ENOFIBER != fctx->f_errno) {
		fbr_log_e(FBR_A_ "libevfibers: unexpected error trying to call"
				" a fiber by id: %s",
				fbr_strerror(FBR_A_ fctx->f_errno));
	}
	if (TAILQ_EMPTY(&fctx->__p->pending_fibers))
		ev_async_stop(EV_A_ &fctx->__p->pending_async);
	else
		ev_async_send(EV_A_ &fctx->__p->pending_async);
}

static void *allocate_in_fiber(FBR_P_ size_t size, struct fbr_fiber *in)
{
	struct mem_pool *pool_entry;
	pool_entry = malloc(size + sizeof(struct mem_pool));
	if (NULL == pool_entry) {
		fbr_log_e(FBR_A_ "libevfibers: unable to allocate %zu bytes\n",
				size + sizeof(struct mem_pool));
		abort();
	}
	pool_entry->ptr = pool_entry;
	pool_entry->destructor = NULL;
	pool_entry->destructor_context = NULL;
	LIST_INSERT_HEAD(&in->pool, pool_entry, entries);
	return pool_entry + 1;
}

static void stdio_logger(struct fbr_logger *logger, enum fbr_log_level level,
		const char *format, va_list ap)
{
	if (level > logger->level)
		return;

	switch (level) {
		case FBR_LOG_ERROR:
			fprintf(stderr, "ERROR ");
			vfprintf(stderr, format, ap);
			fprintf(stderr, "\n");
			break;
		case FBR_LOG_WARNING:
			fprintf(stdout, "WARNING ");
			vfprintf(stdout, format, ap);
			fprintf(stderr, "\n");
			break;
		case FBR_LOG_NOTICE:
			fprintf(stdout, "NOTICE ");
			vfprintf(stdout, format, ap);
			fprintf(stderr, "\n");
			break;
		case FBR_LOG_INFO:
			fprintf(stdout, "INFO ");
			vfprintf(stdout, format, ap);
			fprintf(stderr, "\n");
			break;
		case FBR_LOG_DEBUG:
			fprintf(stdout, "DEBUG ");
			vfprintf(stdout, format, ap);
			fprintf(stderr, "\n");
			break;
		default:
			fprintf(stdout, "????? ");
			vfprintf(stdout, format, ap);
			fprintf(stderr, "\n");
			break;
	}
}

void fbr_init(FBR_P_ struct ev_loop *loop)
{
	struct fbr_fiber *root;
	struct fbr_logger *logger;

	fctx->__p = malloc(sizeof(struct fbr_context_private));
	LIST_INIT(&fctx->__p->reclaimed);
	LIST_INIT(&fctx->__p->root.children);
	LIST_INIT(&fctx->__p->root.pool);
	TAILQ_INIT(&fctx->__p->mutexes);
	TAILQ_INIT(&fctx->__p->pending_fibers);

	root = &fctx->__p->root;
	root->name = "root";
	fctx->__p->last_id = 0;
	root->id = fctx->__p->last_id++;
	coro_create(&root->ctx, NULL, NULL, NULL, 0);

	logger = allocate_in_fiber(FBR_A_ sizeof(struct fbr_logger), root);
	logger->logv = stdio_logger;
	logger->level = FBR_LOG_NOTICE;
	fctx->logger = logger;

	fctx->__p->sp = fctx->__p->stack;
	fctx->__p->sp->fiber = root;
	fill_trace_info(FBR_A_ &fctx->__p->sp->tinfo);
	fctx->__p->loop = loop;
	fctx->__p->mutex_async.data = fctx;
	fctx->__p->pending_async.data = fctx;
	fctx->__p->backtraces_enabled = 0;
	ev_async_init(&fctx->__p->mutex_async, mutex_async_cb);
	ev_async_init(&fctx->__p->pending_async, pending_async_cb);
}

const char *fbr_strerror(_unused_ FBR_P_ enum fbr_error_code code)
{
	switch (code) {
		case FBR_SUCCESS:
			return "Success";
		case FBR_EINVAL:
			return "Invalid argument";
		case FBR_ENOFIBER:
			return "No such fiber";
	}
	return "Unknown error";
}

void fbr_log_e(FBR_P_ const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	(*fctx->logger->logv)(fctx->logger, FBR_LOG_DEBUG, format, ap);
	va_end(ap);
}

void fbr_log_w(FBR_P_ const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	(*fctx->logger->logv)(fctx->logger, FBR_LOG_DEBUG, format, ap);
	va_end(ap);
}

void fbr_log_n(FBR_P_ const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	(*fctx->logger->logv)(fctx->logger, FBR_LOG_DEBUG, format, ap);
	va_end(ap);
}

void fbr_log_i(FBR_P_ const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	(*fctx->logger->logv)(fctx->logger, FBR_LOG_DEBUG, format, ap);
	va_end(ap);
}

void fbr_log_d(FBR_P_ const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	(*fctx->logger->logv)(fctx->logger, FBR_LOG_DEBUG, format, ap);
	va_end(ap);
}

static void fiber_id_tailq_i_destructor(void *ptr, void *context)
{
	struct fiber_id_tailq_i *item = ptr;
	struct fiber_id_tailq *head = context;
	TAILQ_REMOVE(head, item, entries);
}

static struct fiber_id_tailq_i *id_tailq_i_for(FBR_P_ struct fbr_fiber *fiber,
		struct fiber_id_tailq *head)
{
	struct fiber_id_tailq_i *item;
	item = allocate_in_fiber(FBR_A_ sizeof(struct fiber_id_tailq_i), fiber);
	item->id = fbr_id_pack(fiber);
	fbr_alloc_set_destructor(FBR_A_ item, fiber_id_tailq_i_destructor, head);
	return item;
}

static void reclaim_children(FBR_P_ struct fbr_fiber *fiber)
{
	struct fbr_fiber *f;
	LIST_FOREACH(f, &fiber->children, entries.children) {
		fbr_reclaim(FBR_A_ fbr_id_pack(f));
	}
}

void fbr_destroy(FBR_P)
{
	struct fbr_fiber *fiber, *x;

	ev_async_stop(fctx->__p->loop, &fctx->__p->mutex_async);

	reclaim_children(FBR_A_ &fctx->__p->root);

	LIST_FOREACH_SAFE(fiber, &fctx->__p->reclaimed, entries.reclaimed, x) {
		if (0 != munmap(fiber->stack, fiber->stack_size))
			err(EXIT_FAILURE, "munmap");
		free(fiber);
	}

	free(fctx->__p);
}

void fbr_enable_backtraces(FBR_P_ int enabled)
{
	if (enabled)
		fctx->__p->backtraces_enabled = 1;
	else
		fctx->__p->backtraces_enabled = 0;

}

static void ev_wakeup_io(_unused_ EV_P_ ev_io *w, _unused_ int event)
{
	struct fbr_context *fctx;
	struct fbr_fiber *fiber;
	fiber = container_of(w, struct fbr_fiber, w_io);
	fctx = (struct fbr_context *)w->data;
	int retval;

	ENSURE_ROOT_FIBER;
	if (1 != fiber->w_io_expected) {
		fbr_log_e(FBR_A_ "libevfibers: fiber ``%s'' is about to be"
				" woken up by an io event but it does not"
				" expect this.", fiber->name);
		fbr_log_e(FBR_A_ "libevfibers: last registered io request for "
				"this fiber was:");
		fbr_log_e(FBR_A_ "--- begin trace ---");
		print_trace_info(FBR_A_ &fiber->w_io_tinfo, fbr_log_e);
		fbr_log_e(FBR_A_ "--- end trace ---");
		abort();
	}

	retval = fbr_call_noinfo(FBR_A_ fbr_id_pack(fiber), 0);
	if (0 == retval)
		return;

	fbr_log_e(FBR_A_ "libevfibers: failed to call fiber ``%s'': %s",
			fiber->name, fbr_strerror(FBR_A_ fctx->f_errno));
	abort();
}

static void ev_wakeup_timer(_unused_ EV_P_ ev_timer *w, _unused_ int event)
{
	struct fbr_context *fctx;
	struct fbr_fiber *fiber;
	fiber = container_of(w, struct fbr_fiber, w_timer);
	fctx = (struct fbr_context *)w->data;
	int retval;

	ENSURE_ROOT_FIBER;
	if (1 != fiber->w_timer_expected) {
		fbr_log_e(FBR_A_ "libevfibers: fiber ``%s'' is about to be"
				" woken up by a timer event but it does not"
				" expect this.", fiber->name);
		fbr_log_e(FBR_A_ "libevfibers: last registered timer request"
				" for this fiber was:");
		fbr_log_e(FBR_A_ "--- begin trace ---");
		print_trace_info(FBR_A_ &fiber->w_io_tinfo, fbr_log_e);
		fbr_log_e(FBR_A_ "--- end trace ---");
		abort();
	}

	retval = fbr_call_noinfo(FBR_A_ fbr_id_pack(fiber), 0);
	if (0 == retval)
		return;

	fbr_log_e(FBR_A_ "libevfibers: failed to call fiber ``%s'': %s",
			fiber->name, fbr_strerror(FBR_A_ fctx->f_errno));
	abort();
}

static void fbr_free_in_fiber(_unused_ FBR_P_ _unused_ struct fbr_fiber *fiber,
		void *ptr, int destructor)
{
	struct mem_pool *pool_entry = NULL;
	if (NULL == ptr)
		return;
	pool_entry = (struct mem_pool *)ptr - 1;
	if (pool_entry->ptr != pool_entry) {
		fbr_log_e(FBR_A_ "libevfibers: address %p does not look like "
				"fiber memory pool entry", ptr);
		if (!RUNNING_ON_VALGRIND)
			abort();
	}
	LIST_REMOVE(pool_entry, entries);
	if (destructor && pool_entry->destructor)
		pool_entry->destructor(ptr, pool_entry->destructor_context);
	free(pool_entry);
}

static void fiber_cleanup(FBR_P_ struct fbr_fiber *fiber)
{
	struct mem_pool *p, *x;
	/* coro_destroy(&fiber->ctx); */
	ev_io_stop(fctx->__p->loop, &fiber->w_io);
	ev_timer_stop(fctx->__p->loop, &fiber->w_timer);
	LIST_REMOVE(fiber, entries.children);
	LIST_FOREACH_SAFE(p, &fiber->pool, entries, x) {
		fbr_free_in_fiber(FBR_A_ fiber, p + 1, 1);
	}
}

int fbr_reclaim(FBR_P_ fbr_id_t id)
{
	struct fbr_fiber *fiber;

	unpack_transfer_errno(&fiber, id);

	fill_trace_info(FBR_A_ &fiber->reclaim_tinfo);
	reclaim_children(FBR_A_ fiber);
	fiber_cleanup(FBR_A_ fiber);
	fiber->id = fctx->__p->last_id++;
	LIST_INSERT_HEAD(&fctx->__p->reclaimed, fiber, entries.reclaimed);

	return_success;
}

int fbr_is_reclaimed(_unused_ FBR_P_ fbr_id_t id)
{
	if (0 == fbr_id_unpack(FBR_A_ NULL, id))
		return 0;
	return 1;
}

fbr_id_t fbr_self(FBR_P)
{
	return CURRENT_FIBER_ID;
}

static void fiber_prepare(FBR_P_ struct fbr_fiber *fiber)
{
	ev_init(&fiber->w_io, ev_wakeup_io);
	ev_init(&fiber->w_timer, ev_wakeup_timer);
	fiber->w_io.data = FBR_A;
	fiber->w_timer.data = FBR_A;
}

static void call_wrapper(FBR_P_ _unused_ void (*func) (FBR_P))
{
	int retval;
	struct fbr_fiber *fiber = CURRENT_FIBER;
	fiber_prepare(FBR_A_ fiber);

	fiber->func(FBR_A);

	retval = fbr_reclaim(FBR_A_ fbr_id_pack(fiber));
	assert(0 == retval);
	fbr_yield(FBR_A);
	assert(NULL);
}

struct fbr_fiber_arg fbr_arg_i(int i)
{
	struct fbr_fiber_arg arg;
	arg.i = i;
	return arg;
}

struct fbr_fiber_arg fbr_arg_v(void *v)
{
	struct fbr_fiber_arg arg;
	arg.v = v;
	return arg;
}

int fbr_vcall(FBR_P_ fbr_id_t id, int leave_info, int argnum, va_list ap)
{
	struct fbr_fiber *callee;
	struct fbr_fiber *caller = fctx->__p->sp->fiber;
	int i;
	struct fbr_call_info *info;

	if (argnum >= FBR_MAX_ARG_NUM) {
		fbr_log_n(FBR_A_ "libevfibers: attempt to pass %d argumens"
				" while FBR_MAX_ARG_NUM is %d", argnum,
				FBR_MAX_ARG_NUM);
		fctx->f_errno = FBR_EINVAL;
		return -1;
	}

	unpack_transfer_errno(&callee, id);

	fctx->__p->sp++;

	fctx->__p->sp->fiber = callee;
	fill_trace_info(FBR_A_ &fctx->__p->sp->tinfo);

	if (0 == leave_info) {
		coro_transfer(&caller->ctx, &callee->ctx);
		return_success;
	}

	info = fbr_alloc(FBR_A_ sizeof(struct fbr_call_info));
	info->caller = id;
	info->argc = argnum;
	for (i = 0; i < argnum; i++)
		info->argv[i] = va_arg(ap, struct fbr_fiber_arg);

	DL_APPEND(callee->call_list, info);
	callee->call_list_size++;
	if (callee->call_list_size >= FBR_CALL_LIST_WARN) {
		fbr_log_n(FBR_A_ "libevfibers: call list for ``%s'' contains"
				" %zu elements, which looks suspicious. Is"
				" anyone fetching the calls?", callee->name,
				callee->call_list_size);
		fbr_dump_stack(FBR_A_ fbr_log_n);
	}

	coro_transfer(&caller->ctx, &callee->ctx);

	return_success;
}

int fbr_call_noinfo(FBR_P_ fbr_id_t callee, int argnum, ...)
{
	int retval;
	va_list ap;
	va_start(ap, argnum);
	retval = fbr_vcall(FBR_A_ callee, 0, argnum, ap);
	va_end(ap);
	return retval;
}

int fbr_call(FBR_P_ fbr_id_t callee, int argnum, ...)
{
	int retval;
	va_list ap;
	va_start(ap, argnum);
	retval = fbr_vcall(FBR_A_ callee, 1, argnum, ap);
	va_end(ap);
	return retval;
}

int fbr_next_call_info(FBR_P_ struct fbr_call_info **info_ptr)
{
	struct fbr_fiber *fiber = CURRENT_FIBER;
	struct fbr_call_info *tmp;

	if (NULL == fiber->call_list)
		return 0;
	tmp = fiber->call_list;
	DL_DELETE(fiber->call_list, fiber->call_list);
	fiber->call_list_size--;

	if (NULL == info_ptr)
		fbr_free(FBR_A_ tmp);
	else {
		if (NULL != *info_ptr)
			fbr_free(FBR_A_ *info_ptr);
		*info_ptr = tmp;
	}
	return 1;
}

void fbr_yield(FBR_P)
{
	struct fbr_fiber *callee = fctx->__p->sp->fiber;
	struct fbr_fiber *caller = (--fctx->__p->sp)->fiber;
	coro_transfer(&callee->ctx, &caller->ctx);
}

static void io_start(FBR_P_ struct fbr_fiber *fiber, int fd, int events)
{
	ev_io_set(&fiber->w_io, fd, events);
	ev_io_start(fctx->__p->loop, &fiber->w_io);
	assert(0 == fiber->w_io_expected);
	fiber->w_io_expected = 1;
	fill_trace_info(FBR_A_ &fiber->w_io_tinfo);
}

static void io_stop(FBR_P_ struct fbr_fiber *fiber)
{
	assert(1 == fiber->w_io_expected);
	fiber->w_io_expected = 0;
	ev_io_stop(fctx->__p->loop, &fiber->w_io);
}

ssize_t fbr_read(FBR_P_ int fd, void *buf, size_t count)
{
	struct fbr_fiber *fiber = CURRENT_FIBER;
	ssize_t r;

	io_start(FBR_A_ fiber, fd, EV_READ);
	fbr_yield(FBR_A);
	if (!CALLED_BY_ROOT) {
		errno = EINTR;
		r = -1;
		goto finish;
	}
	do {
		r = read(fd, buf, count);
	} while (-1 == r && EINTR == errno);

finish:
	io_stop(FBR_A_ fiber);
	return r;
}

ssize_t fbr_read_all(FBR_P_ int fd, void *buf, size_t count)
{
	struct fbr_fiber *fiber = CURRENT_FIBER;
	ssize_t r;
	size_t done = 0;

	io_start(FBR_A_ fiber, fd, EV_READ);
	while (count != done) {
next:
		fbr_yield(FBR_A);
		if (!CALLED_BY_ROOT)
			continue;
		for (;;) {
			r = read(fd, buf + done, count - done);
			if (-1 == r) {
				switch (errno) {
					case EINTR:
						continue;
					case EAGAIN:
						goto next;
					default:
						goto error;
				}
			}
			break;
		}
		if (0 == r)
			break;
		done += r;
	}
	io_stop(FBR_A_ fiber);
	return (ssize_t)done;

error:
	io_stop(FBR_A_ fiber);
	return -1;
}

ssize_t fbr_readline(FBR_P_ int fd, void *buffer, size_t n)
{
	ssize_t num_read;
	size_t total_read;
	char *buf;
	char ch;

	if (n <= 0 || buffer == NULL) {
		errno = EINVAL;
		return -1;
	}

	buf = buffer;

	total_read = 0;
	for (;;) {
		num_read = fbr_read(FBR_A_ fd, &ch, 1);

		if (num_read == -1) {
			if (errno == EINTR)
				continue;
			else
				return -1;

		} else if (num_read == 0) {
			if (total_read == 0)
				return 0;
			else
				break;

		} else {
			if (total_read < n - 1) {
				total_read++;
				*buf++ = ch;
			}

			if (ch == '\n')
				break;
		}
	}

	*buf = '\0';
	return total_read;
}

ssize_t fbr_write(FBR_P_ int fd, const void *buf, size_t count)
{
	struct fbr_fiber *fiber = CURRENT_FIBER;
	ssize_t r;

	io_start(FBR_A_ fiber, fd, EV_WRITE);
	fbr_yield(FBR_A);
	if (!CALLED_BY_ROOT) {
		errno = EINTR;
		r = -1;
		goto finish;
	}
	do {
		r = write(fd, buf, count);
	} while (-1 == r && EINTR == errno);

finish:
	io_stop(FBR_A_ fiber);
	return r;
}

ssize_t fbr_write_all(FBR_P_ int fd, const void *buf, size_t count)
{
	struct fbr_fiber *fiber = CURRENT_FIBER;
	ssize_t r;
	size_t done = 0;

	io_start(FBR_A_ fiber, fd, EV_WRITE);
	while (count != done) {
next:
		fbr_yield(FBR_A);
		if (!CALLED_BY_ROOT)
			continue;
		for (;;) {
			r = write(fd, buf + done, count - done);
			if (-1 == r) {
				switch (errno) {
					case EINTR:
						continue;
					case EAGAIN:
						goto next;
					default:
						goto error;
				}
			}
			break;
		}
		done += r;
	}
	io_stop(FBR_A_ fiber);
	return (ssize_t)done;

error:
	io_stop(FBR_A_ fiber);
	return -1;
}

ssize_t fbr_recvfrom(FBR_P_ int sockfd, void *buf, size_t len, int flags,
		struct sockaddr *src_addr, socklen_t *addrlen)
{
	struct fbr_fiber *fiber = CURRENT_FIBER;
	int nbytes;

	io_start(FBR_A_ fiber, sockfd, EV_READ);
	fbr_yield(FBR_A);
	io_stop(FBR_A_ fiber);
	if (CALLED_BY_ROOT) {
		nbytes = recvfrom(sockfd, buf, len, flags, src_addr, addrlen);
		return nbytes;
	}
	errno = EINTR;
	return -1;
}

ssize_t fbr_sendto(FBR_P_ int sockfd, const void *buf, size_t len, int flags, const
		struct sockaddr *dest_addr, socklen_t addrlen)
{
	struct fbr_fiber *fiber = CURRENT_FIBER;
	int nbytes;

	io_start(FBR_A_ fiber, sockfd, EV_WRITE);
	fbr_yield(FBR_A);
	io_stop(FBR_A_ fiber);
	if (CALLED_BY_ROOT) {
		nbytes = sendto(sockfd, buf, len, flags, dest_addr, addrlen);
		return nbytes;
	}
	errno = EINTR;
	return -1;
}

int fbr_accept(FBR_P_ int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
	struct fbr_fiber *fiber = CURRENT_FIBER;
	int r;

	io_start(FBR_A_ fiber, sockfd, EV_READ);
	fbr_yield(FBR_A);
	if (!CALLED_BY_ROOT) {
		io_stop(FBR_A_ fiber);
		errno = EINTR;
		return -1;
	}
	do {
		r = accept(sockfd, addr, addrlen);
	} while (-1 == r && EINTR == errno);

	io_stop(FBR_A_ fiber);

	return r;
}

static void timer_start(FBR_P_ struct fbr_fiber *fiber, ev_tstamp timeout,
		ev_tstamp repeat)
{
	ev_timer_set(&fiber->w_timer, timeout, repeat);
	ev_timer_start(fctx->__p->loop, &fiber->w_timer);
	fiber->w_timer_expected = 1;
	fill_trace_info(FBR_A_ &fiber->w_timer_tinfo);
}

static void timer_stop(FBR_P_ struct fbr_fiber *fiber)
{
	fiber->w_timer_expected = 0;
	ev_timer_stop(fctx->__p->loop, &fiber->w_timer);
}

ev_tstamp fbr_sleep(FBR_P_ ev_tstamp seconds)
{
	struct fbr_fiber *fiber = CURRENT_FIBER;
	ev_tstamp expected = ev_now(fctx->__p->loop) + seconds;
	timer_start(FBR_A_ fiber, seconds, 0.);
	fbr_yield(FBR_A);
	timer_stop(FBR_A_ fiber);
	return max(0., expected - ev_now(fctx->__p->loop));
}

static size_t round_up_to_page_size(size_t size)
{
	static long sz;
	size_t remainder;
	if (0 == sz)
		sz = sysconf(_SC_PAGESIZE);
	remainder = size % sz;
	if (remainder == 0)
		return size;
	return size + sz - remainder;
}

fbr_id_t fbr_create(FBR_P_ const char *name, void (*func) (FBR_P),
		size_t stack_size)
{
	struct fbr_fiber *fiber;
	if (!LIST_EMPTY(&fctx->__p->reclaimed)) {
		fiber = LIST_FIRST(&fctx->__p->reclaimed);
		LIST_REMOVE(fiber, entries.reclaimed);
	} else {
		fiber = malloc(sizeof(struct fbr_fiber));
		memset(fiber, 0x00, sizeof(struct fbr_fiber));
		if (0 == stack_size)
			stack_size = FBR_STACK_SIZE;
		stack_size = round_up_to_page_size(stack_size);
		fiber->stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (MAP_FAILED == fiber->stack)
			err(EXIT_FAILURE, "mmap failed");
		fiber->stack_size = stack_size;
		(void)VALGRIND_STACK_REGISTER(fiber->stack, fiber->stack +
				stack_size);
		fiber->id = fctx->__p->last_id++;
	}
	coro_create(&fiber->ctx, (coro_func)call_wrapper, FBR_A, fiber->stack,
			FBR_STACK_SIZE);
	fiber->w_io_expected = 0;
	fiber->w_timer_expected = 0;
	fiber->call_list = NULL;
	fiber->call_list_size = 0;
	LIST_INIT(&fiber->children);
	LIST_INIT(&fiber->pool);
	fiber->name = name;
	fiber->func = func;
	LIST_INSERT_HEAD(&CURRENT_FIBER->children, fiber, entries.children);
	fiber->parent = CURRENT_FIBER;
	return fbr_id_pack(fiber);
}

int fbr_disown(FBR_P_ fbr_id_t parent_id)
{
	struct fbr_fiber *fiber, *parent;
	if (parent_id > 0)
		unpack_transfer_errno(&parent, parent_id);
	else
		parent = &fctx->__p->root;
	fiber = CURRENT_FIBER;
	LIST_REMOVE(fiber, entries.children);
	LIST_INSERT_HEAD(&parent->children, fiber, entries.children);
	fiber->parent = parent;
	return_success;
}

fbr_id_t fbr_parent(FBR_P)
{
	struct fbr_fiber *fiber = CURRENT_FIBER;
	if (fiber->parent == &fctx->__p->root)
		return 0;
	return fbr_id_pack(fiber->parent);
}

void *fbr_calloc(FBR_P_ unsigned int nmemb, size_t size)
{
	void *ptr;
	ptr = allocate_in_fiber(FBR_A_ nmemb * size, CURRENT_FIBER);
	memset(ptr, 0x00, nmemb * size);
	return ptr;
}

void *fbr_alloc(FBR_P_ size_t size)
{
	return allocate_in_fiber(FBR_A_ size, CURRENT_FIBER);
}

void fbr_alloc_set_destructor(_unused_ FBR_P_ void *ptr,
		fbr_alloc_destructor_func_t func, void *context)
{
	struct mem_pool *pool_entry;
	pool_entry = (struct mem_pool *)ptr - 1;
	pool_entry->destructor = func;
	pool_entry->destructor_context = context;
}

void fbr_free(FBR_P_ void *ptr)
{
	fbr_free_in_fiber(FBR_A_ CURRENT_FIBER, ptr, 1);
}

void fbr_free_nd(FBR_P_ void *ptr)
{
	fbr_free_in_fiber(FBR_A_ CURRENT_FIBER, ptr, 0);
}


void fbr_dump_stack(FBR_P_ fbr_logutil_func_t log)
{
	struct fbr_stack_item *ptr = fctx->__p->sp;
	(*log)(FBR_A_ "%s\n%s", "Fiber call stack:",
			"-------------------------------");
	while (ptr >= fctx->__p->stack) {
		(*log)(FBR_A_ "fiber_call: %p\t%s",
				ptr->fiber,
				ptr->fiber->name);
		print_trace_info(FBR_A_ &ptr->tinfo, log);
		(*log)(FBR_A_ "%s", "-------------------------------");
		ptr--;
	}
}

struct fbr_mutex *fbr_mutex_create(_unused_ FBR_P)
{
	struct fbr_mutex *mutex;
	mutex = malloc(sizeof(struct fbr_mutex));
	mutex->locked_by = 0;
	TAILQ_INIT(&mutex->pending);
	return mutex;
}

void fbr_mutex_lock(FBR_P_ struct fbr_mutex *mutex)
{
	struct fiber_id_tailq_i *item;
	if (0 == mutex->locked_by) {
		mutex->locked_by = CURRENT_FIBER_ID;
		return;
	}
	item = id_tailq_i_for(FBR_A_ CURRENT_FIBER, &mutex->pending);
	TAILQ_INSERT_TAIL(&mutex->pending, item, entries);
	fbr_yield(FBR_A);
	while (mutex->locked_by != CURRENT_FIBER_ID)
		fbr_yield(FBR_A);
}

int fbr_mutex_trylock(FBR_P_ struct fbr_mutex *mutex)
{
	if (0 == mutex->locked_by) {
		mutex->locked_by = CURRENT_FIBER_ID;
		return 1;
	}
	return 0;
}

void fbr_mutex_unlock(FBR_P_ struct fbr_mutex *mutex)
{
	struct fiber_id_tailq_i *item;

	if (TAILQ_EMPTY(&mutex->pending)) {
		mutex->locked_by = 0;
		return;
	}

	while (!TAILQ_EMPTY(&mutex->pending)) {
		item = TAILQ_FIRST(&mutex->pending);
		TAILQ_REMOVE(&mutex->pending, item, entries);
		if (-1 == fbr_id_unpack(FBR_A_ NULL, item->id)) {
			assert(FBR_ENOFIBER == fctx->f_errno);
			fbr_free(FBR_A_ item);
			continue;
		}
		mutex->locked_by = item->id;
		break;
	}

	if (TAILQ_EMPTY(&mutex->pending))
		ev_async_start(fctx->__p->loop, &fctx->__p->mutex_async);

	TAILQ_INSERT_TAIL(&fctx->__p->mutexes, mutex, entries);

	ev_async_send(fctx->__p->loop, &fctx->__p->mutex_async);
}

void fbr_mutex_destroy(_unused_ FBR_P_ struct fbr_mutex *mutex)
{
	struct fiber_id_tailq_i *item, *x;
	TAILQ_FOREACH_SAFE(item, &mutex->pending, entries, x) {
		fbr_free_nd(FBR_A_ item);
	}
	free(mutex);
}

struct fbr_cond_var *fbr_cond_create(_unused_ FBR_P)
{
	struct fbr_cond_var *cond;
	cond = malloc(sizeof(struct fbr_cond_var));
	cond->mutex = NULL;
	TAILQ_INIT(&cond->waiting);
	return cond;
}

void fbr_cond_destroy(_unused_ FBR_P_ struct fbr_cond_var *cond)
{
	struct fiber_id_tailq_i *item, *x;
	TAILQ_FOREACH_SAFE(item, &cond->waiting, entries, x) {
		fbr_free_nd(FBR_A_ item);
	}
	free(cond);
}

int fbr_cond_wait(FBR_P_ struct fbr_cond_var *cond, struct fbr_mutex *mutex)
{
	struct fiber_id_tailq_i *item;
	struct fbr_fiber *fiber = CURRENT_FIBER;
	if (0 == mutex->locked_by) {
		fctx->f_errno = FBR_EINVAL;
		return -1;
	}
	item = id_tailq_i_for(FBR_A_ fiber, &cond->waiting);
	TAILQ_INSERT_TAIL(&cond->waiting, item, entries);
	fbr_mutex_unlock(FBR_A_ mutex);
	fbr_yield(FBR_A);
	while (!CALLED_BY_ROOT)
		fbr_yield(FBR_A);
	fbr_mutex_lock(FBR_A_ mutex);
	return_success;
}

void fbr_cond_broadcast(FBR_P_ struct fbr_cond_var *cond)
{
	int was_empty;
	if (TAILQ_EMPTY(&cond->waiting))
		return;
	was_empty = TAILQ_EMPTY(&fctx->__p->pending_fibers);
	TAILQ_CONCAT(&fctx->__p->pending_fibers, &cond->waiting, entries);
	if (was_empty && !TAILQ_EMPTY(&fctx->__p->pending_fibers))
		ev_async_start(fctx->__p->loop, &fctx->__p->pending_async);
	ev_async_send(fctx->__p->loop, &fctx->__p->pending_async);
}

void fbr_cond_signal(FBR_P_ struct fbr_cond_var *cond)
{
	struct fiber_id_tailq_i *item;
	int was_empty;
	if (TAILQ_EMPTY(&cond->waiting))
		return;
	was_empty = TAILQ_EMPTY(&fctx->__p->pending_fibers);
	item = TAILQ_FIRST(&cond->waiting);
	TAILQ_REMOVE(&cond->waiting, item, entries);
	TAILQ_INSERT_TAIL(&fctx->__p->pending_fibers, item, entries);
	if (was_empty && !TAILQ_EMPTY(&fctx->__p->pending_fibers))
		ev_async_start(fctx->__p->loop, &fctx->__p->pending_async);
	ev_async_send(fctx->__p->loop, &fctx->__p->pending_async);
}
