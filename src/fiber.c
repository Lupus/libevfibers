/********************************************************************

  Copyright 2012 Konstantin Olkhovskiy <lupus@oxnull.net>

  This file is part of Mersenne.

  Mersenne is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  any later version.

  Mersenne is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Mersenne.  If not, see <http://www.gnu.org/licenses/>.

 ********************************************************************/
#include <sys/mman.h>
#include <assert.h>
#include <errno.h>
#include <utlist.h>
#include <stdio.h>
#include <valgrind/valgrind.h>

#include <evfibers_private/fiber.h>

#define ENSURE_ROOT_FIBER do { assert(fctx->__p->sp->fiber == &fctx->__p->root); } while(0);
#define CURRENT_FIBER fctx->__p->sp->fiber
#define CALLED_BY_ROOT ((fctx->__p->sp - 1)->fiber == &fctx->__p->root)

static void mutex_async_cb(EV_P_ ev_async *w, int revents)
{
	struct fbr_context *fctx;
	struct fbr_mutex *mutex, *tmp;
	struct fbr_mutex_pending *pending;
	fctx = (struct fbr_context *)w->data;

	ENSURE_ROOT_FIBER;

	DL_FOREACH_SAFE(fctx->__p->mutex_list, mutex, tmp) {
		pending = mutex->pending;
		assert(NULL != pending);
		DL_DELETE(mutex->pending, pending);
		if(NULL == mutex->pending)
			DL_DELETE(fctx->__p->mutex_list, mutex);
		fbr_call_noinfo(FBR_A_ pending->fiber, 0);
	}
}

void fbr_init(FBR_P_ struct ev_loop *loop)
{
	fctx->__p = malloc(sizeof(struct fbr_context_private));
	fctx->__p->reclaimed = NULL;
	fctx->__p->multicalls = NULL;
	fctx->__p->root.name = "root";
	fctx->__p->root.children = NULL;
	fctx->__p->root.pool = NULL;
	coro_create(&fctx->__p->root.ctx, NULL, NULL, NULL, 0);
	fctx->__p->sp = fctx->__p->stack;
	fctx->__p->sp->fiber = &fctx->__p->root;
	fill_trace_info(&fctx->__p->sp->tinfo);
	fctx->__p->loop = loop;
	fctx->__p->mutex_async.data = fctx;
	ev_async_init(&fctx->__p->mutex_async, mutex_async_cb);
	ev_async_start(loop, &fctx->__p->mutex_async);
}

static void reclaim_children(FBR_P_ struct fbr_fiber *fiber)
{
	struct fbr_fiber *child;
	DL_FOREACH(fiber->children, child) {
		fbr_reclaim(FBR_A_ child);
	}
}

void fbr_destroy(FBR_P)
{
	struct fbr_fiber *fiber, *tmp;
	struct fbr_multicall *call, *tmp2;
	
	ev_async_stop(fctx->__p->loop, &fctx->__p->mutex_async);

	reclaim_children(FBR_A_ &fctx->__p->root);

	DL_FOREACH_SAFE(fctx->__p->reclaimed, fiber, tmp) {
		if(0 != munmap(fiber->stack, fiber->stack_size))
			err(EXIT_FAILURE, "munmap");
		free(fiber);
	}
	HASH_ITER(hh, fctx->__p->multicalls, call, tmp2) {
		HASH_DEL(fctx->__p->multicalls, call);
		free(call);
	}
	free(fctx->__p);
}

static void ev_wakeup_io(EV_P_ ev_io *w, int event)
{
	struct fbr_context *fctx;
	struct fbr_fiber *fiber;
	fiber = container_of(w, struct fbr_fiber, w_io);
	fctx = (struct fbr_context *)w->data;

	ENSURE_ROOT_FIBER;
	if(1 != fiber->w_io_expected) {
		fprintf(stderr, "libevfibers: fiber ``%s'' is about to be woken "
				"up by an io event but it does not expect "
				"this.\n", fiber->name);
		fprintf(stderr, "libevfibers: last registered io request for "
				"this fiber was:\n");
		fprintf(stderr, "--- begin trace ---\n");
		print_trace_info(&fiber->w_io_tinfo);
		fprintf(stderr, "--- end trace ---\n");
		abort();
	}

	fbr_call_noinfo(FBR_A_ fiber, 0);
}

