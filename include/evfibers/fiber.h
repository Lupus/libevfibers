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
/**
 * @file evfibers/fiber.h
 * This file contains all client-visible API functions for working
 * with fibers.
 */
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <assert.h>
#include <ev.h>
#include <err.h>

/**
 * Maximum allowed level of fbr_call nesting within fibers.
 */
#define FBR_CALL_STACK_SIZE 16
/**
 * Default stack size for a fiber of 64 KB.
 */
#define FBR_STACK_SIZE 64 * 1024 // 64 KB
/**
 * Maximum allowed arguments that might be attached to fiber
 * invocation via fbr_call.
 */
#define FBR_MAX_ARG_NUM 10

/**
 * @def fbr_assert
 * Fiber version of classic assert.
 */
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

/**
 * Library context structure, should be initialized before any other
 * library callse will be performed.
 * @see fbr_init
 * @see fbr_destroy
 */
struct fbr_context
{
	struct fbr_context_private *__p; //!< pointer to internal context structure
};

/**
 * Utility macro for context parameter used in function prototypes.
 */
#define FBR_P struct fbr_context *fctx

/**
 * Same as FBR_P, but with comma afterwards for use in functions that
 * accept more that one parameter (which itself is the context pointer).
 */
#define FBR_P_ FBR_P,

/**
 * Utility macro for context parameter passing when calling fbr_*
 * functions.
 */
#define FBR_A fctx

/**
 * Same as FBR_A, but with comma afterwards for invocations of
 * functions that require more that one parameter (which itself is
 * the context pointer).
 */
#define FBR_A_ FBR_A,

/**
 * Fiber's ``main'' function type.
 * Fiber main function takes only one parameter --- the context. If
 * you need to pass more context information, you shall embed
 * fbr_context into any structure of your choice and calculate the
 * base pointer using container_of macro.
 * @see FBR_P
 * @see fbr_context
 */
typedef void (*fbr_fiber_func_t)(FBR_P);

struct fbr_fiber_arg;

/**
 * Callback function type, used for fiber call arguments processing.
 * Whenever you call some fiber with arguments, you need to ensure
 * that those arguments will survive if caller decides to free
 * resources associated with the arguments. This is especially useful
 * when multicall is used since you don't know how many fibers will
 * be actually called.
 * Actions in this callback depend solely on semantics of arguments
 * passed, i.e. one might want to do a newly allocated copy of some
 * object for each fiber called, or increase some reference counting.
 * @see fbr_fiber_arg
 * @see fbr_call
 * @see fbr_multicall
 */
typedef void (*fbr_arg_callback_t)(void *context, struct fbr_fiber_arg *arg);

/**
 * Actual argument of a fiber call.
 * It's implemented as a union between integer (i.e. enum or some
 * other constant) and pointer which covers a lot of use cases.
 * Additionally it might be attached a callback that will be invoked
 * upon passing of this argument to a concrete fiber during the call.
 * @see fbr_call
 * @see fbr_multicall
 */
struct fbr_fiber_arg {
	union {
		int i; //!< some integer value, enum for example
		void *v; //!< some pointer to an object
	};
	fbr_arg_callback_t cb; //!< optional callback
};

/**
 * Information about a call made to a fiber.
 * Whenever some fiber calls another fiber, such a structure is
 * allocated and appended to callee call queue.
 * @see fbr_next_call_info
 * @see fbr_call
 */
struct fbr_call_info {
	int argc; //!< number of arguments passed
	struct fbr_fiber_arg argv[FBR_MAX_ARG_NUM]; //!< actual array of arguments
	struct fbr_fiber *caller; //!< which fiber was the caller
	struct fbr_call_info *next, *prev;
};

/**
 * Destructor function type for the memory allocated in a fiber.
 * One can attache a destructor to a piece of memory allocated in a
 * fiber. It will be called whenever memory is freed with original
 * pointer allocated along with a user context pointer passed to it.
 * @see fbr_alloc
 * @see fbr_free
 */
