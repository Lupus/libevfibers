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

#ifndef _FBR_FIBER_H_
#define _FBR_FIBER_H_

#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <assert.h>
#include <ev.h>
#include <err.h>

#define FBR_CALL_STACK_SIZE 16
#define FBR_STACK_SIZE 64 * 1024 // 64 KB
#define FBR_MAX_ARG_NUM 10

#ifdef  NDEBUG
#define fbr_assert(expr)           ((void)(0))
#else
#define fbr_assert(context, expr)                                                                 \
	do {                                                                                      \
		__typeof__(expr) ex = (expr);                                                     \
		if(ex) (void)(0);                                                                 \
		else {                                                                            \
			fbr_dump_stack(context);                                                  \
			__assert_fail (__STRING(expr), __FILE__, __LINE__, __ASSERT_FUNCTION);    \
		}                                                                                 \
	} while(0);
#endif

struct fbr_context_private;
struct fbr_fiber;
struct fbr_mutex;

struct fbr_context
{
	struct fbr_context_private *__p;
};


#define FBR_P struct fbr_context *fctx
#define FBR_P_ FBR_P,
#define FBR_A fctx
#define FBR_A_ FBR_A,

typedef void (*fbr_fiber_func_t)(FBR_P);

struct fbr_fiber_arg;
typedef void (*fbr_arg_callback_t)(void *context, struct fbr_fiber_arg *arg);

struct fbr_fiber_arg {
	union {
		int i;
		void *v;
	};
	fbr_arg_callback_t cb;
};

struct fbr_call_info {
	int argc;
	struct fbr_fiber_arg argv[FBR_MAX_ARG_NUM];
	struct fbr_fiber *caller;
	struct fbr_call_info *next, *prev;
};

typedef void (*fbr_alloc_destructor_func)(void *ptr, void *context);

void fbr_init(FBR_P_ struct ev_loop *loop);
void fbr_destroy(FBR_P);
struct fbr_fiber * fbr_create(FBR_P_ const char *name, void (*func) (FBR_P),
		size_t stack_size);
void fbr_reclaim(FBR_P_ struct fbr_fiber *fiber);
int fbr_is_reclaimed(FBR_P_ struct fbr_fiber *fiber);
struct fbr_fiber_arg fbr_arg_i(int i);
struct fbr_fiber_arg fbr_arg_v(void *v);
struct fbr_fiber_arg fbr_arg_i_cb(int i, fbr_arg_callback_t cb);
struct fbr_fiber_arg fbr_arg_v_cb(void *v, fbr_arg_callback_t cb);
void fbr_subscribe(FBR_P_ int mid);
void fbr_unsubscribe(FBR_P_ int mid);
void fbr_unsubscribe_all(FBR_P);
void fbr_vcall(FBR_P_ struct fbr_fiber *callee, int argnum, va_list ap);
void fbr_vcall_context(FBR_P_ struct fbr_fiber *callee, void *context,
		int leave_info, int argnum, va_list ap);
void fbr_call(FBR_P_ struct fbr_fiber *fiber, int argnum, ...);
void fbr_call_context(FBR_P_ struct fbr_fiber *fiber, void *context,
		int leave_info, int argnum, ...);
void fbr_call_noinfo(FBR_P_ struct fbr_fiber *callee, int argnum, ...);
void fbr_multicall(FBR_P_ int mid, int argnum, ...);
void fbr_multicall_context(FBR_P_ int mid, void *context, int leave_info,
		int argnum, ...);
void fbr_yield(FBR_P);
void * fbr_alloc(FBR_P_ size_t size);
void fbr_alloc_set_destructor(FBR_P_ void *ptr, fbr_alloc_destructor_func func,
		void *context);
void * fbr_calloc(FBR_P_ unsigned int nmemb, size_t size);
void fbr_free(FBR_P_ void *ptr);
int fbr_next_call_info(FBR_P_ struct fbr_call_info **info_ptr);
ssize_t fbr_read(FBR_P_ int fd, void *buf, size_t count);
ssize_t fbr_read_all(FBR_P_ int fd, void *buf, size_t count);
ssize_t fbr_readline(FBR_P_ int fd, void *buffer, size_t n);
ssize_t fbr_write(FBR_P_ int fd, const void *buf, size_t count);
ssize_t fbr_write_all(FBR_P_ int fd, const void *buf, size_t count);
ssize_t fbr_recvfrom(FBR_P_ int sockfd, void *buf, size_t len, int flags, struct
		sockaddr *src_addr, socklen_t *addrlen);
ssize_t fbr_sendto(FBR_P_ int sockfd, const void *buf, size_t len, int flags, const
		struct sockaddr *dest_addr, socklen_t addrlen);
int fbr_accept(FBR_P_ int sockfd, struct sockaddr *addr, socklen_t *addrlen);
ev_tstamp fbr_sleep(FBR_P_ ev_tstamp seconds);
void fbr_dump_stack(FBR_P);
struct fbr_mutex * fbr_mutex_create(FBR_P);
void fbr_mutex_lock(FBR_P_ struct fbr_mutex * mutex);
int fbr_mutex_trylock(FBR_P_ struct fbr_mutex * mutex);
void fbr_mutex_unlock(FBR_P_ struct fbr_mutex * mutex);
void fbr_mutex_destroy(FBR_P_ struct fbr_mutex * mutex);

#endif