static void ev_wakeup_timer(EV_P_ ev_timer *w, int event)
{
	struct fbr_context *fctx;
	struct fbr_fiber *fiber;
	fiber = container_of(w, struct fbr_fiber, w_timer);
	fctx = (struct fbr_context *)w->data;

	ENSURE_ROOT_FIBER;
	if(1 != fiber->w_timer_expected) {
		fprintf(stderr, "libevfibers: fiber ``%s'' is about to be woken "
				"up by a timer event but it does not expect "
				"this.\n", fiber->name);
		fprintf(stderr, "libevfibers: last registered timer request for "
				"this fiber was:\n");
		fprintf(stderr, "--- begin trace ---\n");
		print_trace_info(&fiber->w_timer_tinfo);
		fprintf(stderr, "--- end trace ---\n");
		abort();
	}

	fbr_call_noinfo(FBR_A_ fiber, 0);
}

static inline int ptrcmp(void *p1, void *p2)
{
	return !(p1 == p2);
}

static void unsubscribe_all(FBR_P_ struct fbr_fiber *fiber)
{
	struct fbr_multicall *call;
	struct fbr_fiber *tmp;
	for(call=fctx->__p->multicalls; call != NULL; call=call->hh.next) {
		DL_SEARCH(call->fibers, tmp, fiber, ptrcmp);
		if(tmp == fiber)
			DL_DELETE(call->fibers, fiber);
	}
}

static void fbr_free_in_fiber(FBR_P_ struct fbr_fiber *fiber, void *ptr)
{
	struct fbr_mem_pool *pool_entry = NULL;
	if(NULL == ptr)
		return;
	pool_entry = (struct fbr_mem_pool *)ptr - 1;
	if(pool_entry->ptr != pool_entry) {
		fprintf(stderr, "libevfibers: address 0x%lx does not look like "
				"fiber memory pool entry\n", (unsigned long)ptr);
		if(!RUNNING_ON_VALGRIND)
			abort();
	}
	DL_DELETE(fiber->pool, pool_entry);
	if(pool_entry->destructor)
		pool_entry->destructor(ptr, pool_entry->destructor_context);
	free(pool_entry);
}

static void fiber_cleanup(FBR_P_ struct fbr_fiber *fiber)
{
	struct fbr_mem_pool *p_elt, *p_tmp;
	//coro_destroy(&fiber->ctx);
	unsubscribe_all(FBR_A_ fiber);
	ev_io_stop(fctx->__p->loop, &fiber->w_io);
	ev_timer_stop(fctx->__p->loop, &fiber->w_timer);
	DL_FOREACH_SAFE(fiber->pool, p_elt, p_tmp) {
		fbr_free_in_fiber(FBR_A_ fiber, p_elt + 1);
	}
}

void fbr_reclaim(FBR_P_ struct fbr_fiber *fiber)
{
	if(fiber->reclaimed)
		return;
	fill_trace_info(&fiber->reclaim_tinfo);
	reclaim_children(FBR_A_ fiber);
	fiber_cleanup(FBR_A_ fiber);
	fiber->reclaimed = 1;
	LL_PREPEND(fctx->__p->reclaimed, fiber);
}

int fbr_is_reclaimed(FBR_P_ struct fbr_fiber *fiber)
{
	return fiber->reclaimed;
}

static void fiber_prepare(FBR_P_ struct fbr_fiber *fiber)
{
	ev_init(&fiber->w_io, ev_wakeup_io);
	ev_init(&fiber->w_timer, ev_wakeup_timer);
	fiber->w_io.data = FBR_A;
	fiber->w_timer.data = FBR_A;
	fiber->reclaimed = 0;
}

static void call_wrapper(FBR_P_ void (*func) (FBR_P))
{
	struct fbr_fiber *fiber = CURRENT_FIBER;
	fiber_prepare(FBR_A_ fiber);

	fiber->func(FBR_A);
	
	fbr_reclaim(FBR_A_ fiber);
	fbr_yield(FBR_A);
}