typedef void (*fbr_alloc_destructor_func)(void *ptr, void *context);

/**
 * Initializes the library context.
 * @param fctx [in] pointer to the user allocated fbr_context.
 * @param loop [in] pointer to the user supplied libev loop.
 *
 * It's user's responsibility to allocate fbr_context structure and create and run the libev event loop.
 * @see fbr_context
 * @see fbr_destroy
 */
void fbr_init(FBR_P_ struct ev_loop *loop);

/**
 * Destroys the library context.
 * All created fibers are reclaimed and all of the memory is freed.
 * Stopping the event loop is user's responsibility.
 * @see fbr_context
 * @see fbr_init
 * @see fbr_reclaim
 */
void fbr_destroy(FBR_P);

/**
 * Creates a new fiber.
 * @param [in] name fiber name, used for identification it
 * backtraces, etc.
 * @param [in] func function used as a fiber's ``main''.
 * @param [in] stack_size stack size (0 for default).
 * @return Pointer to the created fiber.
 * 
 * The created fiber is not running in any shape or form, it's just
 * creted and is ready to be launched.
 *
 * The returned pointer may actually be the fiber that was recently
 * reclaimed, not the newly created one.
 *
 * Stack is anonymously mmaped so it should not occupy all the
 * required space straight away. Adjust stack size only when you know
 * what you are doing!
 *
 * Allocated stacks are registered as stacks via valgrind client
 * client request mechanism, so it's generally valgrind friendly and
 * should not cause any noise.
 *
 * Fibers are organized in a tree. Child nodes are attached to a parent
 * whenever the parent is creating them. This tree is used primarily for
 * automatic reclaim of child fibers.
 * @see fbr_reclaim
 */
struct fbr_fiber * fbr_create(FBR_P_ const char *name, void (*func) (FBR_P),
		size_t stack_size);

/**
 * Reclaims a fiber.
 * @param [in] fiber fiber pointer
 * 
 * Fibers are never destroyed, but reclaimed. Reclamaition frees some
 * resources like call lists and memory pools immediately while
 * keeping fiber structure itself and its stack as is. Reclaimed
 * fiber is prepended to the reclaimed fiber list and will be served
 * as a new one whenever next fbr_create is called. Fiber is
 * prepended because it is warm in terms of cpu cache and its use
 * might be faster than any other fiber in the list.
 *
 * When you have some reclaimed fibers in the list, reclaming and
 * creating are generally cheap operations.
 */
void fbr_reclaim(FBR_P_ struct fbr_fiber *fiber);

/**
 * Tests if given fiber is reclaimed.
 * @param [in] fiber fiber pointer
 * @return 1 if fiber is reclaimed, 0 otherwise
 */
int fbr_is_reclaimed(FBR_P_ struct fbr_fiber *fiber);

/**
 * Utility function for creating integer fbr_fiber_arg.
 * @param [in] i integer argument
 * @return integer fbr_fiber_arg struct.
 */
struct fbr_fiber_arg fbr_arg_i(int i);

/**
 * Utility function for creating void pointer fbr_fiber_arg.
 * @param [in] v void pointer argument
 * @return void pointer fbr_fiber_arg struct
 */
struct fbr_fiber_arg fbr_arg_v(void *v);

/**
 * Utility function for creating integer fbr_fiber_arg with callback.
 * @param [in] i integer argument
 * @return integer fbr_fiber_arg struct with callback
 */
struct fbr_fiber_arg fbr_arg_i_cb(int i, fbr_arg_callback_t cb);

/**
 * Utility function for creating void pointer fbr_fiber_arg with
 * callback.
 * @param [in] v void pointer argument
 * @return void pointer fbr_fiber_arg struct with callback
 */
struct fbr_fiber_arg fbr_arg_v_cb(void *v, fbr_arg_callback_t cb);

