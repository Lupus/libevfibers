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

#include <evfibers/config.h>

#include <sys/mman.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <libgen.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <err.h>
#ifdef HAVE_VALGRIND_H
#include <valgrind/valgrind.h>
#else
#define RUNNING_ON_VALGRIND (0)
#define VALGRIND_STACK_REGISTER(a,b) (void)0
#endif

#ifdef FBR_EIO_ENABLED
#include <evfibers/eio.h>
#endif
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
static const char default_buffer_pattern[] = "/dev/shm/fbr_buffer.XXXXXXXXX";

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
	char *buffer_pattern;

	fctx->__p = malloc(sizeof(struct fbr_context_private));
	LIST_INIT(&fctx->__p->reclaimed);
	LIST_INIT(&fctx->__p->root.children);
	LIST_INIT(&fctx->__p->root.pool);
	TAILQ_INIT(&fctx->__p->root.destructors);
	TAILQ_INIT(&fctx->__p->pending_fibers);

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

	buffer_pattern = getenv("FBR_BUFFER_FILE_PATTERN");
	if (buffer_pattern)
		fctx->__p->buffer_file_pattern = buffer_pattern;
	else
		fctx->__p->buffer_file_pattern = default_buffer_pattern;
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
		case FBR_EPROTOBUF:
			return "Protobuf unpacking error";
		case FBR_EBUFFERNOSPACE:
			return "Not enough space in the buffer";
		case FBR_EEIO:
			return "libeio request error";
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
		free(fiber->stack);
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