struct fbr_fiber_arg fbr_arg_i(int i)
{
	return fbr_arg_i_cb(i, NULL);
}

struct fbr_fiber_arg fbr_arg_v(void *v)
{
	return fbr_arg_v_cb(v, NULL);
}

struct fbr_fiber_arg fbr_arg_i_cb(int i, fbr_arg_callback_t cb)
{
	struct fbr_fiber_arg arg;
	arg.i = i;
	arg.cb = cb;
	return arg;
}

struct fbr_fiber_arg fbr_arg_v_cb(void *v, fbr_arg_callback_t cb)
{
	struct fbr_fiber_arg arg;
	arg.v = v;
	arg.cb = cb;
	return arg;
}

static struct fbr_multicall * get_multicall(FBR_P_ int mid)
{
	struct fbr_multicall *call;
	HASH_FIND_INT(fctx->__p->multicalls, &mid, call);
	if(NULL == call) {
		call = malloc(sizeof(struct fbr_multicall));
		call->fibers = NULL;
		call->mid = mid;
		HASH_ADD_INT(fctx->__p->multicalls, mid, call);
	}
	return call;
}

void fbr_subscribe(FBR_P_ int mid)
{
	struct fbr_multicall *call = get_multicall(FBR_A_ mid);
	DL_APPEND(call->fibers, CURRENT_FIBER);
}

void fbr_unsubscribe(FBR_P_ int mid)
{
	struct fbr_multicall *call = get_multicall(FBR_A_ mid);
	struct fbr_fiber *fiber = CURRENT_FIBER;
	struct fbr_fiber *tmp;
	DL_SEARCH(call->fibers, tmp, fiber, ptrcmp);
	if(tmp == fiber)
		DL_DELETE(call->fibers, fiber);
}

void fbr_unsubscribe_all(FBR_P)
{
	unsubscribe_all(FBR_A_ CURRENT_FIBER);
}

void fbr_vcall(FBR_P_ struct fbr_fiber *callee, int argnum, va_list ap)
{
	fbr_vcall_context(FBR_A_ callee, NULL, 1, argnum, ap);
}

static void * allocate_in_fiber(FBR_P_ size_t size, struct fbr_fiber *in)
{
	struct fbr_mem_pool *pool_entry;
	pool_entry = malloc(size + sizeof(struct fbr_mem_pool));
	if(NULL == pool_entry) {
		fprintf(stderr, "libevfibers: unable to allocate %lu bytes\n",
				size + sizeof(struct fbr_mem_pool));
		abort();
	}
	pool_entry->ptr = pool_entry;
	pool_entry->destructor = NULL;
	pool_entry->destructor_context = NULL;
	DL_APPEND(in->pool, pool_entry);
	return pool_entry + 1;
}

void fbr_vcall_context(FBR_P_ struct fbr_fiber *callee, void *context,
		int leave_info, int argnum, va_list ap)
{
	struct fbr_fiber *caller = fctx->__p->sp->fiber;
	int i;
	struct fbr_call_info *info;
	
	if(argnum >= FBR_MAX_ARG_NUM) {
		fprintf(stderr, "libevfibers: attempt to pass %d argumens while "
				"FBR_MAX_ARG_NUM is %d, aborting\n", argnum,
				FBR_MAX_ARG_NUM);
		abort();
	}

	if(1 == callee->reclaimed) {
		fprintf(stderr, "libevfibers: fiber 0x%lu is about to be called "
				"but it was reclaimed here:\n", (long unsigned)callee);
		print_trace_info(&callee->reclaim_tinfo);
		abort();
	}

	fctx->__p->sp++;

	fctx->__p->sp->fiber = callee;
	fill_trace_info(&fctx->__p->sp->tinfo);

	if(0 == leave_info) {
		coro_transfer(&caller->ctx, &callee->ctx);
		return;
	}

	info = fbr_alloc(FBR_A_ sizeof(struct fbr_call_info));
	info->caller = caller;
	info->argc = argnum;
	for(i = 0; i < argnum; i++) {
		info->argv[i] = va_arg(ap, struct fbr_fiber_arg);
		if(NULL != info->argv[i].cb)
			info->argv[i].cb(context, info->argv + i);
	}

	DL_APPEND(callee->call_list, info);
	callee->call_list_size++;
	if(callee->call_list_size >= FBR_CALL_LIST_WARN) {
		fprintf(stderr, "libevfibers: call list for ``%s'' contains %lu"
				" elements, which looks suspicious. Is anyone"
				" fetching the calls?\n", callee->name,
				callee->call_list_size);
		fbr_dump_stack(FBR_A);
	}

	coro_transfer(&caller->ctx, &callee->ctx);
}

