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

#ifndef _FBR_FIBER_PRIVATE_H_
#define _FBR_FIBER_PRIVATE_H_

#include <stdarg.h>
#include <sys/queue.h>
#include <evfibers/fiber.h>
#include <evfibers_private/trace.h>
#include <coro.h>
#include <uthash.h>

#define obstack_chunk_alloc malloc
#define obstack_chunk_free free

#define FBR_CALL_LIST_WARN 1000

#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type, member) );	\
		})

#define max(a,b) ({						\
		const typeof(a) __tmp_a = (a);			\
		const typeof(b) __tmp_b = (b);			\
		__tmp_a > __tmp_b ? __tmp_a : __tmp_b;		\
		})

#define _unused_ __attribute__((unused))

struct mem_pool {
	void *ptr;
	fbr_alloc_destructor_func_t destructor;
	void *destructor_context;
	LIST_ENTRY(mem_pool) entries;
};

LIST_HEAD(mem_pool_list, mem_pool);

struct fiber_id_tailq_i {
	fbr_id_t id;
	struct fbr_ev_base *ev;
	TAILQ_ENTRY(fiber_id_tailq_i) entries;
};

TAILQ_HEAD(fiber_id_tailq, fiber_id_tailq_i);

LIST_HEAD(fiber_list, fbr_fiber);

struct fbr_fiber {
	uint64_t id;
	const char *name;
	fbr_fiber_func_t func;
	void *func_arg;
	coro_context ctx;
	char *stack;
	size_t stack_size;
	struct fbr_call_info *call_list;
	size_t call_list_size;
	struct {
		struct fbr_ev_base **waiting;
		struct fbr_ev_base *arrived;
	} ev;
	struct trace_info reclaim_tinfo;
	struct fiber_list children;
	struct fbr_fiber *parent;
	struct mem_pool_list pool;
	struct {
		LIST_ENTRY(fbr_fiber) reclaimed;
		LIST_ENTRY(fbr_fiber) children;
	} entries;
};

struct fbr_mutex {
	fbr_id_t locked_by;
	struct fiber_id_tailq pending;
	TAILQ_ENTRY(fbr_mutex) entries;
};

TAILQ_HEAD(mutex_tailq, fbr_mutex);

struct fbr_cond_var {
	struct fbr_mutex *mutex;
	struct fiber_id_tailq waiting;
};

struct fbr_stack_item {
	struct fbr_fiber *fiber;
	struct trace_info tinfo;
};

struct fbr_context_private {
	struct fbr_stack_item stack[FBR_CALL_STACK_SIZE];
	struct fbr_stack_item *sp;
	struct fbr_fiber root;
	struct fiber_list reclaimed;
	struct ev_async pending_async;
	struct fiber_id_tailq pending_fibers;
	int backtraces_enabled;
	uint64_t last_id;

	struct ev_loop *loop;
};

#endif
