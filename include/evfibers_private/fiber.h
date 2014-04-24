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

#ifndef _FBR_FIBER_PRIVATE_H_
#define _FBR_FIBER_PRIVATE_H_

#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/queue.h>
#include <evfibers/fiber.h>
#include <evfibers_private/trace.h>
#include <coro.h>
#define max(a,b) ({						\
		const typeof(a) __tmp_a = (a);			\
		const typeof(b) __tmp_b = (b);			\
		__tmp_a > __tmp_b ? __tmp_a : __tmp_b;		\
		})

#define min(a,b) ({						\
		const typeof(a) __tmp_a = (a);			\
		const typeof(b) __tmp_b = (b);			\
		__tmp_a < __tmp_b ? __tmp_a : __tmp_b;		\
		})

#define _unused_ __attribute__((unused))

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


struct mem_pool {
	void *ptr;
	fbr_alloc_destructor_func_t destructor;
	void *destructor_context;
	LIST_ENTRY(mem_pool) entries;
};

LIST_HEAD(mem_pool_list, mem_pool);

TAILQ_HEAD(fiber_destructor_tailq, fbr_destructor);
LIST_HEAD(fiber_list, fbr_fiber);

struct fbr_fiber {
	uint64_t id;
	char name[FBR_MAX_FIBER_NAME];
	fbr_fiber_func_t func;
	void *func_arg;
	coro_context ctx;
	char *stack;
	size_t stack_size;
	struct {
		struct fbr_ev_base **waiting;
		int arrived;
	} ev;
	struct trace_info reclaim_tinfo;
	struct fiber_list children;
	struct fbr_fiber *parent;
	struct mem_pool_list pool;
	struct {
		LIST_ENTRY(fbr_fiber) reclaimed;
		LIST_ENTRY(fbr_fiber) children;
	} entries;
	struct fiber_destructor_tailq destructors;
	void *user_data;
	void *key_data[FBR_MAX_KEY];
	int no_reclaim;
	int want_reclaim;
	struct fbr_cond_var reclaim_cond;
};

TAILQ_HEAD(mutex_tailq, fbr_mutex);

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
	struct fbr_id_tailq pending_fibers;
	int backtraces_enabled;
	uint64_t last_id;
	uint64_t key_free_mask;
	const char *buffer_file_pattern;

	struct ev_loop *loop;
};

#endif