/**
 * Subscribes to a multicall group.
 * @param [in] mid multicall group id
 *
 * Multicall group is an arbitrary number which is chosen by the
 * user. This function joins current fiber to the specified multicall
 * group.
 * @see fbr_multicall
 */
void fbr_subscribe(FBR_P_ int mid);

/**
 * Drops membership in a multicall group.
 * @param [in] mid multicall group id
 *
 * This drops membership of current fiber in the specified multicall group.
 * @see fbr_multicall
 */
void fbr_unsubscribe(FBR_P_ int mid);

/**
 * Drops membership in all multicall group.
 * @param [in] mid multicall group id
 *
 * This drops membership of current fiber in all of the multicall
 * groups it was subscribed to.
 *
 * You don't need to explicitly call it before reclaiming of a fiber
 * since fbr_reclaim does that for you.
 * @see fbr_multicall
 */
void fbr_unsubscribe_all(FBR_P);

/**
 * Calls the specified fiber.
 * @param [in] callee fiber pointer to call
 * @param [in] argnum number of arguments to pass
 * @param [in] ap variadic argument list
 *
 * Behind the scenes this is a wrapper for fbr_vcall_context with
 * NULL user context passed.
 * @see fbr_vcall_context
 */
void fbr_vcall(FBR_P_ struct fbr_fiber *callee, int argnum, va_list ap);

/**
 * Actually calls the specified fiber.
 * @param [in] callee fiber pointer to call
 * @param [in] context user context pointer
 * @param [in] leave_info flag, indicating that fbr_call_info should
 * be queued
 * @param [in] argnum number of arguments to pass
 * @param [in] ap variadic argument list
 * @return function returns immediately if callee is busy or
 * eventually whenever callee yields
 *
 * This function adds fbr_call_info if desired to the callee call
 * list and transfers the control to callee execution context.
 *
 * Variadic arguments are supposed to be of type fbr_fiber_arg.
 *
 * User supplied context pointer will be passed to any non-NULL
 * fbr_arg_callback_t callback.
 *
 * If callee is reclaimed --- runtime error is generated.
 * @see fbr_yield
 */
void fbr_vcall_context(FBR_P_ struct fbr_fiber *callee, void *context,
		int leave_info, int argnum, va_list ap);

/**
 * Calls the specified fiber.
 * @param [in] callee fiber pointer to call
 * @param [in] argnum number of arguments to pass
 *
 * Behind the scenes this is a wrapper for fbr_call_vcontext with
 * NULL user context passed and leave_info of 1.
 * @see fbr_vcall_context
 */
void fbr_call(FBR_P_ struct fbr_fiber *fiber, int argnum, ...);

/**
 * Calls the specified fiber.
 * @param [in] callee fiber pointer to call
 * @param [in] context user context pointer
 * @param [in] argnum number of arguments to pass
 *
 * Behind the scenes this is a wrapper for fbr_call_vcontext with
 * leave_info of 1 passed.
 * @see fbr_vcall_context
 */
void fbr_call_context(FBR_P_ struct fbr_fiber *fiber, void *context,
		int leave_info, int argnum, ...);

/**
 * Calls the specified fiber.
 * @param [in] callee fiber pointer to call
 * @param [in] argnum number of arguments to pass
 *
 * Behind the scenes this is a wrapper for fbr_call_vcontext with
 * NULL user context passed and leave_info of 0.
 * @see fbr_vcall_context
 */
void fbr_call_noinfo(FBR_P_ struct fbr_fiber *callee, int argnum, ...);

/**
 * Calls the specified fiber group.
 * @param [in] mid multicall group id to call
 * @param [in] argnum number of arguments to pass
 *
 * This is a loop wrapper around fbr_call_vcontext with
 * NULL user context passed and leave_info of 1.
 *
 * It loops through all fibers subscribed to specified multicast
 * group id.
 * @see fbr_vcall_context
 */