void fbr_call_noinfo(FBR_P_ struct fbr_fiber *callee, int argnum, ...)
{
	va_list ap;
	va_start(ap, argnum);
	fbr_vcall_context(FBR_A_ callee, NULL, 0, argnum, ap);
	va_end(ap);
}

void fbr_call(FBR_P_ struct fbr_fiber *callee, int argnum, ...)
{
	va_list ap;
	va_start(ap, argnum);
	fbr_vcall(FBR_A_ callee, argnum, ap);
	va_end(ap);
}

void fbr_call_context(FBR_P_ struct fbr_fiber *callee, void *context,
		int leave_info, int argnum, ...)
{
	va_list ap;
	va_start(ap, argnum);
	fbr_vcall_context(FBR_A_ callee, context, leave_info, argnum, ap);
	va_end(ap);
}

void fbr_multicall(FBR_P_ int mid, int argnum, ...)
{
	struct fbr_multicall *call = get_multicall(FBR_A_ mid);
	struct fbr_fiber *fiber;
	va_list ap;
	DL_FOREACH(call->fibers,fiber) {
		va_start(ap, argnum);
		fbr_vcall(FBR_A_ fiber, argnum, ap);
		va_end(ap);
	}
}

void fbr_multicall_context(FBR_P_ int mid, void *context, int leave_info,
		int argnum, ...)
{
	struct fbr_multicall *call = get_multicall(FBR_A_ mid);
	struct fbr_fiber *fiber;
	va_list ap;
	DL_FOREACH(call->fibers,fiber) {
		va_start(ap, argnum);
		fbr_vcall_context(FBR_A_ fiber, context, leave_info, argnum, ap);
		va_end(ap);
	}
}

