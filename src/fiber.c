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
#include <fcntl.h>
#include <linux/limits.h>
#include <libgen.h>
#include <assert.h>
#include <errno.h>
#include <utlist.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <err.h>
#include <valgrind/valgrind.h>

#include <evfibers_private/config.h>
#include <evfibers_private/fiber.h>
#include <evfibers_private/worker.pb-c.h>

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


#define ENSURE_ROOT_FIBER do {                            \
	assert(fctx->__p->sp->fiber == &fctx->__p->root); \
} while (0)

#define CURRENT_FIBER (fctx->__p->sp->fiber)
#define CURRENT_FIBER_ID (fbr_id_pack(CURRENT_FIBER))
#define CALLED_BY_ROOT ((fctx->__p->sp - 1)->fiber == &fctx->__p->root)

#define unpack_transfer_errno(value, ptr, id)           \
	do {                                            \
		if (-1 == fbr_id_unpack(fctx, ptr, id)) \
			return (value);                 \
	} while (0)

#define return_success(value)                \
	do {                                 \
		fctx->f_errno = FBR_SUCCESS; \
		return (value);              \
	} while (0)

#define return_error(value, code)       \
	do {                            \
		fctx->f_errno = (code); \
		return (value);         \
	} while (0)


const fbr_id_t FBR_ID_NULL = {0, NULL};

static fbr_id_t fbr_id_pack(struct fbr_fiber *fiber)
{
	return (struct fbr_id_s){.g = fiber->id, .p = fiber};
}

static int fbr_id_unpack(FBR_P_ struct fbr_fiber **ptr, fbr_id_t id)
{
	struct fbr_fiber *fiber = id.p;
	if (fiber->id != id.g)
		return_error(-1, FBR_ENOFIBER);
	if (ptr)
		*ptr = id.p;
	return 0;
}

static void pending_async_cb(EV_P_ ev_async *w, _unused_ int revents)
{
	struct fbr_context *fctx;
	struct fbr_id_tailq_i *item;
	fctx = (struct fbr_context *)w->data;
	int retval;

	ENSURE_ROOT_FIBER;

	if (TAILQ_EMPTY(&fctx->__p->pending_fibers)) {
		ev_async_stop(EV_A_ &fctx->__p->pending_async);
		return;
	}

	item = TAILQ_FIRST(&fctx->__p->pending_fibers);
	assert(item->head == &fctx->__p->pending_fibers);
	/* item shall be removed from the queue by a destructor, which shall be
	 * set by the procedure demanding delayed execution. Destructor
	 * guarantees removal upon the reclaim of fiber. */
	ev_async_send(EV_A_ &fctx->__p->pending_async);

	retval = fbr_transfer(FBR_A_ item->id);
	if (-1 == retval && FBR_ENOFIBER != fctx->f_errno) {
		fbr_log_e(FBR_A_ "libevfibers: unexpected error trying to call"
				" a fiber by id: %s",
				fbr_strerror(FBR_A_ fctx->f_errno));
	}
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

static void stdio_logger(FBR_P_ struct fbr_logger *logger,
		enum fbr_log_level level, const char *format, va_list ap)
{
	struct fbr_fiber *fiber;
	FILE* stream;
	char *str_level;
	ev_tstamp tstamp;

	if (level > logger->level)
		return;

	fiber = CURRENT_FIBER;

	switch (level) {
		case FBR_LOG_ERROR:
			str_level = "ERROR";
			stream = stderr;
			break;
		case FBR_LOG_WARNING:
			str_level = "WARNING";
			stream = stdout;
			break;
		case FBR_LOG_NOTICE:
			str_level = "NOTICE";
			stream = stdout;
			break;
		case FBR_LOG_INFO:
			str_level = "INFO";
			stream = stdout;
			break;
		case FBR_LOG_DEBUG:
			str_level = "DEBUG";
			stream = stdout;
			break;
		default:
			str_level = "?????";
			stream = stdout;
			break;
	}
	tstamp = ev_now(fctx->__p->loop);
	fprintf(stream, "%.6f  %-7s %-16s ", tstamp, str_level, fiber->name);
	vfprintf(stream, format, ap);
	fprintf(stream, "\n");
}

void fbr_init(FBR_P_ struct ev_loop *loop)
{
	struct fbr_fiber *root;
	struct fbr_logger *logger;

	fctx->__p = malloc(sizeof(struct fbr_context_private));
	LIST_INIT(&fctx->__p->reclaimed);
	LIST_INIT(&fctx->__p->root.children);
	LIST_INIT(&fctx->__p->root.pool);
	TAILQ_INIT(&fctx->__p->root.destructors);
	TAILQ_INIT(&fctx->__p->pending_fibers);
	SLIST_INIT(&fctx->__p->free_workers);

	root = &fctx->__p->root;
	strncpy(root->name, "root", FBR_MAX_FIBER_NAME);
	fctx->__p->last_id = 0;
	root->id = fctx->__p->last_id++;
	coro_create(&root->ctx, NULL, NULL, NULL, 0);

	logger = allocate_in_fiber(FBR_A_ sizeof(struct fbr_logger), root);
	logger->logv = stdio_logger;
	logger->level = FBR_LOG_NOTICE;
	fctx->logger = logger;

	fctx->__p->sp = fctx->__p->stack;
	fctx->__p->sp->fiber = root;
	fctx->__p->backtraces_enabled = 1;
	fill_trace_info(FBR_A_ &fctx->__p->sp->tinfo);
	fctx->__p->loop = loop;
	fctx->__p->pending_async.data = fctx;
	fctx->__p->backtraces_enabled = 0;
	memset(&fctx->__p->key_free_mask, 0x00,
			sizeof(fctx->__p->key_free_mask));
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
		case FBR_ESYSTEM:
			return "System error, consult system errno";
		case FBR_EBUFFERMMAP:
			return "Failed to mmap two adjacent regions";
		case FBR_ENOKEY:
			return "Fiber-local key does not exist";
		case FBR_EASYNC:
			return "Async call error (see logs)";
		case FBR_EPROTOBUF:
			return "Protobuf unpacking error";
	}
	return "Unknown error";
}

void fbr_log_e(FBR_P_ const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	(*fctx->logger->logv)(FBR_A_ fctx->logger, FBR_LOG_ERROR, format, ap);
	va_end(ap);
}

void fbr_log_w(FBR_P_ const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	(*fctx->logger->logv)(FBR_A_ fctx->logger, FBR_LOG_WARNING, format, ap);
	va_end(ap);
}

void fbr_log_n(FBR_P_ const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	(*fctx->logger->logv)(FBR_A_ fctx->logger, FBR_LOG_NOTICE, format, ap);
	va_end(ap);
}

void fbr_log_i(FBR_P_ const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	(*fctx->logger->logv)(FBR_A_ fctx->logger, FBR_LOG_INFO, format, ap);
	va_end(ap);
}

void fbr_log_d(FBR_P_ const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	(*fctx->logger->logv)(FBR_A_ fctx->logger, FBR_LOG_DEBUG, format, ap);
	va_end(ap);
}

void id_tailq_i_set(_unused_ FBR_P_
		struct fbr_id_tailq_i *item,
		struct fbr_fiber *fiber)
{
	item->id = fbr_id_pack(fiber);
	item->ev = NULL;
}

static void reclaim_children(FBR_P_ struct fbr_fiber *fiber)
{
	struct fbr_fiber *f;
	LIST_FOREACH(f, &fiber->children, entries.children) {
		fbr_reclaim(FBR_A_ fbr_id_pack(f));
	}
}

static void fbr_free_in_fiber(_unused_ FBR_P_ _unused_ struct fbr_fiber *fiber,
		void *ptr, int destructor);