void fbr_multicall(FBR_P_ int mid, int argnum, ...);

/**
 * Calls the specified fiber group.
 * @param [in] mid multicall group id to call
 * @param [in] context user context pointer
 * @param [in] argnum number of arguments to pass
 *
 * This is a loop wrapper around fbr_call_vcontext with leave_info of
 * 1 passed.
 *
 * It loops through all fibers subscribed to specified multicast
 * group id.
 * @see fbr_vcall_context
 */
void fbr_multicall_context(FBR_P_ int mid, void *context, int leave_info,
		int argnum, ...);

/**
 * Yields execution to other fiber.
 * @param [in] mid multicall group id to call
 * @param [in] context user context pointer
 * @param [in] argnum number of arguments to pass
 *
 * When a fiber is waiting for some incoming event --- it should
 * yield. This will pop current fiber from the fiber stack and
 * transfer the execution context to the next fiber from the stack
 * making that fiber a new current one.
 *
 * It loops through all fibers subscribed to specified multicast
 * group id.
 * @see fbr_call
 */
void fbr_yield(FBR_P);

/**
 * Allocates memory in current fiber's pool.
 * @param [in] size size of the requested memory block
 * @return allocated memory chunk
 *
 * When a fiber is reclaimed, this memory will be freed. Prior to that a destructor will be called if any specified.
 * @see fbr_calloc
 * @see fbr_alloc_set_destructor
 * @see fbr_alloc_destructor_func
 * @see fbr_free
 */
void * fbr_alloc(FBR_P_ size_t size);

/**
 * Sets destructor for a memory chunk.
 * @param [in] ptr address of a memory chunk
 * @param [in] func destructor function
 * @param [in] context user supplied context pointer
 *
 * Setting new destructor simply changes it without calling old one or queueing them.
 *
 * You can allocate 0 sized memory chunk and never free it just for
 * the purpose of calling destructor with some context when fiber is
 * reclaimed. This way you can for example close some file descritors
 * or do some other required cleanup.
 * @see fbr_alloc
 * @see fbr_free
 */
void fbr_alloc_set_destructor(FBR_P_ void *ptr, fbr_alloc_destructor_func func,
		void *context);

/**
 * Allocates a set of initalized objects in fiber's pool.
 * @param [in] nmemb number of members
 * @param [in] size size of a single member
 * @return zero-filled allocated memory chunk
 *
 * Same as fbr_alloc called with nmemb multiplied by size.
 * @see fbr_alloc
 * @see fbr_free
 */
void * fbr_calloc(FBR_P_ unsigned int nmemb, size_t size);

/**
 * Explicitly frees allocated memory chunk.
 * @param [in] ptr chunk address
 *
 * Explicitly frees a fiber pool chunk calling the destructor if any.
 * @see fbr_alloc
 * @see fbr_calloc
 * @see fbr_alloc_set_destructor
 */
void fbr_free(FBR_P_ void *ptr);

/**
 * Fetches next call info.
 * @param [in,out] info_ptr pointer to info pointer
 * @return 1 if more are pending, 0 otherwise
 *
 * Should be used in a loop until returns 0. Afterwards a fiber
 * probably needs to yield and to expect more call infos available
 * after fbr_yield returns.
 *
 * Function writes new info pointer into specified location. If that
 * location contains an address of previous info --- it will be
 * freed and that's probably the behavoir you want. Just ensure that
 * you set your pointer to NULL before passing it to this function
 * first time.
 *
 * Also bear in mind that first invocation of a fiber that might be
 * considered the starting (or intializing) one is still queued into
 * a call list and you need to fetch if you want to fetch anything
 * else. If you are not interested in call info --- just pass NULL as
 * location (i.e. info_ptr). Next call info will just be freed in
 * this case.
 * @see fbr_call
 * @see fbr_call_info
 */
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