static int do_reclaim(FBR_P_ struct fbr_fiber *fiber)
{
#if 0
	struct fbr_fiber *f;
#endif

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

int fbr_reclaim(FBR_P_ fbr_id_t id)
{
	struct fbr_fiber *fiber;
	struct fbr_mutex mutex;
	int retval;

	unpack_transfer_errno(-1, &fiber, id);

	fbr_mutex_init(FBR_A_ &mutex);
	fbr_mutex_lock(FBR_A_ &mutex);
	while (fiber->no_reclaim > 0) {
		fiber->want_reclaim = 1;
		assert("Attempt to reclaim self while no_reclaim is set would"
				" block forever" && fiber != CURRENT_FIBER);
		if (-1 == fbr_id_unpack(FBR_A_ NULL, id) &&
				FBR_ENOFIBER == fctx->f_errno)
			return_success(0);
		retval = fbr_cond_wait(FBR_A_ &fiber->reclaim_cond, &mutex);
		assert(0 == retval);
		(void)retval;
	}
	fbr_mutex_unlock(FBR_A_ &mutex);
	fbr_mutex_destroy(FBR_A_ &mutex);

	if (-1 == fbr_id_unpack(FBR_A_ NULL, id) &&
			FBR_ENOFIBER == fctx->f_errno)
		return_success(0);

	return do_reclaim(FBR_A_ fiber);
}

int fbr_set_reclaim(FBR_P_ fbr_id_t id)
{
	struct fbr_fiber *fiber;

	unpack_transfer_errno(-1, &fiber, id);
	fiber->no_reclaim--;
	if (0 == fiber->no_reclaim)
		fbr_cond_broadcast(FBR_A_ &fiber->reclaim_cond);
	return_success(0);
}

int fbr_set_noreclaim(FBR_P_ fbr_id_t id)
{
	struct fbr_fiber *fiber;

	unpack_transfer_errno(-1, &fiber, id);
	fiber->no_reclaim++;
	return_success(0);
}

int fbr_want_reclaim(FBR_P_ fbr_id_t id)
{
	struct fbr_fiber *fiber;

	unpack_transfer_errno(-1, &fiber, id);
	if (fiber->no_reclaim > 0)
		/* If we're in noreclaim block of any depth, always return 0 */
		return 0;
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

static int do_reclaim(FBR_P_ struct fbr_fiber *fiber);

static void call_wrapper(FBR_P)
{
	int retval;
	struct fbr_fiber *fiber = CURRENT_FIBER;

	fiber->func(FBR_A_ fiber->func_arg);

	retval = do_reclaim(FBR_A_ fiber);
	assert(0 == retval);
	(void)retval;
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
		if (e_cond->mutex && fbr_id_isnull(e_cond->mutex->locked_by)) {
			fbr_destructor_remove(FBR_A_ &ev->item.dtor,
					0 /* call it */);
			return EV_AH_EINVAL;
		}
		id_tailq_i_set(FBR_A_ item, CURRENT_FIBER);
		item->ev = ev;
		ev->data = item;
		TAILQ_INSERT_TAIL(&e_cond->cond->waiting, item, entries);
		item->head = &e_cond->cond->waiting;
		if (e_cond->mutex)
			fbr_mutex_unlock(FBR_A_ e_cond->mutex);
		break;
	case FBR_EV_EIO:
#ifdef FBR_EIO_ENABLED
		/* NOP */
#else
		fbr_log_e(FBR_A_ "libevfibers: libeio support is not compiled");
		abort();
#endif
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
		if (e_cond->mutex)
			fbr_mutex_lock(FBR_A_ e_cond->mutex);
		break;
	case FBR_EV_WATCHER:
		e_watcher = fbr_ev_upcast(ev, fbr_ev_watcher);
		ev_set_cb(e_watcher->w, NULL);
		break;
	case FBR_EV_MUTEX:
		/* NOP */
		break;
	case FBR_EV_EIO:
#ifdef FBR_EIO_ENABLED
		/* NOP */
#else
		fbr_log_e(FBR_A_ "libevfibers: libeio support is not compiled");
		abort();
#endif
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
	enum ev_action_hint hint;
	int num = 0;
	int i;

	fiber->ev.arrived = 0;
	fiber->ev.waiting = events;

	for (i = 0; NULL != events[i]; i++) {
		hint = prepare_ev(FBR_A_ events[i]);
		switch (hint) {
		case EV_AH_OK:
			break;
		case EV_AH_ARRIVED:
			fiber->ev.arrived = 1;
			events[i]->arrived = 1;
			break;
		case EV_AH_EINVAL:
			return_error(-1, FBR_EINVAL);
		}
	}

	while (0 == fiber->ev.arrived)
		fbr_yield(FBR_A);

	for (i = 0; NULL != events[i]; i++) {
		if (events[i]->arrived) {
			num++;
			finish_ev(FBR_A_ events[i]);
		} else
			cancel_ev(FBR_A_ events[i]);
	}
	return_success(num);
}

int fbr_ev_wait_one(FBR_P_ struct fbr_ev_base *one)
{
	struct fbr_fiber *fiber = CURRENT_FIBER;
	enum ev_action_hint hint;
	struct fbr_ev_base *events[] = {one, NULL};

	fiber->ev.arrived = 0;
	fiber->ev.waiting = events;

	hint = prepare_ev(FBR_A_ one);
	switch (hint) {
	case EV_AH_OK:
		break;
	case EV_AH_ARRIVED:
		goto finish;
	case EV_AH_EINVAL:
		return_error(-1, FBR_EINVAL);
	}

	while (0 == fiber->ev.arrived)
		fbr_yield(FBR_A);

finish:
	finish_ev(FBR_A_ one);
	return 0;
}

int fbr_ev_wait_one_wto(FBR_P_ struct fbr_ev_base *one, ev_tstamp timeout)
{
	int n_events;
	struct fbr_ev_base *events[] = {one, NULL, NULL};
	ev_timer timer;
	struct fbr_ev_watcher twatcher;
	struct fbr_destructor dtor = FBR_DESTRUCTOR_INITIALIZER;

	ev_timer_init(&timer, NULL, timeout, 0.);
	ev_timer_start(fctx->__p->loop, &timer);

	fbr_ev_watcher_init(FBR_A_ &twatcher,
			(struct ev_watcher *)&timer);
	dtor.func = watcher_timer_dtor;
	dtor.arg = &timer;
	fbr_destructor_add(FBR_A_ &dtor);
	events[1] = &twatcher.ev_base;

	n_events = fbr_ev_wait(FBR_A_ events);
	fbr_destructor_remove(FBR_A_ &dtor, 1 /* Call it? */);

	if (n_events > 0 && events[0]->arrived)
		return 0;
	errno = ETIMEDOUT;
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

int fbr_connect_wto(FBR_P_ int sockfd, const struct sockaddr *addr,
                   socklen_t addrlen, ev_tstamp timeout) {
	ev_io io;
	struct fbr_ev_watcher watcher;
	struct fbr_destructor dtor = FBR_DESTRUCTOR_INITIALIZER;
	int r, rc;
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
	rc = fbr_ev_wait_one_wto(FBR_A_ &watcher.ev_base, timeout);
	if (0 == rc) {
		len = sizeof(r);
		if (-1 == getsockopt(sockfd, SOL_SOCKET, SO_ERROR, (void *)&r, &len)) {
			r = -1;
		} else if ( 0 != r ) {
			errno = r;
			r = -1;
		}
	} else {
		r = -1;
		errno = ETIMEDOUT;
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

ssize_t fbr_read_wto(FBR_P_ int fd, void *buf, size_t count, ev_tstamp timeout)
{
	ssize_t r = 0;
	ev_io io;
	struct fbr_ev_watcher watcher;
	struct fbr_destructor dtor = FBR_DESTRUCTOR_INITIALIZER;
	int rc = 0;

	ev_io_init(&io, NULL, fd, EV_READ);
	ev_io_start(fctx->__p->loop, &io);
	dtor.func = watcher_io_dtor;
	dtor.arg = &io;
	fbr_destructor_add(FBR_A_ &dtor);

	fbr_ev_watcher_init(FBR_A_ &watcher, (ev_watcher *)&io);
	rc = fbr_ev_wait_one_wto(FBR_A_ &watcher.ev_base, timeout);

	fbr_destructor_remove(FBR_A_ &dtor, 0 /* Call it? */);
	if (0 == rc) {
		do {
			r = read(fd, buf, count);
		} while (-1 == r && EINTR == errno);
	}
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

ssize_t fbr_read_all_wto(FBR_P_ int fd, void *buf, size_t count, ev_tstamp timeout)
{
	ssize_t r;
	size_t done = 0;
	ev_io io;
	struct fbr_ev_watcher watcher, twatcher;
	struct fbr_destructor dtor = FBR_DESTRUCTOR_INITIALIZER;
	struct fbr_destructor dtor2 = FBR_DESTRUCTOR_INITIALIZER;
	struct fbr_ev_base *events[] = {NULL, NULL, NULL};
	ev_timer timer;

	ev_io_init(&io, NULL, fd, EV_READ);
	ev_io_start(fctx->__p->loop, &io);
	dtor.func = watcher_io_dtor;
	dtor.arg = &io;
	fbr_destructor_add(FBR_A_ &dtor);

	fbr_ev_watcher_init(FBR_A_ &watcher, (ev_watcher *)&io);
	events[0] = &watcher.ev_base;

	ev_timer_init(&timer, NULL, timeout, 0.);
	ev_timer_start(fctx->__p->loop, &timer);

	fbr_ev_watcher_init(FBR_A_ &twatcher,
			(struct ev_watcher *)&timer);
	dtor2.func = watcher_timer_dtor;
	dtor2.arg = &timer;
	fbr_destructor_add(FBR_A_ &dtor2);
	events[1] = &twatcher.ev_base;

	while (count != done) {
next:
		fbr_ev_wait(FBR_A_ events);
		if (events[1]->arrived)
			goto error;

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
	fbr_destructor_remove(FBR_A_ &dtor2, 0 /* Call it? */);
	ev_timer_stop(fctx->__p->loop, &timer);
	ev_io_stop(fctx->__p->loop, &io);
	return (ssize_t)done;

error:
	fbr_destructor_remove(FBR_A_ &dtor, 0 /* Call it? */);
	fbr_destructor_remove(FBR_A_ &dtor2, 0 /* Call it? */);
	ev_timer_stop(fctx->__p->loop, &timer);
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

ssize_t fbr_write_wto(FBR_P_ int fd, const void *buf, size_t count, ev_tstamp timeout)
{
	ssize_t r = 0;
	int rc;
	ev_io io;
	struct fbr_ev_watcher watcher;
	struct fbr_destructor dtor = FBR_DESTRUCTOR_INITIALIZER;

	ev_io_init(&io, NULL, fd, EV_WRITE);
	ev_io_start(fctx->__p->loop, &io);
	dtor.func = watcher_io_dtor;
	dtor.arg = &io;
	fbr_destructor_add(FBR_A_ &dtor);

	fbr_ev_watcher_init(FBR_A_ &watcher, (ev_watcher *)&io);
	rc = fbr_ev_wait_one_wto(FBR_A_ &watcher.ev_base, timeout);
	if (0 == rc) {
		do {
			r = write(fd, buf, count);
		} while (-1 == r && EINTR == errno);
	}

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

ssize_t fbr_write_all_wto(FBR_P_ int fd, const void *buf, size_t count, ev_tstamp timeout)
{
	ssize_t r;
	size_t done = 0;
	ev_io io;
	struct fbr_ev_watcher watcher, twatcher;
	struct fbr_destructor dtor = FBR_DESTRUCTOR_INITIALIZER;
	struct fbr_destructor dtor2 = FBR_DESTRUCTOR_INITIALIZER;
	struct fbr_ev_base *events[] = {NULL, NULL, NULL};
	ev_timer timer;

	ev_io_init(&io, NULL, fd, EV_WRITE);
	ev_io_start(fctx->__p->loop, &io);
	dtor.func = watcher_io_dtor;
	dtor.arg = &io;
	fbr_destructor_add(FBR_A_ &dtor);

	fbr_ev_watcher_init(FBR_A_ &watcher, (ev_watcher *)&io);
	events[0] = &watcher.ev_base;

	ev_timer_init(&timer, NULL, timeout, 0.);
	ev_timer_start(fctx->__p->loop, &timer);

	fbr_ev_watcher_init(FBR_A_ &twatcher,
			(struct ev_watcher *)&timer);
	dtor2.func = watcher_timer_dtor;
	dtor2.arg = &timer;
	fbr_destructor_add(FBR_A_ &dtor2);
	events[1] = &twatcher.ev_base;

	while (count != done) {
next:
		fbr_ev_wait(FBR_A_ events);
		if (events[1]->arrived) {
			errno = ETIMEDOUT;
			goto error;
		}

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
	fbr_destructor_remove(FBR_A_ &dtor2, 0 /* Call it? */);
	ev_timer_stop(fctx->__p->loop, &timer);
	ev_io_stop(fctx->__p->loop, &io);
	return (ssize_t)done;

error:
	fbr_destructor_remove(FBR_A_ &dtor, 0 /* Call it? */);
	fbr_destructor_remove(FBR_A_ &dtor2, 0 /* Call it? */);
	ev_timer_stop(fctx->__p->loop, &timer);
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

ssize_t fbr_recv(FBR_P_ int sockfd, void *buf, size_t len, int flags)
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

	return recv(sockfd, buf, len, flags);
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

ssize_t fbr_send(FBR_P_ int sockfd, const void *buf, size_t len, int flags)
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

	return send(sockfd, buf, len, flags);
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

static long get_page_size()
{
	static long sz;
	if (0 == sz)
		sz = sysconf(_SC_PAGESIZE);
	return sz;
}

static size_t round_up_to_page_size(size_t size)
{
	long sz = get_page_size();
	size_t remainder;
	remainder = size % sz;
	if (remainder == 0)
		return size;
	return size + sz - remainder;
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
		fiber->stack = malloc(stack_size);
		if (NULL == fiber->stack)
			err(EXIT_FAILURE, "malloc failed");
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
	fprintf(stderr, "libevfibers: fbr_calloc is deprecated\n");
	ptr = allocate_in_fiber(FBR_A_ nmemb * size, CURRENT_FIBER);
	memset(ptr, 0x00, nmemb * size);
	return ptr;
}

void *fbr_alloc(FBR_P_ size_t size)
{
	fprintf(stderr, "libevfibers: fbr_alloc is deprecated\n");
	return allocate_in_fiber(FBR_A_ size, CURRENT_FIBER);
}

void fbr_alloc_set_destructor(_unused_ FBR_P_ void *ptr,
		fbr_alloc_destructor_func_t func, void *context)
{
	struct mem_pool *pool_entry;
	fprintf(stderr, "libevfibers:"
		       " fbr_alloc_set_destructor is deprecated\n");
	pool_entry = (struct mem_pool *)ptr - 1;
	pool_entry->destructor = func;
	pool_entry->destructor_context = context;
}

void fbr_free(FBR_P_ void *ptr)
{
	fprintf(stderr, "libevfibers: fbr_free is deprecated\n");
	fbr_free_in_fiber(FBR_A_ CURRENT_FIBER, ptr, 1);
}

void fbr_free_nd(FBR_P_ void *ptr)
{
	fprintf(stderr, "libevfibers: fbr_free_nd is deprecated\n");
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

	assert(!fbr_id_eq(mutex->locked_by, CURRENT_FIBER_ID) &&
			"Mutex is already locked by current fiber");
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
	assert(fbr_id_eq(mutex->locked_by, CURRENT_FIBER_ID) &&
			"Can't unlock the mutex, locked by another fiber");

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
	assert(!fbr_id_isnull(mutex->locked_by));
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

	if (mutex && fbr_id_isnull(mutex->locked_by))
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

int fbr_vrb_init(struct fbr_vrb *vrb, size_t size, const char *file_pattern)
{
	int fd = -1;
	size_t sz = get_page_size();
	size = (size ? round_up_to_page_size(size) : sz);
	void *ptr = MAP_FAILED;
	char *temp_name = NULL;

	temp_name = strdup(file_pattern);
	if (!temp_name)
		return -1;
	//fctx->__p->vrb_file_pattern);
	vrb->mem_ptr_size = size * 2 + sz * 2;
	vrb->mem_ptr = mmap(NULL, vrb->mem_ptr_size, PROT_NONE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (MAP_FAILED == vrb->mem_ptr)
		goto error;
	vrb->lower_ptr = vrb->mem_ptr + sz;
	vrb->upper_ptr = vrb->lower_ptr + size;
	vrb->ptr_size = size;
	vrb->data_ptr = vrb->lower_ptr;
	vrb->space_ptr = vrb->lower_ptr;

	fd = mkstemp(temp_name);
	if (0 >= fd)
		goto error;

	if (0 > unlink(temp_name))
		goto error;
	free(temp_name);
	temp_name = NULL;

	if (0 > ftruncate(fd, size))
		goto error;

	ptr = mmap(vrb->lower_ptr, vrb->ptr_size, PROT_READ | PROT_WRITE,
			MAP_FIXED | MAP_SHARED, fd, 0);
	if (MAP_FAILED == ptr)
		goto error;
	if (ptr != vrb->lower_ptr)
		goto error;

	ptr = mmap(vrb->upper_ptr, vrb->ptr_size, PROT_READ | PROT_WRITE,
			MAP_FIXED | MAP_SHARED, fd, 0);
	if (MAP_FAILED == ptr)
		goto error;
	if (ptr != vrb->upper_ptr)
		goto error;

	close(fd);
	return 0;

error:
	if (MAP_FAILED != ptr)
		munmap(ptr, size);
	if (0 < fd)
		close(fd);
	if (vrb->mem_ptr)
		munmap(vrb->mem_ptr, vrb->mem_ptr_size);
	if (temp_name)
		free(temp_name);
	return -1;
}

int fbr_buffer_init(FBR_P_ struct fbr_buffer *buffer, size_t size)
{
	int rv;
	rv = fbr_vrb_init(&buffer->vrb, size, fctx->__p->buffer_file_pattern);
	if (rv)
		return_error(-1, FBR_EBUFFERMMAP);

	buffer->prepared_bytes = 0;
	buffer->waiting_bytes = 0;
	fbr_cond_init(FBR_A_ &buffer->committed_cond);
	fbr_cond_init(FBR_A_ &buffer->bytes_freed_cond);
	fbr_mutex_init(FBR_A_ &buffer->write_mutex);
	fbr_mutex_init(FBR_A_ &buffer->read_mutex);
	return_success(0);
}

void fbr_vrb_destroy(struct fbr_vrb *vrb)
{
	munmap(vrb->upper_ptr, vrb->ptr_size);
	munmap(vrb->lower_ptr, vrb->ptr_size);
	munmap(vrb->mem_ptr, vrb->mem_ptr_size);
}

void fbr_buffer_destroy(FBR_P_ struct fbr_buffer *buffer)
{
	fbr_vrb_destroy(&buffer->vrb);

	fbr_mutex_destroy(FBR_A_ &buffer->read_mutex);
	fbr_mutex_destroy(FBR_A_ &buffer->write_mutex);
	fbr_cond_destroy(FBR_A_ &buffer->committed_cond);
	fbr_cond_destroy(FBR_A_ &buffer->bytes_freed_cond);
}

void *fbr_buffer_alloc_prepare(FBR_P_ struct fbr_buffer *buffer, size_t size)
{
	if (size > fbr_buffer_size(FBR_A_ buffer))
		return_error(NULL, FBR_EINVAL);

	fbr_mutex_lock(FBR_A_ &buffer->write_mutex);

	while (buffer->prepared_bytes > 0)
		fbr_cond_wait(FBR_A_ &buffer->committed_cond,
				&buffer->write_mutex);

	assert(0 == buffer->prepared_bytes);

	buffer->prepared_bytes = size;

	while (fbr_buffer_free_bytes(FBR_A_ buffer) < size)
		fbr_cond_wait(FBR_A_ &buffer->bytes_freed_cond,
				&buffer->write_mutex);

	return fbr_buffer_space_ptr(FBR_A_ buffer);
}

void fbr_buffer_alloc_commit(FBR_P_ struct fbr_buffer *buffer)
{
	fbr_vrb_give(&buffer->vrb, buffer->prepared_bytes);
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
	if (size > fbr_buffer_size(FBR_A_ buffer))
		return_error(NULL, FBR_EINVAL);

	fbr_mutex_lock(FBR_A_ &buffer->read_mutex);

	while (fbr_buffer_bytes(FBR_A_ buffer) < size) {
		retval = fbr_cond_wait(FBR_A_ &buffer->committed_cond,
				&buffer->read_mutex);
		assert(0 == retval);
		(void)retval;
	}

	buffer->waiting_bytes = size;

	return_success(fbr_buffer_data_ptr(FBR_A_ buffer));
}

void fbr_buffer_read_advance(FBR_P_ struct fbr_buffer *buffer)
{
	fbr_vrb_take(&buffer->vrb, buffer->waiting_bytes);

	fbr_cond_signal(FBR_A_ &buffer->bytes_freed_cond);
	fbr_mutex_unlock(FBR_A_ &buffer->read_mutex);
}

void fbr_buffer_read_discard(FBR_P_ struct fbr_buffer *buffer)
{
	fbr_mutex_unlock(FBR_A_ &buffer->read_mutex);
}

int fbr_buffer_resize(FBR_P_ struct fbr_buffer *buffer, size_t size)
{
	int rv;
	fbr_mutex_lock(FBR_A_ &buffer->read_mutex);
	fbr_mutex_lock(FBR_A_ &buffer->write_mutex);
	rv = fbr_vrb_resize(&buffer->vrb, size, fctx->__p->buffer_file_pattern);
	fbr_mutex_unlock(FBR_A_ &buffer->write_mutex);
	fbr_mutex_unlock(FBR_A_ &buffer->read_mutex);
	if (rv)
		return_error(-1, FBR_EBUFFERMMAP);
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

static inline int wrap_ffsll(uint64_t val)
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
	fbr_key_t key = wrap_ffsll(fctx->__p->key_free_mask);
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

static int make_pipe(FBR_P_ int *r, int*w)
{
	int fds[2];
	int retval;
	retval = pipe(fds);
	if (-1 == retval)
		return_error(-1, FBR_ESYSTEM);
	*r = fds[0];
	*w = fds[1];
	return_success(0);
}

pid_t fbr_popen3(FBR_P_ const char *filename, char *const argv[],
		char *const envp[], const char *working_dir,
		int *stdin_w_ptr, int *stdout_r_ptr, int *stderr_r_ptr)
{
	pid_t pid;
	int stdin_r = 0, stdin_w = 0;
	int stdout_r = 0, stdout_w = 0;
	int stderr_r = 0, stderr_w = 0;
	int devnull;
	int retval;

	if (!stdin_w_ptr || !stdout_r_ptr || !stderr_r_ptr)
		devnull = open("/dev/null", O_WRONLY);

	retval = (stdin_w_ptr ? make_pipe(FBR_A_ &stdin_r, &stdin_w) : 0);
	if (retval)
		return retval;
	retval = (stdout_r_ptr ? make_pipe(FBR_A_ &stdout_r, &stdout_w) : 0);
	if (retval)
		return retval;
	retval = (stderr_r_ptr ? make_pipe(FBR_A_ &stderr_r, &stderr_w) : 0);
	if (retval)
		return retval;

	pid = fork();
	if (-1 == pid)
		return_error(-1, FBR_ESYSTEM);
	if (0 == pid) {
		/* Child */
		ev_break(EV_DEFAULT, EVBREAK_ALL);
		if (stdin_w_ptr) {
			retval = close(stdin_w);
			if (-1 == retval)
				err(EXIT_FAILURE, "close");
			retval = dup2(stdin_r, STDIN_FILENO);
			if (-1 == retval)
				err(EXIT_FAILURE, "dup2");
		} else {
			devnull = open("/dev/null", O_RDONLY);
			if (-1 == retval)
				err(EXIT_FAILURE, "open");
			retval = dup2(devnull, STDIN_FILENO);
			if (-1 == retval)
				err(EXIT_FAILURE, "dup2");
		}
		if (stdout_r_ptr) {
			retval = close(stdout_r);
			if (-1 == retval)
				err(EXIT_FAILURE, "close");
			retval = dup2(stdout_w, STDOUT_FILENO);
			if (-1 == retval)
				err(EXIT_FAILURE, "dup2");
		} else {
			devnull = open("/dev/null", O_WRONLY);
			if (-1 == retval)
				err(EXIT_FAILURE, "open");
			retval = dup2(devnull, STDOUT_FILENO);
			if (-1 == retval)
				err(EXIT_FAILURE, "dup2");
		}
		if (stderr_r_ptr) {
			retval = close(stderr_r);
			if (-1 == retval)
				err(EXIT_FAILURE, "close");
			retval = dup2(stderr_w, STDERR_FILENO);
			if (-1 == retval)
				err(EXIT_FAILURE, "dup2");
		} else {
			devnull = open("/dev/null", O_WRONLY);
			if (-1 == retval)
				err(EXIT_FAILURE, "open");
			retval = dup2(stderr_w, STDERR_FILENO);
			if (-1 == retval)
				err(EXIT_FAILURE, "dup2");
		}

		if (working_dir) {
			retval = chdir(working_dir);
			if (-1 == retval)
				err(EXIT_FAILURE, "chdir");
		}

		retval = execve(filename, argv, envp);
		if (-1 == retval)
			err(EXIT_FAILURE, "execve");

		errx(EXIT_FAILURE, "execve failed without error code");
	}
	/* Parent */
	if (stdin_w_ptr) {
		retval = close(stdin_r);
		if (-1 == retval)
			return_error(-1, FBR_ESYSTEM);
		retval = fbr_fd_nonblock(FBR_A_ stdin_w);
		if (retval)
			return retval;
	}
	if (stdout_r_ptr) {
		retval = close(stdout_w);
		if (-1 == retval)
			return_error(-1, FBR_ESYSTEM);
		retval = fbr_fd_nonblock(FBR_A_ stdout_r);
		if (retval)
			return retval;
	}
	if (stderr_r_ptr) {
		retval = close(stderr_w);
		if (-1 == retval)
			return_error(-1, FBR_ESYSTEM);
		retval = fbr_fd_nonblock(FBR_A_ stderr_r);
		if (retval)
			return retval;
	}

	fbr_log_d(FBR_A_ "child pid %d has been launched", pid);
	*stdin_w_ptr = stdin_w;
	*stdout_r_ptr = stdout_r;
	*stderr_r_ptr = stderr_r;
	return pid;
}

static void watcher_child_dtor(_unused_ FBR_P_ void *_arg)
{
	struct ev_child *w = _arg;
	ev_child_stop(fctx->__p->loop, w);
}

int fbr_waitpid(FBR_P_ pid_t pid)
{
	struct ev_child child;
	struct fbr_ev_watcher watcher;
	struct fbr_destructor dtor = FBR_DESTRUCTOR_INITIALIZER;
	ev_child_init(&child, NULL, pid, 0.);
	ev_child_start(fctx->__p->loop, &child);
	dtor.func = watcher_child_dtor;
	dtor.arg = &child;
	fbr_destructor_add(FBR_A_ &dtor);

	fbr_ev_watcher_init(FBR_A_ &watcher, (ev_watcher *)&child);
	fbr_ev_wait_one(FBR_A_ &watcher.ev_base);

	fbr_destructor_remove(FBR_A_ &dtor, 0 /* Call it? */);
	ev_child_stop(fctx->__p->loop, &child);
	return_success(child.rstatus);
}

int fbr_system(FBR_P_ const char *filename, char *const argv[],
		char *const envp[], const char *working_dir)
{
	pid_t pid;
	int retval;

	pid = fork();
	if (-1 == pid)
		return_error(-1, FBR_ESYSTEM);
	if (0 == pid) {
		/* Child */
		ev_break(EV_DEFAULT, EVBREAK_ALL);

		if (working_dir) {
			retval = chdir(working_dir);
			if (-1 == retval)
				err(EXIT_FAILURE, "chdir");
		}

		retval = execve(filename, argv, envp);
		if (-1 == retval)
			err(EXIT_FAILURE, "execve");

		errx(EXIT_FAILURE, "execve failed without error code");
	}
	/* Parent */

	fbr_log_d(FBR_A_ "child pid %d has been launched", pid);
	return fbr_waitpid(FBR_A_ pid);
}

#ifdef FBR_EIO_ENABLED

static struct ev_loop *eio_loop;
static ev_idle repeat_watcher;
static ev_async ready_watcher;

/* idle watcher callback, only used when eio_poll */
/* didn't handle all results in one call */
static void repeat(EV_P_ ev_idle *w, _unused_ int revents)
{
	if (eio_poll () != -1)
		ev_idle_stop(EV_A_ w);
}

/* eio has some results, process them */
static void ready(EV_P_ _unused_ ev_async *w, _unused_ int revents)
{
	if (eio_poll() == -1)
		ev_idle_start(EV_A_ &repeat_watcher);
}

/* wake up the event loop */
static void want_poll()
{
	ev_async_send(eio_loop, &ready_watcher);
}

void fbr_eio_init()
{
	if (NULL != eio_loop) {
		fprintf(stderr, "libevfibers: fbr_eio_init called twice");
		abort();
	}
	eio_loop = EV_DEFAULT;
	ev_idle_init(&repeat_watcher, repeat);
	ev_async_init(&ready_watcher, ready);
	ev_async_start(eio_loop, &ready_watcher);
	ev_unref(eio_loop);
	eio_init(want_poll, 0);
}

void fbr_ev_eio_init(FBR_P_ struct fbr_ev_eio *ev, eio_req *req)
{
	ev_base_init(FBR_A_ &ev->ev_base, FBR_EV_EIO);
	ev->req = req;
}

static void eio_req_dtor(_unused_ FBR_P_ void *_arg)
{
	eio_req *req = _arg;
	eio_cancel(req);
}

static int fiber_eio_cb(eio_req *req)
{
	struct fbr_fiber *fiber;
	struct fbr_ev_eio *ev = req->data;
	struct fbr_context *fctx = ev->ev_base.fctx;
	int retval;

	ENSURE_ROOT_FIBER;

	ev_unref(eio_loop);
	if (EIO_CANCELLED(req))
		return 0;

	retval = fbr_id_unpack(FBR_A_ &fiber, ev->ev_base.id);
	if (-1 == retval) {
		fbr_log_e(FBR_A_ "libevfibers: fiber is about to be called by"
			" the eio callback, but it's id is not valid: %s",
			fbr_strerror(FBR_A_ fctx->f_errno));
		abort();
	}

	post_ev(FBR_A_ fiber, &ev->ev_base);

	retval = fbr_transfer(FBR_A_ fbr_id_pack(fiber));
	assert(0 == retval);
	return 0;
}

#define FBR_EIO_PREP \
	eio_req *req; \
	struct fbr_ev_eio e_eio; \
	int retval; \
	struct fbr_destructor dtor = FBR_DESTRUCTOR_INITIALIZER; \
	ev_ref(eio_loop);

#define FBR_EIO_WAIT \
	if (NULL == req) { \
		ev_unref(eio_loop); \
		return_error(-1, FBR_EEIO); \
	} \
	dtor.func = eio_req_dtor; \
	dtor.arg = req; \
	fbr_destructor_add(FBR_A_ &dtor); \
	fbr_ev_eio_init(FBR_A_ &e_eio, req); \
	retval = fbr_ev_wait_one(FBR_A_ &e_eio.ev_base); \
	fbr_destructor_remove(FBR_A_ &dtor, 0 /* Call it? */); \
	if (retval) \
		return retval;

#define FBR_EIO_RESULT_CHECK \
	if (0 > req->result) { \
		errno = req->errorno; \
		return_error(-1, FBR_ESYSTEM); \
	}

#define FBR_EIO_RESULT_RET \
	FBR_EIO_RESULT_CHECK \
	return req->result;

int fbr_eio_open(FBR_P_ const char *path, int flags, mode_t mode, int pri)
{
	FBR_EIO_PREP;
	req = eio_open(path, flags, mode, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_truncate(FBR_P_ const char *path, off_t offset, int pri)
{
	FBR_EIO_PREP;
	req = eio_truncate(path, offset, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_chown(FBR_P_ const char *path, uid_t uid, gid_t gid, int pri)
{
	FBR_EIO_PREP;
	req = eio_chown(path, uid, gid, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_chmod(FBR_P_ const char *path, mode_t mode, int pri)
{
	FBR_EIO_PREP;
	req = eio_chmod(path, mode, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_mkdir(FBR_P_ const char *path, mode_t mode, int pri)
{
	FBR_EIO_PREP;
	req = eio_mkdir(path, mode, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_rmdir(FBR_P_ const char *path, int pri)
{
	FBR_EIO_PREP;
	req = eio_rmdir(path, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_unlink(FBR_P_ const char *path, int pri)
{
	FBR_EIO_PREP;
	req = eio_unlink(path, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_utime(FBR_P_ const char *path, eio_tstamp atime, eio_tstamp mtime,
		int pri)
{
	FBR_EIO_PREP;
	req = eio_utime(path, atime, mtime, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_mknod(FBR_P_ const char *path, mode_t mode, dev_t dev, int pri)
{
	FBR_EIO_PREP;
	req = eio_mknod(path, mode, dev, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_link(FBR_P_ const char *path, const char *new_path, int pri)
{
	FBR_EIO_PREP;
	req = eio_link(path, new_path, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_symlink(FBR_P_ const char *path, const char *new_path, int pri)
{
	FBR_EIO_PREP;
	req = eio_symlink(path, new_path, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_rename(FBR_P_ const char *path, const char *new_path, int pri)
{
	FBR_EIO_PREP;
	req = eio_rename(path, new_path, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_mlock(FBR_P_ void *addr, size_t length, int pri)
{
	FBR_EIO_PREP;
	req = eio_mlock(addr, length, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_close(FBR_P_ int fd, int pri)
{
	FBR_EIO_PREP;
	req = eio_close(fd, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_sync(FBR_P_ int pri)
{
	FBR_EIO_PREP;
	req = eio_sync(pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_fsync(FBR_P_ int fd, int pri)
{
	FBR_EIO_PREP;
	req = eio_fsync(fd, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_fdatasync(FBR_P_ int fd, int pri)
{
	FBR_EIO_PREP;
	req = eio_fdatasync(fd, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_futime(FBR_P_ int fd, eio_tstamp atime, eio_tstamp mtime, int pri)
{
	FBR_EIO_PREP;
	req = eio_futime(fd, atime, mtime, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_ftruncate(FBR_P_ int fd, off_t offset, int pri)
{
	FBR_EIO_PREP;
	req = eio_ftruncate(fd, offset, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_fchmod(FBR_P_ int fd, mode_t mode, int pri)
{
	FBR_EIO_PREP;
	req = eio_fchmod(fd, mode, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_fchown(FBR_P_ int fd, uid_t uid, gid_t gid, int pri)
{
	FBR_EIO_PREP;
	req = eio_fchown(fd, uid, gid, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_dup2(FBR_P_ int fd, int fd2, int pri)
{
	FBR_EIO_PREP;
	req = eio_dup2(fd, fd2, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

ssize_t fbr_eio_seek(FBR_P_ int fd, off_t offset, int whence, int pri)
{
	FBR_EIO_PREP;
	req = eio_seek(fd, offset, whence, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_CHECK;
	return req->offs;
}

ssize_t fbr_eio_read(FBR_P_ int fd, void *buf, size_t length, off_t offset,
		int pri)
{
	FBR_EIO_PREP;
	req = eio_read(fd, buf, length, offset, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

ssize_t fbr_eio_write(FBR_P_ int fd, void *buf, size_t length, off_t offset,
		int pri)
{
	FBR_EIO_PREP;
	req = eio_write(fd, buf, length, offset, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_mlockall(FBR_P_ int flags, int pri)
{
	FBR_EIO_PREP;
	req = eio_mlockall(flags, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_msync(FBR_P_ void *addr, size_t length, int flags, int pri)
{
	FBR_EIO_PREP;
	req = eio_msync(addr, length, flags, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_readlink(FBR_P_ const char *path, char *buf, size_t size, int pri)
{
	FBR_EIO_PREP;
	req = eio_readlink(path, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_CHECK;
	strncpy(buf, req->ptr2, min(size, (size_t)req->result));
	return req->result;
}

int fbr_eio_realpath(FBR_P_ const char *path, char *buf, size_t size, int pri)
{
	FBR_EIO_PREP;
	req = eio_realpath(path, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_CHECK;
	strncpy(buf, req->ptr2, min(size, (size_t)req->result));
	return req->result;
}

int fbr_eio_stat(FBR_P_ const char *path, EIO_STRUCT_STAT *statdata, int pri)
{
	EIO_STRUCT_STAT *st;
	FBR_EIO_PREP;
	req = eio_stat(path, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_CHECK;
	st = (EIO_STRUCT_STAT *)req->ptr2;
	memcpy(statdata, st, sizeof(*st));
	return req->result;
}

int fbr_eio_lstat(FBR_P_ const char *path, EIO_STRUCT_STAT *statdata, int pri)
{
	EIO_STRUCT_STAT *st;
	FBR_EIO_PREP;
	req = eio_lstat(path, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_CHECK;
	st = (EIO_STRUCT_STAT *)req->ptr2;
	memcpy(statdata, st, sizeof(*st));
	return req->result;
}

int fbr_eio_fstat(FBR_P_ int fd, EIO_STRUCT_STAT *statdata, int pri)
{
	EIO_STRUCT_STAT *st;
	FBR_EIO_PREP;
	req = eio_fstat(fd, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_CHECK;
	st = (EIO_STRUCT_STAT *)req->ptr2;
	memcpy(statdata, st, sizeof(*st));
	return req->result;
}

int fbr_eio_statvfs(FBR_P_ const char *path, EIO_STRUCT_STATVFS *statdata,
		int pri)
{
	EIO_STRUCT_STATVFS *st;
	FBR_EIO_PREP;
	req = eio_statvfs(path, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_CHECK;
	st = (EIO_STRUCT_STATVFS *)req->ptr2;
	memcpy(statdata, st, sizeof(*st));
	return req->result;
}

int fbr_eio_fstatvfs(FBR_P_ int fd, EIO_STRUCT_STATVFS *statdata, int pri)
{
	EIO_STRUCT_STATVFS *st;
	FBR_EIO_PREP;
	req = eio_fstatvfs(fd, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_CHECK;
	st = (EIO_STRUCT_STATVFS *)req->ptr2;
	memcpy(statdata, st, sizeof(*st));
	return req->result;
}

int fbr_eio_sendfile(FBR_P_ int out_fd, int in_fd, off_t in_offset,
		size_t length, int pri)
{
	FBR_EIO_PREP;
	req = eio_sendfile(out_fd, in_fd, in_offset, length, pri, fiber_eio_cb,
			&e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_readahead(FBR_P_ int fd, off_t offset, size_t length, int pri)
{
	FBR_EIO_PREP;
	req = eio_readahead(fd, offset, length, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_syncfs(FBR_P_ int fd, int pri)
{
	FBR_EIO_PREP;
	req = eio_syncfs(fd, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_sync_file_range(FBR_P_ int fd, off_t offset, size_t nbytes,
			unsigned int flags, int pri)
{
	FBR_EIO_PREP;
	req = eio_sync_file_range(fd, offset, nbytes, flags, pri, fiber_eio_cb,
			&e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

int fbr_eio_fallocate(FBR_P_ int fd, int mode, off_t offset, off_t len, int pri)
{
	FBR_EIO_PREP;
	req = eio_fallocate(fd, mode, offset, len, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

static void custom_execute_cb(eio_req *req)
{
	struct fbr_ev_eio *ev = req->data;
	req->result = ev->custom_func(ev->custom_arg);
}

eio_ssize_t fbr_eio_custom(FBR_P_ fbr_eio_custom_func_t func, void *data,
		int pri)
{
	FBR_EIO_PREP;
	e_eio.custom_func = func;
	e_eio.custom_arg = data;
	req = eio_custom(custom_execute_cb, pri, fiber_eio_cb, &e_eio);
	FBR_EIO_WAIT;
	FBR_EIO_RESULT_RET;
}

#else

void fbr_eio_init(FBR_PU)
{
	fbr_log_e(FBR_A_ "libevfibers: libeio support is not compiled");
	abort();
}

#endif