int fbr_next_call_info(FBR_P_ struct fbr_call_info **info_ptr)
{
	struct fbr_fiber *fiber = CURRENT_FIBER;
	struct fbr_call_info *tmp;

	if(NULL == fiber->call_list)
		return 0;
	tmp = fiber->call_list;
	DL_DELETE(fiber->call_list, fiber->call_list);
	fiber->call_list_size--;

	if(NULL == info_ptr)
		fbr_free(FBR_A_ tmp);
	else {
		if(NULL != *info_ptr)
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
	fill_trace_info(&fiber->w_io_tinfo);
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
	if(!CALLED_BY_ROOT) {
		errno = EINTR;
		r = -1;
		goto finish;
	}
	do {
		r = read(fd, buf, count);
	} while(-1 == r && EINTR == errno);

finish:
	io_stop(FBR_A_ fiber);
	return r;
}

ssize_t fbr_read_all(FBR_P_ int fd, void *buf, size_t count)
{
	struct fbr_fiber *fiber = CURRENT_FIBER;
	ssize_t r;
	ssize_t done = 0;

	io_start(FBR_A_ fiber, fd, EV_READ);
	while (count != done) {
next:
		fbr_yield(FBR_A);
		if(!CALLED_BY_ROOT)
			continue;
		for(;;) {
			r = read(fd, buf + done, count - done);
			if (-1 == r) {
				switch(errno) {
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
		if(0 == r)
			break;
		done += r;
	}
	io_stop(FBR_A_ fiber);
	return done;

error:
	io_stop(FBR_A_ fiber);
	return -1;
}

ssize_t fbr_readline(FBR_P_ int fd, void *buffer, size_t n)
{
    ssize_t num_read;                    /* # of bytes fetched by last read() */
    size_t total_read;                     /* Total bytes read so far */
    char *buf;
    char ch;

    if (n <= 0 || buffer == NULL) {
        errno = EINVAL;
        return -1;
    }

    buf = buffer;                       /* No pointer arithmetic on "void *" */

    total_read = 0;
    for (;;) {
        num_read = fbr_read(FBR_A_ fd, &ch, 1);

        if (num_read == -1) {
            if (errno == EINTR)         /* Interrupted --> restart read() */
                continue;
            else
                return -1;              /* Some other error */

        } else if (num_read == 0) {      /* EOF */
            if (total_read == 0)           /* No bytes read; return 0 */
                return 0;
            else                        /* Some bytes read; add '\0' */
                break;

        } else {                        /* 'numRead' must be 1 if we get here */
            if (total_read < n - 1) {      /* Discard > (n - 1) bytes */
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
	if(!CALLED_BY_ROOT) {
		errno = EINTR;
		r = -1;
		goto finish;
	}
	do {		
		r = write(fd, buf, count);
	} while(-1 == r && EINTR == errno);

finish:
	io_stop(FBR_A_ fiber);
	return r;
}

ssize_t fbr_write_all(FBR_P_ int fd, const void *buf, size_t count)
{
	struct fbr_fiber *fiber = CURRENT_FIBER;
	ssize_t r;
	ssize_t done = 0;

	io_start(FBR_A_ fiber, fd, EV_WRITE);
	while (count != done) {
next:
		fbr_yield(FBR_A);
		if(!CALLED_BY_ROOT)
			continue;
		for(;;) {
			r = write(fd, buf + done, count - done);
			if (-1 == r) {
				switch(errno) {
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
	return done;

error:
	io_stop(FBR_A_ fiber);
	return -1;
}

ssize_t fbr_recvfrom(FBR_P_ int sockfd, void *buf, size_t len, int flags, struct
		sockaddr *src_addr, socklen_t *addrlen)
{
	struct fbr_fiber *fiber = CURRENT_FIBER;
	int nbytes;

	io_start(FBR_A_ fiber, sockfd, EV_READ);
	fbr_yield(FBR_A);
	io_stop(FBR_A_ fiber);
	if(CALLED_BY_ROOT) {
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
	if(CALLED_BY_ROOT) {
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
	if(!CALLED_BY_ROOT) {
		io_stop(FBR_A_ fiber);
		errno = EINTR;
		return -1;
	}
	do {
		r = accept(sockfd, addr, addrlen);
	} while(-1 == r && EINTR == errno);
	
	io_stop(FBR_A_ fiber);

	return r;
}

static void timer_start(FBR_P_ struct fbr_fiber *fiber, ev_tstamp timeout,
		ev_tstamp repeat)
{
	ev_timer_set(&fiber->w_timer, timeout, repeat);
	ev_timer_start(fctx->__p->loop, &fiber->w_timer);
	fiber->w_timer_expected = 1;
	fill_trace_info(&fiber->w_timer_tinfo);
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
	if(CALLED_BY_ROOT)
		return 0.;
	return expected - ev_now(fctx->__p->loop);
}

static size_t round_up_to_page_size(size_t size)
{
	static long sz = 0;
	size_t remainder;
	if(0 == sz)
		sz = sysconf(_SC_PAGESIZE);
	remainder = size % sz;
	if(remainder == 0)
		return size;
	return size + sz - remainder;
}

struct fbr_fiber * fbr_create(FBR_P_ const char *name, void (*func) (FBR_P),
		size_t stack_size)
{
	struct fbr_fiber *fiber;
	if(fctx->__p->reclaimed) {
		fiber = fctx->__p->reclaimed;
		LL_DELETE(fctx->__p->reclaimed, fctx->__p->reclaimed);
	} else {
		fiber = malloc(sizeof(struct fbr_fiber));
		memset(fiber, 0x00, sizeof(struct fbr_fiber));
		if(0 == stack_size) stack_size = FBR_STACK_SIZE;
		stack_size = round_up_to_page_size(stack_size);
		fiber->stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if(MAP_FAILED == fiber->stack)
			err(EXIT_FAILURE, "mmap failed");
		fiber->stack_size = stack_size;
		(void)VALGRIND_STACK_REGISTER(fiber->stack, fiber->stack +
				stack_size);
	}
	coro_create(&fiber->ctx, (coro_func)call_wrapper, FBR_A, fiber->stack,
			FBR_STACK_SIZE);
	fiber->w_io_expected = 0;
	fiber->w_timer_expected = 0;
	fiber->reclaimed = 0;
	fiber->call_list = NULL;
	fiber->call_list_size = 0;
	fiber->children = NULL;
	fiber->pool = NULL;
	fiber->name = name;
	fiber->func = func;
	DL_APPEND(CURRENT_FIBER->children, fiber);
	fiber->parent = CURRENT_FIBER;
	return fiber;
}

void * fbr_calloc(FBR_P_ unsigned int nmemb, size_t size)
{
	void *ptr;
	ptr = allocate_in_fiber(FBR_A_ nmemb * size, CURRENT_FIBER);
	memset(ptr, 0x00, nmemb * size);
	return ptr;
}

void * fbr_alloc(FBR_P_ size_t size)
{
	return allocate_in_fiber(FBR_A_ size, CURRENT_FIBER);
}

void fbr_alloc_set_destructor(FBR_P_ void *ptr, fbr_alloc_destructor_func
		func, void *context)
{
	struct fbr_mem_pool *pool_entry;
	pool_entry = (struct fbr_mem_pool *)ptr - 1;
	pool_entry->destructor = func;
	pool_entry->destructor_context = context;
}

void fbr_free(FBR_P_ void *ptr)
{
	fbr_free_in_fiber(FBR_A_ CURRENT_FIBER, ptr);
}

void fbr_dump_stack(FBR_P)
{
	struct fbr_stack_item *ptr = fctx->__p->sp;
		fprintf(stderr, "%s\n%s\n", "Fiber call stack:",
				"-------------------------------");
	while(ptr >= fctx->__p->stack) {
		fprintf(stderr, "fiber_call: 0x%lx\t%s\n",
				(long unsigned int)ptr->fiber,
				ptr->fiber->name);
		print_trace_info(&ptr->tinfo);
		fprintf(stderr, "%s\n", "-------------------------------");
		ptr--;
	}
}

static void mutex_pending_destructor(void *ptr, void *context)
{
	struct fbr_mutex_pending *pending = ptr;
	struct fbr_mutex *mutex = context;
	DL_DELETE(mutex->pending, pending);
}

struct fbr_mutex * fbr_mutex_create(FBR_P)
{
	struct fbr_mutex *mutex;
	mutex = malloc(sizeof(struct fbr_mutex));
	mutex->locked = 0;
	mutex->pending = NULL;
	return mutex;
}

void fbr_mutex_lock(FBR_P_ struct fbr_mutex * mutex)
{
	struct fbr_mutex_pending *pending;
	if(0 == mutex->locked) {
		mutex->locked = 1;
		return;
	}
	pending = fbr_alloc(FBR_A_ sizeof(struct fbr_mutex_pending));
	fbr_alloc_set_destructor(FBR_A_ pending, mutex_pending_destructor, mutex);
	pending->fiber = CURRENT_FIBER;
	DL_APPEND(mutex->pending, pending);
	fbr_yield(FBR_A);
	while(!CALLED_BY_ROOT) fbr_yield(FBR_A);
}

int fbr_mutex_trylock(FBR_P_ struct fbr_mutex * mutex)
{
	if(0 == mutex->locked) {
		mutex->locked = 1;
		return 1;
	}
	return 0;
}

void fbr_mutex_unlock(FBR_P_ struct fbr_mutex * mutex)
{
	if(NULL == mutex->pending) {
		mutex->locked = 0;
		return;
	}
	DL_APPEND(fctx->__p->mutex_list, mutex);
	ev_async_send(fctx->__p->loop, &fctx->__p->mutex_async);
}

void fbr_mutex_destroy(FBR_P_ struct fbr_mutex * mutex)
{
	free(mutex);
}