void fbr_destroy(FBR_P)
{
	struct fbr_fiber *fiber, *x;
	struct mem_pool *p, *x2;

	reclaim_children(FBR_A_ &fctx->__p->root);

	LIST_FOREACH_SAFE(p, &fctx->__p->root.pool, entries, x2) {
		fbr_free_in_fiber(FBR_A_ &fctx->__p->root, p + 1, 1);
	}

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

static void cancel_ev(_unused_ FBR_P_ struct fbr_ev_base *ev)
{
	fbr_destructor_remove(FBR_A_ &ev->item.dtor, 1 /* call it */);
}

static void post_ev(_unused_ FBR_P_ struct fbr_fiber *fiber,
		struct fbr_ev_base *ev)
{
	assert(NULL != fiber->ev.waiting);

	fiber->ev.arrived = 1;
	ev->arrived = 1;
}

static void ev_watcher_cb(_unused_ EV_P_ ev_watcher *w, _unused_ int event)
{
	struct fbr_fiber *fiber;
	struct fbr_ev_watcher *ev = w->data;
	struct fbr_context *fctx = ev->ev_base.fctx;
	int retval;

	ENSURE_ROOT_FIBER;

	retval = fbr_id_unpack(FBR_A_ &fiber, ev->ev_base.id);
	if (-1 == retval) {
		fbr_log_e(FBR_A_ "libevfibers: fiber is about to be called by"
			" the watcher callback, but it's id is not valid: %s",
			fbr_strerror(FBR_A_ fctx->f_errno));
		abort();
	}

	post_ev(FBR_A_ fiber, &ev->ev_base);

	retval = fbr_transfer(FBR_A_ fbr_id_pack(fiber));
	assert(0 == retval);
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
		pool_entry->destructor(FBR_A_ ptr, pool_entry->destructor_context);
	free(pool_entry);
}

static void fiber_cleanup(FBR_P_ struct fbr_fiber *fiber)
{
	struct mem_pool *p, *x;
	struct fbr_destructor *dtor;
	/* coro_destroy(&fiber->ctx); */
	LIST_REMOVE(fiber, entries.children);
	TAILQ_FOREACH(dtor, &fiber->destructors, entries) {
		dtor->func(FBR_A_ dtor->arg);
	}
	LIST_FOREACH_SAFE(p, &fiber->pool, entries, x) {
		fbr_free_in_fiber(FBR_A_ fiber, p + 1, 1);
	}
}

static void filter_fiber_stack(FBR_P_ struct fbr_fiber *fiber)
{
	struct fbr_stack_item *sp;
	for (sp = fctx->__p->stack; sp < fctx->__p->sp; sp++) {
		if (sp->fiber == fiber) {
			memmove(sp, sp + 1, (fctx->__p->sp - sp) * sizeof(*sp));
			fctx->__p->sp--;
		}
	}
}

int fbr_reclaim(FBR_P_ fbr_id_t id)
{
	struct fbr_fiber *fiber;
	struct fbr_mutex mutex;
	int retval;
#if 0
	struct fbr_fiber *f;
#endif

	unpack_transfer_errno(-1, &fiber, id);

	fbr_mutex_init(FBR_A_ &mutex);
	fbr_mutex_lock(FBR_A_ &mutex);
	while (fiber->no_reclaim) {
		fiber->want_reclaim = 1;
		assert("Attempt to reclaim self while no_reclaim is set would"
				" block forever" && fiber != CURRENT_FIBER);
		if (-1 == fbr_id_unpack(FBR_A_ NULL, id) &&
				FBR_ENOFIBER == fctx->f_errno)
			return_success(0);
		retval = fbr_cond_wait(FBR_A_ &fiber->reclaim_cond, &mutex);
		assert(0 == retval);
	}
	fbr_mutex_unlock(FBR_A_ &mutex);
	fbr_mutex_destroy(FBR_A_ &mutex);

	if (-1 == fbr_id_unpack(FBR_A_ NULL, id) &&
			FBR_ENOFIBER == fctx->f_errno)
		return_success(0);

	fill_trace_info(FBR_A_ &fiber->reclaim_tinfo);
	reclaim_children(FBR_A_ fiber);
	fiber_cleanup(FBR_A_ fiber);
	fiber->id = fctx->__p->last_id++;
#if 0
	LIST_FOREACH(f, &fctx->__p->reclaimed, entries.reclaimed) {
		assert(f != fiber);
	}
#endif
	LIST_INSERT_HEAD(&fctx->__p->reclaimed, fiber, entries.reclaimed);

	filter_fiber_stack(FBR_A_ fiber);

	if (CURRENT_FIBER == fiber)
		fbr_yield(FBR_A);

	return_success(0);
}

int fbr_set_reclaim(FBR_P_ fbr_id_t id)
{
	struct fbr_fiber *fiber;

	unpack_transfer_errno(-1, &fiber, id);
	fiber->no_reclaim = 0;
	fbr_cond_broadcast(FBR_A_ &fiber->reclaim_cond);
	return_success(0);
}

int fbr_set_noreclaim(FBR_P_ fbr_id_t id)
{
	struct fbr_fiber *fiber;

	unpack_transfer_errno(-1, &fiber, id);
	fiber->no_reclaim = 1;
	return_success(0);
}

int fbr_want_reclaim(FBR_P_ fbr_id_t id)
{
	struct fbr_fiber *fiber;

	unpack_transfer_errno(-1, &fiber, id);
	return_success(fiber->want_reclaim);
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

static void call_wrapper(FBR_P)
{
	int retval;
	struct fbr_fiber *fiber = CURRENT_FIBER;

	fiber->func(FBR_A_ fiber->func_arg);

	retval = fbr_reclaim(FBR_A_ fbr_id_pack(fiber));
	assert(0 == retval);
	fbr_yield(FBR_A);
	assert(NULL);
}

enum ev_action_hint {
	EV_AH_OK = 0,
	EV_AH_ARRIVED,
	EV_AH_EINVAL
};

static void item_dtor(_unused_ FBR_P_ void *arg)
{
	struct fbr_id_tailq_i *item = arg;

	if (item->head) {
		TAILQ_REMOVE(item->head, item, entries);
	}
}

static enum ev_action_hint prepare_ev(FBR_P_ struct fbr_ev_base *ev)
{
	struct fbr_ev_watcher *e_watcher;
	struct fbr_ev_mutex *e_mutex;
	struct fbr_ev_cond_var *e_cond;
	struct fbr_id_tailq_i *item = &ev->item;

	ev->arrived = 0;
	ev->item.dtor.func = item_dtor;
	ev->item.dtor.arg = item;
	fbr_destructor_add(FBR_A_ &ev->item.dtor);

	switch (ev->type) {
	case FBR_EV_WATCHER:
		e_watcher = fbr_ev_upcast(ev, fbr_ev_watcher);
		if (!ev_is_active(e_watcher->w)) {
			fbr_destructor_remove(FBR_A_ &ev->item.dtor,
					0 /* call it */);
			return EV_AH_EINVAL;
		}
		e_watcher->w->data = e_watcher;
		ev_set_cb(e_watcher->w, ev_watcher_cb);
		break;
	case FBR_EV_MUTEX:
		e_mutex = fbr_ev_upcast(ev, fbr_ev_mutex);
		if (fbr_id_isnull(e_mutex->mutex->locked_by)) {
			e_mutex->mutex->locked_by = CURRENT_FIBER_ID;
			return EV_AH_ARRIVED;
		}
		id_tailq_i_set(FBR_A_ item, CURRENT_FIBER);
		item->ev = ev;
		ev->data = item;
		TAILQ_INSERT_TAIL(&e_mutex->mutex->pending, item, entries);
		item->head = &e_mutex->mutex->pending;
		break;
	case FBR_EV_COND_VAR:
		e_cond = fbr_ev_upcast(ev, fbr_ev_cond_var);
		if (fbr_id_isnull(e_cond->mutex->locked_by)) {
			fbr_destructor_remove(FBR_A_ &ev->item.dtor,
					0 /* call it */);
			return EV_AH_EINVAL;
		}
		id_tailq_i_set(FBR_A_ item, CURRENT_FIBER);
		item->ev = ev;
		ev->data = item;
		TAILQ_INSERT_TAIL(&e_cond->cond->waiting, item, entries);
		item->head = &e_cond->cond->waiting;
		fbr_mutex_unlock(FBR_A_ e_cond->mutex);
		break;
	}
	return EV_AH_OK;
}

static void finish_ev(FBR_P_ struct fbr_ev_base *ev)
{
	struct fbr_ev_cond_var *e_cond;
	struct fbr_ev_watcher *e_watcher;
	fbr_destructor_remove(FBR_A_ &ev->item.dtor, 1 /* call it */);
	switch (ev->type) {
	case FBR_EV_COND_VAR:
		e_cond = fbr_ev_upcast(ev, fbr_ev_cond_var);
		fbr_mutex_lock(FBR_A_ e_cond->mutex);
		break;
	case FBR_EV_WATCHER:
		e_watcher = fbr_ev_upcast(ev, fbr_ev_watcher);
		ev_set_cb(e_watcher->w, NULL);
		break;
	case FBR_EV_MUTEX:
		/* NOP */
		break;
	}
}

static void watcher_timer_dtor(_unused_ FBR_P_ void *_arg)
{
	struct ev_timer *w = _arg;
	ev_timer_stop(fctx->__p->loop, w);
}

int fbr_ev_wait_to(FBR_P_ struct fbr_ev_base *events[], ev_tstamp timeout)
{
	size_t size;
	ev_timer timer;
	struct fbr_ev_watcher watcher;
	struct fbr_destructor dtor = FBR_DESTRUCTOR_INITIALIZER;
	struct fbr_ev_base **new_events;
	struct fbr_ev_base **ev_pptr;
	int n_events;

	ev_timer_init(&timer, NULL, timeout, 0.);
	ev_timer_start(fctx->__p->loop, &timer);
	fbr_ev_watcher_init(FBR_A_ &watcher,
			(struct ev_watcher *)&timer);
	dtor.func = watcher_timer_dtor;
	dtor.arg = &timer;
	fbr_destructor_add(FBR_A_ &dtor);
	size = 0;
	for (ev_pptr = events; NULL != *ev_pptr; ev_pptr++)
		size++;
	new_events = alloca((size + 1) * sizeof(void *));
	memcpy(new_events, events, size * sizeof(void *));
	new_events[size] = &watcher.ev_base;
	new_events[size + 1] = NULL;
	n_events = fbr_ev_wait(FBR_A_ new_events);
	fbr_destructor_remove(FBR_A_ &dtor, 1 /* Call it? */);
	if (n_events < 0)
		return n_events;
	if (watcher.ev_base.arrived)
		n_events--;
	return n_events;
}

int fbr_ev_wait(FBR_P_ struct fbr_ev_base *events[])
{
	struct fbr_fiber *fiber = CURRENT_FIBER;
	struct fbr_ev_base **ev_pptr;
	enum ev_action_hint hint;
	int num = 0;

	fiber->ev.arrived = 0;
	fiber->ev.waiting = events;

	for (ev_pptr = events; NULL != *ev_pptr; ev_pptr++) {
		hint = prepare_ev(FBR_A_ *ev_pptr);
		switch (hint) {
		case EV_AH_OK:
			break;
		case EV_AH_ARRIVED:
			fiber->ev.arrived = 1;
			(*ev_pptr)->arrived = 1;
			break;
		case EV_AH_EINVAL:
			return_error(-1, FBR_EINVAL);
		}
	}

	while (0 == fiber->ev.arrived)
		fbr_yield(FBR_A);

	for (ev_pptr = events; NULL != *ev_pptr; ev_pptr++) {
		if ((*ev_pptr)->arrived) {
			num++;
			finish_ev(FBR_A_ *ev_pptr);
		} else
			cancel_ev(FBR_A_ *ev_pptr);
	}
	return_success(num);
}

int fbr_ev_wait_one(FBR_P_ struct fbr_ev_base *one)
{
	int n_events;
	struct fbr_ev_base *events[] = {one, NULL};
	n_events = fbr_ev_wait(FBR_A_ events);
	if (1 == n_events)
		return 0;
	return -1;
}

int fbr_transfer(FBR_P_ fbr_id_t to)
{
	struct fbr_fiber *callee;
	struct fbr_fiber *caller = fctx->__p->sp->fiber;

	unpack_transfer_errno(-1, &callee, to);

	fctx->__p->sp++;

	fctx->__p->sp->fiber = callee;
	fill_trace_info(FBR_A_ &fctx->__p->sp->tinfo);

	coro_transfer(&caller->ctx, &callee->ctx);

	return_success(0);
}

void fbr_yield(FBR_P)
{
	struct fbr_fiber *callee;
	struct fbr_fiber *caller;
	assert("Attemp to yield in a root fiber" &&
			fctx->__p->sp->fiber != &fctx->__p->root);
	callee = fctx->__p->sp->fiber;
	caller = (--fctx->__p->sp)->fiber;
	coro_transfer(&callee->ctx, &caller->ctx);
}

int fbr_fd_nonblock(FBR_P_ int fd)
{
	int flags, s;

	flags = fcntl(fd, F_GETFL, 0);
	if (flags == -1)
		return_error(-1, FBR_ESYSTEM);

	flags |= O_NONBLOCK;
	s = fcntl(fd, F_SETFL, flags);
	if (s == -1)
		return_error(-1, FBR_ESYSTEM);

	return_success(0);
}

static void ev_base_init(FBR_P_ struct fbr_ev_base *ev,
		enum fbr_ev_type type)
{
	memset(ev, 0x00, sizeof(*ev));
	ev->type = type;
	ev->id = CURRENT_FIBER_ID;
	ev->fctx = fctx;
}

void fbr_ev_watcher_init(FBR_P_ struct fbr_ev_watcher *ev, ev_watcher *w)
{
	ev_base_init(FBR_A_ &ev->ev_base, FBR_EV_WATCHER);
	ev->w = w;
}

static void watcher_io_dtor(_unused_ FBR_P_ void *_arg)
{
	struct ev_io *w = _arg;
	ev_io_stop(fctx->__p->loop, w);
}

int fbr_connect(FBR_P_ int sockfd, const struct sockaddr *addr,
                   socklen_t addrlen) {
	ev_io io;
	struct fbr_ev_watcher watcher;
	struct fbr_destructor dtor = FBR_DESTRUCTOR_INITIALIZER;
	int r;
	socklen_t len;
	r = connect(sockfd, addr, addrlen);
	if ((-1 == r) && (EINPROGRESS != errno))
	    return -1;

	ev_io_init(&io, NULL, sockfd, EV_WRITE);
	ev_io_start(fctx->__p->loop, &io);
	dtor.func = watcher_io_dtor;
	dtor.arg = &io;
	fbr_destructor_add(FBR_A_ &dtor);
	fbr_ev_watcher_init(FBR_A_ &watcher, (ev_watcher *)&io);
	fbr_ev_wait_one(FBR_A_ &watcher.ev_base);

	len = sizeof(r);
	if (-1 == getsockopt(sockfd, SOL_SOCKET, SO_ERROR, (void *)&r, &len)) {
		r = -1;
	} else if ( 0 != r ) {
		errno = r;
		r = -1;
	}

	fbr_destructor_remove(FBR_A_ &dtor, 0 /* Call it? */);
	ev_io_stop(fctx->__p->loop, &io);
	return r;
}

ssize_t fbr_read(FBR_P_ int fd, void *buf, size_t count)
{
	ssize_t r;
	ev_io io;
	struct fbr_ev_watcher watcher;
	struct fbr_destructor dtor = FBR_DESTRUCTOR_INITIALIZER;

	ev_io_init(&io, NULL, fd, EV_READ);
	ev_io_start(fctx->__p->loop, &io);
	dtor.func = watcher_io_dtor;
	dtor.arg = &io;
	fbr_destructor_add(FBR_A_ &dtor);

	fbr_ev_watcher_init(FBR_A_ &watcher, (ev_watcher *)&io);
	fbr_ev_wait_one(FBR_A_ &watcher.ev_base);

	fbr_destructor_remove(FBR_A_ &dtor, 0 /* Call it? */);
	do {
		r = read(fd, buf, count);
	} while (-1 == r && EINTR == errno);

	ev_io_stop(fctx->__p->loop, &io);

	return r;
}

ssize_t fbr_read_all(FBR_P_ int fd, void *buf, size_t count)
{
	ssize_t r;
	size_t done = 0;
	ev_io io;
	struct fbr_ev_watcher watcher;
	struct fbr_destructor dtor = FBR_DESTRUCTOR_INITIALIZER;

	ev_io_init(&io, NULL, fd, EV_READ);
	ev_io_start(fctx->__p->loop, &io);
	dtor.func = watcher_io_dtor;
	dtor.arg = &io;
	fbr_destructor_add(FBR_A_ &dtor);

	fbr_ev_watcher_init(FBR_A_ &watcher, (ev_watcher *)&io);

	while (count != done) {
next:
		fbr_ev_wait_one(FBR_A_ &watcher.ev_base);
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
	fbr_destructor_remove(FBR_A_ &dtor, 0 /* Call it? */);
	ev_io_stop(fctx->__p->loop, &io);
	return (ssize_t)done;

error:
	fbr_destructor_remove(FBR_A_ &dtor, 0 /* Call it? */);
	ev_io_stop(fctx->__p->loop, &io);
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
	ssize_t r;
	ev_io io;
	struct fbr_ev_watcher watcher;
	struct fbr_destructor dtor = FBR_DESTRUCTOR_INITIALIZER;

	ev_io_init(&io, NULL, fd, EV_WRITE);
	ev_io_start(fctx->__p->loop, &io);
	dtor.func = watcher_io_dtor;
	dtor.arg = &io;
	fbr_destructor_add(FBR_A_ &dtor);

	fbr_ev_watcher_init(FBR_A_ &watcher, (ev_watcher *)&io);
	fbr_ev_wait_one(FBR_A_ &watcher.ev_base);

	do {
		r = write(fd, buf, count);
	} while (-1 == r && EINTR == errno);

	fbr_destructor_remove(FBR_A_ &dtor, 0 /* Call it? */);
	ev_io_stop(fctx->__p->loop, &io);
	return r;
}

ssize_t fbr_write_all(FBR_P_ int fd, const void *buf, size_t count)
{
	ssize_t r;
	size_t done = 0;
	ev_io io;
	struct fbr_ev_watcher watcher;
	struct fbr_destructor dtor = FBR_DESTRUCTOR_INITIALIZER;

	ev_io_init(&io, NULL, fd, EV_WRITE);
	ev_io_start(fctx->__p->loop, &io);
	dtor.func = watcher_io_dtor;
	dtor.arg = &io;
	fbr_destructor_add(FBR_A_ &dtor);

	fbr_ev_watcher_init(FBR_A_ &watcher, (ev_watcher *)&io);

	while (count != done) {
next:
		fbr_ev_wait_one(FBR_A_ &watcher.ev_base);
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
	fbr_destructor_remove(FBR_A_ &dtor, 0 /* Call it? */);
	ev_io_stop(fctx->__p->loop, &io);
	return (ssize_t)done;

error:
	fbr_destructor_remove(FBR_A_ &dtor, 0 /* Call it? */);
	ev_io_stop(fctx->__p->loop, &io);
	return -1;
}

ssize_t fbr_recvfrom(FBR_P_ int sockfd, void *buf, size_t len, int flags,
		struct sockaddr *src_addr, socklen_t *addrlen)
{
	ev_io io;
	struct fbr_ev_watcher watcher;
	struct fbr_destructor dtor = FBR_DESTRUCTOR_INITIALIZER;

	ev_io_init(&io, NULL, sockfd, EV_READ);
	ev_io_start(fctx->__p->loop, &io);
	dtor.func = watcher_io_dtor;
	dtor.arg = &io;
	fbr_destructor_add(FBR_A_ &dtor);

	fbr_ev_watcher_init(FBR_A_ &watcher, (ev_watcher *)&io);
	fbr_ev_wait_one(FBR_A_ &watcher.ev_base);

	fbr_destructor_remove(FBR_A_ &dtor, 0 /* Call it? */);
	ev_io_stop(fctx->__p->loop, &io);

	return recvfrom(sockfd, buf, len, flags, src_addr, addrlen);
}

ssize_t fbr_sendto(FBR_P_ int sockfd, const void *buf, size_t len, int flags,
		const struct sockaddr *dest_addr, socklen_t addrlen)
{
	ev_io io;
	struct fbr_ev_watcher watcher;
	struct fbr_destructor dtor = FBR_DESTRUCTOR_INITIALIZER;

	ev_io_init(&io, NULL, sockfd, EV_WRITE);
	ev_io_start(fctx->__p->loop, &io);
	dtor.func = watcher_io_dtor;
	dtor.arg = &io;
	fbr_destructor_add(FBR_A_ &dtor);

	fbr_ev_watcher_init(FBR_A_ &watcher, (ev_watcher *)&io);
	fbr_ev_wait_one(FBR_A_ &watcher.ev_base);

	fbr_destructor_remove(FBR_A_ &dtor, 0 /* Call it? */);
	ev_io_stop(fctx->__p->loop, &io);

	return sendto(sockfd, buf, len, flags, dest_addr, addrlen);
}

int fbr_accept(FBR_P_ int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
	int r;
	ev_io io;
	struct fbr_ev_watcher watcher;
	struct fbr_destructor dtor = FBR_DESTRUCTOR_INITIALIZER;

	ev_io_init(&io, NULL, sockfd, EV_READ);
	ev_io_start(fctx->__p->loop, &io);
	dtor.func = watcher_io_dtor;
	dtor.arg = &io;
	fbr_destructor_add(FBR_A_ &dtor);

	fbr_ev_watcher_init(FBR_A_ &watcher, (ev_watcher *)&io);
	fbr_ev_wait_one(FBR_A_ &watcher.ev_base);

	do {
		r = accept(sockfd, addr, addrlen);
	} while (-1 == r && EINTR == errno);

	fbr_destructor_remove(FBR_A_ &dtor, 0 /* Call it? */);
	ev_io_stop(fctx->__p->loop, &io);

	return r;
}

ev_tstamp fbr_sleep(FBR_P_ ev_tstamp seconds)
{
	ev_timer timer;
	struct fbr_ev_watcher watcher;
	struct fbr_destructor dtor = FBR_DESTRUCTOR_INITIALIZER;
	ev_tstamp expected = ev_now(fctx->__p->loop) + seconds;

	ev_timer_init(&timer, NULL, seconds, 0.);
	ev_timer_start(fctx->__p->loop, &timer);
	dtor.func = watcher_timer_dtor;
	dtor.arg = &timer;
	fbr_destructor_add(FBR_A_ &dtor);

	fbr_ev_watcher_init(FBR_A_ &watcher, (ev_watcher *)&timer);
	fbr_ev_wait_one(FBR_A_ &watcher.ev_base);

	fbr_destructor_remove(FBR_A_ &dtor, 0 /* Call it? */);
	ev_timer_stop(fctx->__p->loop, &timer);

	return max(0., expected - ev_now(fctx->__p->loop));
}

static void watcher_async_dtor(_unused_ FBR_P_ void *_arg)
{
	struct ev_async *w = _arg;
	ev_async_stop(fctx->__p->loop, w);
}

void fbr_cooperate(FBR_P)
{
	ev_async async;
	struct fbr_ev_watcher watcher;
	struct fbr_destructor dtor = FBR_DESTRUCTOR_INITIALIZER;

	ev_async_init(&async, NULL);
	ev_set_priority(&async, -2);
	ev_async_start(fctx->__p->loop, &async);
	dtor.func = watcher_async_dtor;
	dtor.arg = &async;
	fbr_destructor_add(FBR_A_ &dtor);

	fbr_ev_watcher_init(FBR_A_ &watcher, (ev_watcher *)&async);
	fbr_ev_wait_one(FBR_A_ &watcher.ev_base);

	fbr_destructor_remove(FBR_A_ &dtor, 0 /* Call it? */);
	ev_async_stop(fctx->__p->loop, &async);
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

fbr_id_t fbr_restart(FBR_P_ fbr_id_t id)
{
	struct fbr_fiber *fiber;
	const char *name;
	fbr_fiber_func_t func;
	void *arg;
	size_t stack_size;

	unpack_transfer_errno(FBR_ID_NULL, &fiber, id);
	name = fiber->name;
	func = fiber->func;
	arg = fiber->func_arg;
	stack_size = fiber->stack_size;
	fbr_reclaim(FBR_A_ id);
	return fbr_create(FBR_A_ name, func, arg, stack_size);
}

fbr_id_t fbr_create(FBR_P_ const char *name, fbr_fiber_func_t func, void *arg,
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
		fbr_cond_init(FBR_A_ &fiber->reclaim_cond);
		fiber->id = fctx->__p->last_id++;
	}
	coro_create(&fiber->ctx, (coro_func)call_wrapper, FBR_A, fiber->stack,
			fiber->stack_size);
	LIST_INIT(&fiber->children);
	LIST_INIT(&fiber->pool);
	TAILQ_INIT(&fiber->destructors);
	strncpy(fiber->name, name, FBR_MAX_FIBER_NAME);
	fiber->func = func;
	fiber->func_arg = arg;
	LIST_INSERT_HEAD(&CURRENT_FIBER->children, fiber, entries.children);
	fiber->parent = CURRENT_FIBER;
	fiber->no_reclaim = 0;
	fiber->want_reclaim = 0;
	return fbr_id_pack(fiber);
}

int fbr_disown(FBR_P_ fbr_id_t parent_id)
{
	struct fbr_fiber *fiber, *parent;
	if (!fbr_id_isnull(parent_id))
		unpack_transfer_errno(-1, &parent, parent_id);
	else
		parent = &fctx->__p->root;
	fiber = CURRENT_FIBER;
	LIST_REMOVE(fiber, entries.children);
	LIST_INSERT_HEAD(&parent->children, fiber, entries.children);
	fiber->parent = parent;
	return_success(0);
}

fbr_id_t fbr_parent(FBR_P)
{
	struct fbr_fiber *fiber = CURRENT_FIBER;
	if (fiber->parent == &fctx->__p->root)
		return FBR_ID_NULL;
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
	(*log)(FBR_A_ "%s", "Fiber call stack:");
	(*log)(FBR_A_ "%s", "-------------------------------");
	while (ptr >= fctx->__p->stack) {
		(*log)(FBR_A_ "fiber_call: %p\t%s",
				ptr->fiber,
				ptr->fiber->name);
		print_trace_info(FBR_A_ &ptr->tinfo, log);
		(*log)(FBR_A_ "%s", "-------------------------------");
		ptr--;
	}
}

static void transfer_later(FBR_P_ struct fbr_id_tailq_i *item)
{
	int was_empty;
	was_empty = TAILQ_EMPTY(&fctx->__p->pending_fibers);
	TAILQ_INSERT_TAIL(&fctx->__p->pending_fibers, item, entries);
	item->head = &fctx->__p->pending_fibers;
	if (was_empty && !TAILQ_EMPTY(&fctx->__p->pending_fibers)) {
		ev_async_start(fctx->__p->loop, &fctx->__p->pending_async);
	}
	ev_async_send(fctx->__p->loop, &fctx->__p->pending_async);
}

static void transfer_later_tailq(FBR_P_ struct fbr_id_tailq *tailq)
{
	int was_empty;
	struct fbr_id_tailq_i *item;
	TAILQ_FOREACH(item, tailq, entries) {
		item->head = &fctx->__p->pending_fibers;
	}
	was_empty = TAILQ_EMPTY(&fctx->__p->pending_fibers);
	TAILQ_CONCAT(&fctx->__p->pending_fibers, tailq, entries);
	if (was_empty && !TAILQ_EMPTY(&fctx->__p->pending_fibers)) {
		ev_async_start(fctx->__p->loop, &fctx->__p->pending_async);
	}
	ev_async_send(fctx->__p->loop, &fctx->__p->pending_async);
}

void fbr_ev_mutex_init(FBR_P_ struct fbr_ev_mutex *ev,
		struct fbr_mutex *mutex)
{
	ev_base_init(FBR_A_ &ev->ev_base, FBR_EV_MUTEX);
	ev->mutex = mutex;
}

void fbr_mutex_init(_unused_ FBR_P_ struct fbr_mutex *mutex)
{
	mutex->locked_by = FBR_ID_NULL;
	TAILQ_INIT(&mutex->pending);
}

void fbr_mutex_lock(FBR_P_ struct fbr_mutex *mutex)
{
	struct fbr_ev_mutex ev;

	fbr_ev_mutex_init(FBR_A_ &ev, mutex);
	fbr_ev_wait_one(FBR_A_ &ev.ev_base);
	assert(fbr_id_eq(mutex->locked_by, CURRENT_FIBER_ID));
}

int fbr_mutex_trylock(FBR_P_ struct fbr_mutex *mutex)
{
	if (fbr_id_isnull(mutex->locked_by)) {
		mutex->locked_by = CURRENT_FIBER_ID;
		return 1;
	}
	return 0;
}

void fbr_mutex_unlock(FBR_P_ struct fbr_mutex *mutex)
{
	struct fbr_id_tailq_i *item, *x;
	struct fbr_fiber *fiber = NULL;

	if (TAILQ_EMPTY(&mutex->pending)) {
		mutex->locked_by = FBR_ID_NULL;
		return;
	}

	TAILQ_FOREACH_SAFE(item, &mutex->pending, entries, x) {
		assert(item->head == &mutex->pending);
		TAILQ_REMOVE(&mutex->pending, item, entries);
		if (-1 == fbr_id_unpack(FBR_A_ &fiber, item->id)) {
			fbr_log_e(FBR_A_ "libevfibers: unexpected error trying"
					" to find a fiber by id: %s",
					fbr_strerror(FBR_A_ fctx->f_errno));
			continue;
		}
		break;
	}

	mutex->locked_by = item->id;
	post_ev(FBR_A_ fiber, item->ev);

	transfer_later(FBR_A_ item);
}

void fbr_mutex_destroy(_unused_ FBR_P_ _unused_ struct fbr_mutex *mutex)
{
	/* Since mutex is stack allocated now, this efffeectively turns into
	 * NOOP. But we might consider adding some cleanup in the future.
	 */
}

void fbr_ev_cond_var_init(FBR_P_ struct fbr_ev_cond_var *ev,
		struct fbr_cond_var *cond, struct fbr_mutex *mutex)
{
	ev_base_init(FBR_A_ &ev->ev_base, FBR_EV_COND_VAR);
	ev->cond = cond;
	ev->mutex = mutex;
}

void fbr_cond_init(_unused_ FBR_P_ struct fbr_cond_var *cond)
{
	cond->mutex = NULL;
	TAILQ_INIT(&cond->waiting);
}

void fbr_cond_destroy(_unused_ FBR_P_ _unused_ struct fbr_cond_var *cond)
{
	/* Since condvar is stack allocated now, this efffeectively turns into
	 * NOOP. But we might consider adding some cleanup in the future.
	 */
}

int fbr_cond_wait(FBR_P_ struct fbr_cond_var *cond, struct fbr_mutex *mutex)
{
	struct fbr_ev_cond_var ev;

	if (fbr_id_isnull(mutex->locked_by))
		return_error(-1, FBR_EINVAL);

	fbr_ev_cond_var_init(FBR_A_ &ev, cond, mutex);
	fbr_ev_wait_one(FBR_A_ &ev.ev_base);
	return_success(0);
}

void fbr_cond_broadcast(FBR_P_ struct fbr_cond_var *cond)
{
	struct fbr_id_tailq_i *item;
	struct fbr_fiber *fiber;
	if (TAILQ_EMPTY(&cond->waiting))
		return;
	TAILQ_FOREACH(item, &cond->waiting, entries) {
		if(-1 == fbr_id_unpack(FBR_A_ &fiber, item->id)) {
			assert(FBR_ENOFIBER == fctx->f_errno);
			continue;
		}
		post_ev(FBR_A_ fiber, item->ev);
	}
	transfer_later_tailq(FBR_A_ &cond->waiting);
}

void fbr_cond_signal(FBR_P_ struct fbr_cond_var *cond)
{
	struct fbr_id_tailq_i *item;
	struct fbr_fiber *fiber;
	if (TAILQ_EMPTY(&cond->waiting))
		return;
	item = TAILQ_FIRST(&cond->waiting);
	if(-1 == fbr_id_unpack(FBR_A_ &fiber, item->id)) {
		assert(FBR_ENOFIBER == fctx->f_errno);
		return;
	}
	post_ev(FBR_A_ fiber, item->ev);

	assert(item->head == &cond->waiting);
	TAILQ_REMOVE(&cond->waiting, item, entries);
	transfer_later(FBR_A_ item);
}

int fbr_buffer_init(FBR_P_ struct fbr_buffer *buffer, size_t size)
{
	buffer->vrb = vrb_new(size, NULL);
	if (NULL == buffer->vrb)
		return_error(-1,  FBR_ESYSTEM);
	buffer->prepared_bytes = 0;
	buffer->waiting_bytes = 0;
	fbr_cond_init(FBR_A_ &buffer->committed_cond);
	fbr_cond_init(FBR_A_ &buffer->bytes_freed_cond);
	fbr_mutex_init(FBR_A_ &buffer->write_mutex);
	fbr_mutex_init(FBR_A_ &buffer->read_mutex);
	return_success(0);
}

void fbr_buffer_destroy(FBR_P_ struct fbr_buffer *buffer)
{
	vrb_destroy(buffer->vrb);

	fbr_mutex_destroy(FBR_A_ &buffer->read_mutex);
	fbr_mutex_destroy(FBR_A_ &buffer->write_mutex);
	fbr_cond_destroy(FBR_A_ &buffer->committed_cond);
	fbr_cond_destroy(FBR_A_ &buffer->bytes_freed_cond);
}

void *fbr_buffer_alloc_prepare(FBR_P_ struct fbr_buffer *buffer, size_t size)
{
	if (size > vrb_capacity(buffer->vrb))
		return_error(NULL, FBR_EINVAL);

	fbr_mutex_lock(FBR_A_ &buffer->write_mutex);

	while (buffer->prepared_bytes > 0)
		fbr_cond_wait(FBR_A_ &buffer->committed_cond,
				&buffer->write_mutex);

	assert(0 == buffer->prepared_bytes);

	buffer->prepared_bytes = size;

	while ((size_t)vrb_space_len(buffer->vrb) < size)
		fbr_cond_wait(FBR_A_ &buffer->bytes_freed_cond,
				&buffer->write_mutex);

	return vrb_space_ptr(buffer->vrb);
}

void fbr_buffer_alloc_commit(FBR_P_ struct fbr_buffer *buffer)
{
	vrb_give(buffer->vrb, buffer->prepared_bytes);
	buffer->prepared_bytes = 0;
	fbr_cond_signal(FBR_A_ &buffer->committed_cond);
	fbr_mutex_unlock(FBR_A_ &buffer->write_mutex);
}

void fbr_buffer_alloc_abort(FBR_P_ struct fbr_buffer *buffer)
{
	buffer->prepared_bytes = 0;
	fbr_cond_signal(FBR_A_ &buffer->committed_cond);
	fbr_mutex_unlock(FBR_A_ &buffer->write_mutex);
}

void *fbr_buffer_read_address(FBR_P_ struct fbr_buffer *buffer, size_t size)
{
	int retval;
	if (size > vrb_capacity(buffer->vrb))
		return_error(NULL, FBR_EINVAL);

	fbr_mutex_lock(FBR_A_ &buffer->read_mutex);

	while ((size_t)vrb_data_len(buffer->vrb) < size) {
		retval = fbr_cond_wait(FBR_A_ &buffer->committed_cond,
				&buffer->read_mutex);
		assert(0 == retval);
	}

	buffer->waiting_bytes = size;

	return_success(vrb_data_ptr(buffer->vrb));
}

void fbr_buffer_read_advance(FBR_P_ struct fbr_buffer *buffer)
{
	vrb_take(buffer->vrb, buffer->waiting_bytes);

	fbr_cond_signal(FBR_A_ &buffer->bytes_freed_cond);
	fbr_mutex_unlock(FBR_A_ &buffer->read_mutex);
}

void fbr_buffer_read_discard(FBR_P_ struct fbr_buffer *buffer)
{
	fbr_mutex_unlock(FBR_A_ &buffer->read_mutex);
}

size_t fbr_buffer_bytes(FBR_PU_ struct fbr_buffer *buffer)
{
	return vrb_data_len(buffer->vrb);
}

void fbr_buffer_reset(FBR_PU_ struct fbr_buffer *buffer)
{
	vrb_take(buffer->vrb, vrb_data_len(buffer->vrb));
}

size_t fbr_buffer_free_bytes(FBR_PU_ struct fbr_buffer *buffer)
{
	return vrb_space_len(buffer->vrb);
}

size_t fbr_buffer_size(FBR_PU_ struct fbr_buffer *buffer)
{
	return vrb_data_len(buffer->vrb) + vrb_space_len(buffer->vrb);
}

int fbr_buffer_resize(FBR_P_ struct fbr_buffer *buffer, size_t size)
{
	int retval;
	retval = vrb_resize(buffer->vrb, size, NULL);
	if (-1 == retval)
		return_error(-1, FBR_ESYSTEM);
	return_success(0);
}

void *fbr_get_user_data(FBR_P_ fbr_id_t id)
{
	struct fbr_fiber *fiber;
	unpack_transfer_errno(NULL, &fiber, id);
	return_success(fiber->user_data);
}

int fbr_set_user_data(FBR_P_ fbr_id_t id, void *data)
{
	struct fbr_fiber *fiber;
	unpack_transfer_errno(-1, &fiber, id);
	fiber->user_data = data;
	return_success(0);
}

void fbr_destructor_add(FBR_P_ struct fbr_destructor *dtor)
{
	struct fbr_fiber *fiber = CURRENT_FIBER;
	TAILQ_INSERT_TAIL(&fiber->destructors, dtor, entries);
	dtor->active = 1;
}

void fbr_destructor_remove(FBR_P_ struct fbr_destructor *dtor,
		int call)
{
	struct fbr_fiber *fiber = CURRENT_FIBER;

	if (0 == dtor->active)
		return;

	TAILQ_REMOVE(&fiber->destructors, dtor, entries);
	if (call)
		dtor->func(FBR_A_ dtor->arg);
	dtor->active = 0;
}

static inline int ffsll(uint64_t val)
{
	/* TODO: Add some check for the existance of this builtin */
	return __builtin_ffsll(val);
}

static inline int is_key_registered(FBR_P_ fbr_key_t key)
{
	return 0 == (fctx->__p->key_free_mask & (1 << key));
}

static inline void register_key(FBR_P_ fbr_key_t key)
{
	fctx->__p->key_free_mask &= ~(1 << key);
}

static inline void unregister_key(FBR_P_ fbr_key_t key)
{
	fctx->__p->key_free_mask |= (1 << key);
}

int fbr_key_create(FBR_P_ fbr_key_t *key_ptr)
{
	fbr_key_t key = ffsll(fctx->__p->key_free_mask);
	assert(key < FBR_MAX_KEY);
	register_key(FBR_A_ key);
	*key_ptr = key;
	return_success(0);
}

int fbr_key_delete(FBR_P_ fbr_key_t key)
{
	if (!is_key_registered(FBR_A_ key))
		return_error(-1, FBR_ENOKEY);

	unregister_key(FBR_A_ key);

	return_success(0);
}

int fbr_key_set(FBR_P_ fbr_id_t id, fbr_key_t key, void *value)
{
	struct fbr_fiber *fiber;

	unpack_transfer_errno(-1, &fiber, id);

	if (!is_key_registered(FBR_A_ key))
		return_error(-1, FBR_ENOKEY);

	fiber->key_data[key] = value;
	return_success(0);
}

void *fbr_key_get(FBR_P_ fbr_id_t id, fbr_key_t key)
{
	struct fbr_fiber *fiber;

	unpack_transfer_errno(NULL, &fiber, id);

	if (!is_key_registered(FBR_A_ key))
		return_error(NULL, FBR_ENOKEY);

	return fiber->key_data[key];
}

const char *fbr_get_name(FBR_P_ fbr_id_t id)
{
	struct fbr_fiber *fiber;
	unpack_transfer_errno(NULL, &fiber, id);
	return_success(fiber->name);
}

int fbr_set_name(FBR_P_ fbr_id_t id, const char *name)
{
	struct fbr_fiber *fiber;
	unpack_transfer_errno(-1, &fiber, id);
	strncpy(fiber->name, name, FBR_MAX_FIBER_NAME);
	return_success(0);
}

static int init_worker(FBR_P_ struct fbr_async *async)
{
	int retval;
	pid_t pid;
	int pipe_stdin[2], pipe_stdout[2];
	char *worker_bin;

	retval = pipe(pipe_stdin);
	if (-1 == retval)
		return_error(-1, FBR_ESYSTEM);
	retval = pipe(pipe_stdout);
	if (-1 == retval)
		return_error(-1, FBR_ESYSTEM);

	pid = fork();
	if (-1 == pid)
		return_error(-1, FBR_ESYSTEM);
	if (0 == pid) { /* child */
		retval = close(pipe_stdin[1]);
		if (-1 == retval)
			err(EXIT_FAILURE, "close");
		retval = dup2(pipe_stdin[0], 0);
		if (-1 == retval)
			err(EXIT_FAILURE, "close");
		retval = close(pipe_stdout[0]);
		if (-1 == retval)
			err(EXIT_FAILURE, "close");
		retval = dup2(pipe_stdout[1], 1);
		if (-1 == retval)
			err(EXIT_FAILURE, "close");
		/*
		retval = execl("/usr/bin/strace", "/usr/bin/strace", "-o",
				"./worker.strace", WORKER_BIN_PATH, NULL);
				*/
		worker_bin = getenv("FBR_WORKER_BIN_PATH");
		if (NULL == worker_bin)
			worker_bin = WORKER_BIN_PATH;
		retval = execl(worker_bin, worker_bin, NULL);
		if (-1 == retval)
			err(EXIT_FAILURE, "execl");
		abort();
	}
	retval = close(pipe_stdin[0]);
	if (-1 == retval)
		return_error(-1, FBR_ESYSTEM);
	retval = close(pipe_stdout[1]);
	if (-1 == retval)
		return_error(-1, FBR_ESYSTEM);
	async->worker_pid = pid;
	async->write_fd = pipe_stdin[1];
	async->read_fd = pipe_stdout[0];
	retval = fbr_fd_nonblock(FBR_A_ async->write_fd);
	if (retval)
		return retval;
	retval = fbr_fd_nonblock(FBR_A_ async->read_fd);
	if (retval)
		return retval;
	return_success(0);
}

struct fbr_async *fbr_async_create(FBR_P)
{
	struct fbr_async *async;
	int retval;
	if (!SLIST_EMPTY(&fctx->__p->free_workers)) {
		async = SLIST_FIRST(&fctx->__p->free_workers);
		SLIST_REMOVE_HEAD(&fctx->__p->free_workers, entries);
	} else {
		async = calloc(1, sizeof(*async));
		if (NULL == async)
			return_error(NULL, FBR_ESYSTEM);
		async->buf_size = BUFSIZ;
		async->buf = malloc(async->buf_size);
		if (NULL == async->buf)
			return_error(NULL, FBR_ESYSTEM);
		fbr_mutex_init(FBR_A_ &async->mutex);
		retval = init_worker(FBR_A_ async);
		if (retval)
			return NULL;
	}
	return async;
}

void fbr_async_destroy(FBR_P_ struct fbr_async *async)
{
	SLIST_INSERT_HEAD(&fctx->__p->free_workers, async, entries);
}

static int worker_ensure_buf(FBR_PU_ struct fbr_async *async, size_t size)
{
	if (async->buf_size >= size)
		return_success(0);
	while (async->buf_size < size)
		async->buf_size *= 2;
	async->buf = realloc(async->buf, async->buf_size);
	if (NULL == async->buf)
		return_error(-1, FBR_ESYSTEM);
	return_success(0);
}

static ReqResult *worker_communicate(FBR_P_ struct fbr_async *async, Req* req)
{
	size_t size;
	ssize_t retval;
	ReqResult *req_result;
	fbr_mutex_lock(FBR_A_ &async->mutex);

	/* Submit request */
	size = req__get_packed_size(req);
	retval = worker_ensure_buf(FBR_A_ async, size);
	if (retval)
		return NULL;
	req__pack(req, async->buf);
	retval = fbr_write_all(FBR_A_ async->write_fd, &size, sizeof(size));
	if (retval < (ssize_t)sizeof(size))
		return_error(NULL, FBR_ESYSTEM);
	retval = fbr_write_all(FBR_A_ async->write_fd, async->buf, size);
	if (retval < (ssize_t)size)
		return_error(NULL, FBR_ESYSTEM);

	/* Read result */
	retval = fbr_read_all(FBR_A_ async->read_fd, &size, sizeof(size));
	if (retval < (ssize_t)sizeof(size))
		return_error(NULL, FBR_ESYSTEM);
	retval = worker_ensure_buf(FBR_A_ async, size);
	if (retval)
		return NULL;
	retval = fbr_read_all(FBR_A_ async->read_fd, async->buf, size);
	if (retval < (ssize_t)size)
		return_error(NULL, FBR_ESYSTEM);

	req_result = req_result__unpack(NULL, size, async->buf);
	if (NULL == req_result)
		return_error(NULL, FBR_EPROTOBUF);

	fbr_mutex_unlock(FBR_A_ &async->mutex);
	return req_result;
}

#define CHECK_ASYNC_ERROR if (req_result->error) {   \
	fbr_log_e(FBR_A_ "async request error: %s",  \
			req_result->error);          \
	req_result__free_unpacked(req_result, NULL); \
	return_error(-1, FBR_EASYNC);                \
}

int fbr_async_fopen(FBR_P_ struct fbr_async *async, const char *filename,
		const char *mode)
{
	char path_buf[PATH_MAX];
	ReqResult *req_result;
	Req req = REQ__INIT;
	FileReq file_req = FILE_REQ__INIT;
	FileReqOpen file_req_open = FILE_REQ_OPEN__INIT;
	req.type = REQ_TYPE__File;
	req.file = &file_req;
	file_req.type = FILE_REQ_TYPE__Open;
	file_req.open = &file_req_open;
	file_req_open.name = (char *)filename;
	file_req_open.mode = (char *)mode;
	if (NULL == getcwd(path_buf, sizeof(path_buf)))
		return_error(-1, FBR_ESYSTEM);
	file_req_open.cwd = path_buf;
	req_result = worker_communicate(FBR_A_ async, &req);
	if (NULL == req_result)
		return -1;
	CHECK_ASYNC_ERROR
	req_result__free_unpacked(req_result, NULL);
	return_success(0);
}

int fbr_async_fclose(FBR_P_ struct fbr_async *async)
{
	ReqResult *req_result;
	Req req = REQ__INIT;
	FileReq file_req = FILE_REQ__INIT;
	req.type = REQ_TYPE__File;
	req.file = &file_req;
	file_req.type = FILE_REQ_TYPE__Close;
	req_result = worker_communicate(FBR_A_ async, &req);
	if (NULL == req_result)
		return -1;
	CHECK_ASYNC_ERROR
	req_result__free_unpacked(req_result, NULL);
	return_success(0);
}

int fbr_async_fread(FBR_P_ struct fbr_async *async, void *buf, size_t size)
{
	ReqResult *req_result;
	Req req = REQ__INIT;
	FileReq file_req = FILE_REQ__INIT;
	FileReqRead file_req_read = FILE_REQ_READ__INIT;
	req.type = REQ_TYPE__File;
	req.file = &file_req;
	file_req.type = FILE_REQ_TYPE__Read;
	file_req.read = &file_req_read;
	file_req_read.size = size;
	req_result = worker_communicate(FBR_A_ async, &req);
	if (NULL == req_result)
		return -1;
	if (!req_result->has_content && req_result->has_eof) {
		req_result__free_unpacked(req_result, NULL);
		return_success(0);
	}
	assert(req_result->content.len <= size);
	memcpy(buf, req_result->content.data, req_result->content.len);
	if (req_result->error) {
		fbr_log_e(FBR_A_ "async request error: %s", req_result->error);
		req_result__free_unpacked(req_result, NULL);
		return_error(-1, FBR_EASYNC);
	}
	req_result__free_unpacked(req_result, NULL);
	return_success(1);
}

int fbr_async_fwrite(FBR_P_ struct fbr_async *async, void *buf, size_t size)
{
	ReqResult *req_result;
	Req req = REQ__INIT;
	FileReq file_req = FILE_REQ__INIT;
	FileReqWrite file_req_write = FILE_REQ_WRITE__INIT;
	req.type = REQ_TYPE__File;
	req.file = &file_req;
	file_req.type = FILE_REQ_TYPE__Write;
	file_req.write = &file_req_write;
	file_req_write.content.data = buf;
	file_req_write.content.len = size;
	req_result = worker_communicate(FBR_A_ async, &req);
	if (NULL == req_result)
		return -1;
	if (req_result->error) {
		fbr_log_e(FBR_A_ "async request error: %s", req_result->error);
		req_result__free_unpacked(req_result, NULL);
		return_error(-1, FBR_EASYNC);
	}
	req_result__free_unpacked(req_result, NULL);
	return_success(1);
}

int fbr_async_fseek(FBR_P_ struct fbr_async *async, size_t offset, int whence)
{
	ReqResult *req_result;
	Req req = REQ__INIT;
	FileReq file_req = FILE_REQ__INIT;
	FileReqSeek file_req_seek = FILE_REQ_SEEK__INIT;
	req.type = REQ_TYPE__File;
	req.file = &file_req;
	file_req.type = FILE_REQ_TYPE__Seek;
	file_req.seek = &file_req_seek;
	file_req_seek.offset = offset;
	file_req_seek.whence = whence;
	req_result = worker_communicate(FBR_A_ async, &req);
	if (NULL == req_result)
		return -1;
	CHECK_ASYNC_ERROR
	req_result__free_unpacked(req_result, NULL);
	return_success(0);
}

ssize_t fbr_async_ftell(FBR_P_ struct fbr_async *async)
{
	ssize_t result;
	ReqResult *req_result;
	Req req = REQ__INIT;
	FileReq file_req = FILE_REQ__INIT;
	req.type = REQ_TYPE__File;
	req.file = &file_req;
	file_req.type = FILE_REQ_TYPE__Tell;
	req_result = worker_communicate(FBR_A_ async, &req);
	if (NULL == req_result)
		return -1;
	CHECK_ASYNC_ERROR
	result = req_result->retval;
	req_result__free_unpacked(req_result, NULL);
	return_success(result);
}

int fbr_async_fflush(FBR_P_ struct fbr_async *async)
{
	ReqResult *req_result;
	Req req = REQ__INIT;
	FileReq file_req = FILE_REQ__INIT;
	req.type = REQ_TYPE__File;
	req.file = &file_req;
	file_req.type = FILE_REQ_TYPE__Flush;
	req_result = worker_communicate(FBR_A_ async, &req);
	if (NULL == req_result)
		return -1;
	CHECK_ASYNC_ERROR
	req_result__free_unpacked(req_result, NULL);
	return_success(0);
}

int fbr_async_ftruncate(FBR_P_ struct fbr_async *async, size_t size)
{
	ReqResult *req_result;
	Req req = REQ__INIT;
	FileReq file_req = FILE_REQ__INIT;
	FileReqTruncate file_req_truncate = FILE_REQ_TRUNCATE__INIT;
	req.type = REQ_TYPE__File;
	req.file = &file_req;
	file_req.type = FILE_REQ_TYPE__Truncate;
	file_req.truncate = &file_req_truncate;
	file_req_truncate.size = size;
	req_result = worker_communicate(FBR_A_ async, &req);
	if (NULL == req_result)
		return -1;
	CHECK_ASYNC_ERROR
	req_result__free_unpacked(req_result, NULL);
	return_success(0);
}

int fbr_async_fsync(FBR_P_ struct fbr_async *async)
{
	ReqResult *req_result;
	Req req = REQ__INIT;
	FileReq file_req = FILE_REQ__INIT;
	req.type = REQ_TYPE__File;
	req.file = &file_req;
	file_req.type = FILE_REQ_TYPE__Sync;
	req_result = worker_communicate(FBR_A_ async, &req);
	if (NULL == req_result)
		return -1;
	CHECK_ASYNC_ERROR
	req_result__free_unpacked(req_result, NULL);
	return_success(0);
}

int fbr_async_fdatasync(FBR_P_ struct fbr_async *async)
{
	ReqResult *req_result;
	Req req = REQ__INIT;
	FileReq file_req = FILE_REQ__INIT;
	req.type = REQ_TYPE__File;
	req.file = &file_req;
	file_req.type = FILE_REQ_TYPE__DataSync;
	req_result = worker_communicate(FBR_A_ async, &req);
	if (NULL == req_result)
		return -1;
	CHECK_ASYNC_ERROR
	req_result__free_unpacked(req_result, NULL);
	return_success(0);
}

int fbr_async_debug(FBR_P_ struct fbr_async *async)
{
	ReqResult *req_result;
	Req req = REQ__INIT;
	req.type = REQ_TYPE__Debug;
	req_result = worker_communicate(FBR_A_ async, &req);
	if (NULL == req_result)
		return -1;
	CHECK_ASYNC_ERROR
	req_result__free_unpacked(req_result, NULL);
	return_success(0);
}

int fbr_async_fs_stat(FBR_P_ struct fbr_async *async, const char *path,
		struct stat *buf)
{
	char path_buf[PATH_MAX];
	ReqResult *req_result;
	Req req = REQ__INIT;
	FilesystemReq fs_req = FILESYSTEM_REQ__INIT;
	req.type = REQ_TYPE__FileSystem;
	req.fs = &fs_req;
	fs_req.type = FILESYSTEM_REQ_TYPE__Stat;
	if (NULL == getcwd(path_buf, sizeof(path_buf)))
		return_error(-1, FBR_ESYSTEM);
	fs_req.cwd = path_buf;
	fs_req.path = (char *)path;
	req_result = worker_communicate(FBR_A_ async, &req);
	if (NULL == req_result)
		return -1;
	CHECK_ASYNC_ERROR
	if (!req_result->has_content) {
		req_result__free_unpacked(req_result, NULL);
		return_error(-1, FBR_EASYNC);
	}
	memcpy(buf, req_result->content.data, req_result->content.len);
	req_result__free_unpacked(req_result, NULL);
	return_success(0);
}

int fbr_async_fs_realpath(FBR_P_ struct fbr_async *async, const char *path,
		char *buf)
{
	char path_buf[PATH_MAX];
	ReqResult *req_result;
	Req req = REQ__INIT;
	FilesystemReq fs_req = FILESYSTEM_REQ__INIT;
	req.type = REQ_TYPE__FileSystem;
	req.fs = &fs_req;
	fs_req.type = FILESYSTEM_REQ_TYPE__RealPath;
	if (NULL == getcwd(path_buf, sizeof(path_buf)))
		return_error(-1, FBR_ESYSTEM);
	fs_req.cwd = path_buf;
	fs_req.path = (char *)path;
	req_result = worker_communicate(FBR_A_ async, &req);
	if (NULL == req_result)
		return -1;
	CHECK_ASYNC_ERROR
	if (!req_result->has_content) {
		req_result__free_unpacked(req_result, NULL);
		return_error(-1, FBR_EASYNC);
	}
	memcpy(buf, req_result->content.data, req_result->content.len);
	req_result__free_unpacked(req_result, NULL);
	return_success(0);
}

#undef CHECK_ASYNC_ERROR
