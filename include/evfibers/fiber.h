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

/** \mainpage About libevfibers
 *
 * \section intro_sec Introduction
 *
 * libevfibers is a small C fiber library that uses libev based event loop and
 * libcoro based coroutine context switching. As libcoro alone is barely enough
 * to do something useful, this project aims at building a complete fiber API
 * around it while leveraging libev's high performance and flexibility.
 *
 * You may ask why yet another fiber library, there are GNU Pth, State threads,
 * etc. When I was looking at their API, I found it being too restrictive: you
 * cannot use other event loop. For GNU Pth it's solely select based
 * implementation, as for state threads --- they provide several
 * implementations including poll, epoll, select though event loop is hidden
 * underneath the public API and is not usable directly. I found another
 * approach more sensible, namely: just put fiber layer on top of well-known
 * and robust event loop implementation. Marc Lehmann already provided all the
 * necessary to do the job: event loop library libev with coroutine library
 * libcoro.
 *
 * So what's so cool about fibers? Fibers are user-space threads. User-space
 * means that context switching from one fiber to an other fiber takes no
 * effort from the kernel. There are different ways to achieve this, but it's
 * not relevant here since libcoro already does all the dirty job. At top level
 * you have a set of functions that execute on private stacks that do not
 * intersect. Whenever such function is going to do some blocking operation,
 * i.e. socket read, it calls fiber library wrapper, that asks event loop to
 * transfer execution to this function whenever some data arrives, then it
 * yields execution to other fiber. From the function's point of view it runs
 * in exclusive mode and blocks on all operations, but really other such
 * functions execute while this one is waiting. Typically most of them are
 * waiting for something and event loop dispatches the events.
 *
 * This approach helps a lot. Imagine that you have some function that requires
 * 3 events. In classic asynchronous model you will have to arrange your
 * function in 3 callbacks and register them in the event loop. On the other
 * hand having one function waiting for 3 events in ``blocking'' fashion is
 * both more readable and maintainable.
 *
 * Then why use event loop when you have fancy callback-less fiber wrappers?
 * Sometimes you just need a function that will set a flag in some object when
 * a timer times out. Creating a fiber solely for this simple task is a bit
 * awkward.
 *
 * libevfibers allows you to use fiber style wrappers for blocking operations
 * as well as fall back to usual event loop style programming when you need it.
 *
 * \section install_sec Installation
 *
 * \subsection requirements_ssec Requirements
 *
 * To build this documentation properly you need to have
 * [doxygen](http://www.stack.nl/~dimitri/doxygen) version >= 1.8 since it used
 * markdown.
 *
 * To build libevfibers you need the following packages:
 * - [cmake](http://www.cmake.org)
 *
 *   CMake is a build system used to assemble this project.
 * - [libev](http://software.schmorp.de/pkg/libev.html) development files
 *
 *   Well-known and robust event loop.
 * - [uthash](http://uthash.sourceforge.net) development files
 *
 *   Macro-based hash library that was used in earlier versions. Currently no
 *   hashes are used, but I still have utlist used somewhere. Will get rid of
 *   this dependency soon in favor of <sys/queue.h>.
 *
 * - [valgrind](http://valgrind.org) development files
 *
 *   libevfibers makes use of client requests in valgrind to register stacks.
 * - [Check](http://check.sourceforge.net) unit testing framework
 *
 *   Strictly it's not a requirement, but you better run unit tests before
 *   installation.
 *
 * You don't need libcoro installed as it's part of source tree and will build
 * along with libevfibers.
 *
 * As far as runtime dependencies concerned, the following is required:
 *  - [libev](http://software.schmorp.de/pkg/libev.html) runtime files
 *
 * For debian-based distributions users (i.e. Ubuntu) you can use the following
 * command to install all the dependencies:
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~{.sh}
 * sudo apt-get install cmake libev-dev uthash-dev valgrind check
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * \subsection building_ssec Building
 *
 * Once you have all required packages installed you may proceed with building.
 * Roughly it's done as follows:
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~{.sh}
 * git clone https://code.google.com/p/libevfibers
 * cd libevfibers/
 * mkdir build
 * cd build/
 * cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
 * make
 * sudo make install
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * \subsection building_deb_ssec Building debian package
 * If you are running debian-based distribution, it will be more useful to
 * build a debian package and install it.
 *
 * The following actions will bring you there:
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~{.sh}
 * git clone https://code.google.com/p/libevfibers
 * cd libevfibers/
 * dpkg-buildpackage
 * sudo dpkg -i ../libevfibers?_*_*.deb ../libevfibers-dev_*_*.deb
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * \section contributors_sec Contributors
 * libevfibers was written and designed by Konstantin Olkhovskiy.
 *
 * Sergey Myasnikov contributed some patches, a lot of criticism and ideas.
 */

#ifndef _FBR_FIBER_H_
#define _FBR_FIBER_H_
/**
 * @file evfibers/fiber.h
 * This file contains all client-visible API functions for working with fibers.
 */
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <assert.h>
#include <ev.h>

/**
 * Maximum allowed level of fbr_call nesting within fibers.
 */
#define FBR_CALL_STACK_SIZE 16
/**
 * Default stack size for a fiber of 64 KB.
 */
#define FBR_STACK_SIZE (64 * 1024) /* 64 KB */
/**
 * Maximum allowed arguments that might be attached to fiber
 * invocation via fbr_call.
 */
#define FBR_MAX_ARG_NUM 10

/**
 * @def fbr_assert
 * Fiber version of classic assert.
 */
#ifdef NDEBUG
#define fbr_assert(context, expr)           ((void)(0))
#else
#define fbr_assert(context, expr)                                                             \
	do {                                                                                  \
		__typeof__(expr) ex = (expr);                                                 \
		if (ex)                                                                       \
			(void)(0);                                                            \
		else {                                                                        \
			fbr_dump_stack(context, fbr_log_e);                                   \
			__assert_fail(__STRING(expr), __FILE__, __LINE__, __ASSERT_FUNCTION); \
		}                                                                             \
	} while (0)
#endif

#define fbr_container_of(ptr, type, member) ({                       \
		const typeof( ((type *)0)->member ) *__mptr = (ptr); \
		(type *)( (char *)__mptr - offsetof(type,member) );  \
		})

struct fbr_context_private;
struct fbr_mutex;
struct fbr_logger;
struct fbr_cond_var;
typedef __uint128_t fbr_id_t;

/**
 * Error codes used within the library.
 *
 * These constants are returned via f_errno member of fbr_context struct.
 * @see fbr_context
 * @see fbr_strerror
 */
enum fbr_error_code {
	FBR_SUCCESS = 0,
	FBR_EINVAL,
	FBR_ENOFIBER,
	FBR_ESYSTEM,
};

/**
 * Library context structure, should be initialized before any other library
 * calls will be performed.
 * @see fbr_init
 * @see fbr_destroy
 * @see fbr_strerror
 */
struct fbr_context {
	struct fbr_context_private *__p; /*!< pointer to internal context
					   structure */
	enum fbr_error_code f_errno; /*!< context wide error code */
	struct fbr_logger *logger; /*!< current logger */
};

/**
 * Utility macro for context parameter used in function prototypes.
 */
#define FBR_P struct fbr_context *fctx

/**
 * Same as FBR_P, but with comma afterwards for use in functions that accept
 * more that one parameter (which itself is the context pointer).
 */
#define FBR_P_ FBR_P,

/**
 * Utility macro for context parameter passing when calling fbr_* functions.
 */
#define FBR_A fctx

/**
 * Same as FBR_A, but with comma afterwards for invocations of functions that
 * require more that one parameter (which itself is the context pointer).
 */
#define FBR_A_ FBR_A,

/**
 * Fiber's ``main'' function type.
 * Fiber main function takes only one parameter --- the context. If you need to
 * pass more context information, you shall embed fbr_context into any
 * structure of your choice and calculate the base pointer using container_of
 * macro.
 * @see FBR_P
 * @see fbr_context
 */
typedef void (*fbr_fiber_func_t)(FBR_P_ void *_arg);

/**
 * Actual argument of a fiber call.
 * It's implemented as a union between integer (i.e. enum or some other
 * constant) and pointer which covers a lot of use cases.
 * @see fbr_call
 */
struct fbr_fiber_arg {
	union {
		long i; /*!< some integer value, enum for example */
		void *v; /*!< some pointer to an object */
	};
};

/**
 * Information about a call made to a fiber.
 *
 * Whenever some fiber calls another fiber, such a structure is allocated and
 * appended to callee call queue.
 * @see fbr_next_call_info
 * @see fbr_call
 */
struct fbr_call_info {
	int argc; /*!< number of arguments passed */
	struct fbr_fiber_arg argv[FBR_MAX_ARG_NUM]; /*!< actual array of
						      arguments */
	fbr_id_t caller; /*!< which fiber was the caller */
	struct fbr_call_info *next, *prev;
};

/**
 * Destructor function type for the memory allocated in a fiber.
 * @param [in] ptr memory pointer for memory to be destroyed
 * @param [in] context user data pointer passed via fbr_alloc_set_destructor
 *
 * One can attach a destructor to a piece of memory allocated in a fiber. It
 * will be called whenever memory is freed with original pointer allocated
 * along with a user context pointer passed to it.
 * @see fbr_alloc
 * @see fbr_free
 * @see fbr_alloc_set_destructor
 */
typedef void (*fbr_alloc_destructor_func_t)(FBR_P_ void *ptr, void *context);

/**
 * Logging levels.
 * @see fbr_logger
 * @see fbr_context
 */
enum fbr_log_level {
	FBR_LOG_ERROR = 0,
	FBR_LOG_WARNING,
	FBR_LOG_NOTICE,
	FBR_LOG_INFO,
	FBR_LOG_DEBUG
};

struct fbr_logger;

/**
 * Logger function type.
 * @param [in] logger currently configured logger
 * @param [in] level log level of message
 * @param [in] format printf-compatible format string
 * @param [in] ap variadic argument list
 * This function should log the message if log level suits the one configured
 * in a non-blocking manner (i.e. it should not synchronously write it to
 * disk).
 * @see fbr_logger
 * @see fbr_log_func_t
 */
typedef void (*fbr_log_func_t)(struct fbr_logger *logger,
		enum fbr_log_level level, const char *format, va_list ap);
/**
 * Logger utility function type.
 * @param [in] format printf-compatible format string
 *
 * This function wraps logger function invocation.
 * @see fbr_logger
 * @see fbr_log_func_t
 */
typedef void (*fbr_logutil_func_t)(FBR_P_ const char *format, ...);

/**
 * Logger structure.
 * @see fbr_logger
 * @see fbr_context
 */
struct fbr_logger {
	fbr_log_func_t logv; /*!< Function pointer that represents the logger */
	enum fbr_log_level level; /*!< Current log level */
	void *data; /*!< User data pointer */
};

enum fbr_ev_type {
	FBR_EV_WATCHER = 1,
	FBR_EV_MESSAGE,
	FBR_EV_MUTEX,
	FBR_EV_COND,
};

struct fbr_ev_base {
	enum fbr_ev_type type;
	int user_type;
	fbr_id_t id;
	struct fbr_context *fctx;
};

#define fbr_ev_cast(ptr, type_no_struct) \
       fbr_container_of(ptr, struct type_no_struct, ev_base)

struct fbr_ev_watcher {
	ev_watcher *w;
	struct fbr_ev_base ev_base;
};

struct fbr_ev_mutex {
	struct fbr_mutex *mutex;
	struct fbr_ev_base ev_base;
};


void fbr_ev_watcher_init(FBR_P_ struct fbr_ev_watcher *ev, ev_watcher *w);
void fbr_ev_mutex_init(FBR_P_ struct fbr_ev_mutex *ev,
		struct fbr_mutex *mutex);
struct fbr_ev_base *fbr_ev_wait(FBR_P_ struct fbr_ev_base *events[]);
void fbr_ev_wait_one(FBR_P_ struct fbr_ev_base *one);
int fbr_transfer(FBR_P_ fbr_id_t to);


/**
 * Initializes the library context.
 * @param [in] fctx pointer to the user allocated fbr_context.
 * @param [in] loop pointer to the user supplied libev loop.
 *
 * It's user's responsibility to allocate fbr_context structure and create and
 * run the libev event loop.
 * @see fbr_context
 * @see fbr_destroy
 */
void fbr_init(struct fbr_context *fctx, struct ev_loop *loop);

/**
 * Destroys the library context.
 * All created fibers are reclaimed and all of the memory is freed.  Stopping
 * the event loop is user's responsibility.
 * @see fbr_context
 * @see fbr_init
 * @see fbr_reclaim
 */
void fbr_destroy(FBR_P);

/**
 * Enables/Disables backtrace capturing.
 * @param [in] enabled are backtraces enabled?
 *
 * The library tries to capture backtraces at certain points which may help
 * when debugging obscure problems. For example it captures the backtrace
 * whenever a fiber is reclaimed and when one tries to call it dumps out the
 * backtrace showing where was it reclaimed. But these cost quite a bit of cpu
 * and are disabled by default.
 */
void fbr_enable_backtraces(FBR_P, int enabled);

/**
 * Analog of strerror but for the library errno.
 * @param [in] code Error code to describe
 * @see fbr_context
 * @see fbr_error_code
 */
const char *fbr_strerror(FBR_P_ enum fbr_error_code code);

/**
 * Utility log wrapper.
 *
 * Wraps logv function of type fbr_log_func_t located in fbr_logger with log
 * level of FBR_LOG_ERROR. Follows printf semantics of format string and
 * variadic argument list.
 * @see fbr_context
 * @see fbr_logger
 * @see fbr_log_func_t
 * @see fbr_logutil_func_t
 */
void fbr_log_e(FBR_P_ const char *format, ...)
	__attribute__ ((format (printf, 2, 3)));

/**
 * Utility log wrapper.
 *
 * Wraps logv function of type fbr_log_func_t located in fbr_logger with log
 * level of FBR_LOG_WARNING. Follows printf semantics of format string and
 * variadic argument list.
 * @see fbr_context
 * @see fbr_logger
 * @see fbr_log_func_t
 * @see fbr_logutil_func_t
 */
void fbr_log_w(FBR_P_ const char *format, ...)
	__attribute__ ((format (printf, 2, 3)));

/**
 * Utility log wrapper.
 *
 * Wraps logv function of type fbr_log_func_t located in fbr_logger with log
 * level of FBR_LOG_NOTICE. Follows printf semantics of format string and
 * variadic argument list.
 * @see fbr_context
 * @see fbr_logger
 * @see fbr_log_func_t
 * @see fbr_logutil_func_t
 */
void fbr_log_n(FBR_P_ const char *format, ...)
	__attribute__ ((format (printf, 2, 3)));

/**
 * Utility log wrapper.
 *
 * Wraps logv function of type fbr_log_func_t located in fbr_logger with log
 * level of FBR_LOG_INFO. Follows printf semantics of format string and
 * variadic argument list.
 * @see fbr_context
 * @see fbr_logger
 * @see fbr_log_func_t
 * @see fbr_logutil_func_t
 */
void fbr_log_i(FBR_P_ const char *format, ...)
	__attribute__ ((format (printf, 2, 3)));

/**
 * Utility log wrapper.
 *
 * Wraps logv function of type fbr_log_func_t located in fbr_logger with log
 * level of FBR_LOG_DEBUG. Follows printf semantics of format string and
 * variadic argument list.
 * @see fbr_context
 * @see fbr_logger
 * @see fbr_log_func_t
 * @see fbr_logutil_func_t
 */
void fbr_log_d(FBR_P_ const char *format, ...)
	__attribute__ ((format (printf, 2, 3)));

/**
 * Creates a new fiber.
 * @param [in] name fiber name, used for identification it
 * backtraces, etc.
 * @param [in] func function used as a fiber's ``main''.
 * @param [in] stack_size stack size (0 for default).
 * @return Pointer to the created fiber.
 *
 * The created fiber is not running in any shape or form, it's just created and
 * is ready to be launched.
 *
 * Stack is anonymously mmaped so it should not occupy all the required space
 * straight away. Adjust stack size only when you know what you are doing!
 *
 * Allocated stacks are registered as stacks via valgrind client request
 * mechanism, so it's generally valgrind friendly and should not cause any
 * noise.
 *
 * Fibers are organized in a tree. Child nodes are attached to a parent
 * whenever the parent is creating them. This tree is used primarily for
 * automatic reclaim of child fibers.
 * @see fbr_reclaim
 * @see fbr_disown
 * @see fbr_parent
 */
fbr_id_t fbr_create(FBR_P_ const char *name, fbr_fiber_func_t func, void *arg,
		size_t stack_size);


/**
 * Changes parent of current fiber.
 * @param [in] parent new parent fiber
 * @returns -1 on error with f_errno set, 0 upon success
 *
 * This function allows you to change fiber's parent. You needs to pass valid
 * id or 0 to indicate the root fiber.
 *
 * This might be useful when some fiber A creates another fiber B that should
 * survive it's parent being reclaimed, or vice versa, some fiber A needs to be
 * reclaimed with fiber B albeit B is not A's parent.
 *
 * Root fiber is reclaimed only when library context is destroyed.
 * @see fbr_create
 * @see fbr_destroy
 */
int fbr_disown(FBR_P_ fbr_id_t parent);

/**
 * Find out current fiber's parent.
 * @returns current fiber's parent
 *
 * This function allows you to find out what fiber is considered to be parent
 * for the current one.
 * @see fbr_create
 * @see fbr_disown
 */
fbr_id_t fbr_parent(FBR_P);

/**
 * Reclaims a fiber.
 * @param [in] fiber fiber pointer
 * @returns -1 on error with f_errno set, 0 upon success
 *
 * Fibers are never destroyed, but reclaimed. Reclamation frees some resources
 * like call lists and memory pools immediately while keeping fiber structure
 * itself and its stack as is. Reclaimed fiber is prepended to the reclaimed
 * fiber list and will be served as a new one whenever next fbr_create is
 * called. Fiber is prepended because it is warm in terms of cpu cache and its
 * use might be faster than any other fiber in the list.
 *
 * When you have some reclaimed fibers in the list, reclaiming and creating are
 * generally cheap operations.
 */
int fbr_reclaim(FBR_P_ fbr_id_t fiber);

/**
 * Tests if given fiber is reclaimed.
 * @param [in] fiber fiber pointer
 * @return 1 if fiber is reclaimed, 0 otherwise
 */
int fbr_is_reclaimed(FBR_P_ fbr_id_t fiber);

/**
 * Returns id of current fiber.
 * @return fbr_id_t of current fiber being executed.
 */
fbr_id_t fbr_self(FBR_P);

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
 * Actually calls the specified fiber.
 * @param [in] callee fiber pointer to call
 * @param [in] leave_info flag, indicating that fbr_call_info should
 * be queued
 * @param [in] argnum number of arguments to pass
 * @param [in] ap variadic argument list
 * @return 0 on success, -1 on failure. Function returns immediately if callee
 * is busy or eventually whenever callee yields
 *
 * This function adds fbr_call_info if desired to the callee call list and
 * transfers the control to callee execution context.
 *
 * Variadic arguments are supposed to be of type fbr_fiber_arg.
 *
 * If callee is reclaimed --- runtime error is generated.
 * @see fbr_yield
 * @see fbr_strerror
 */
int fbr_vcall(FBR_P_ fbr_id_t callee, int leave_info, int argnum, va_list ap)
	__attribute__ ((warn_unused_result));

/**
 * Calls the specified fiber.
 * @param [in] fiber fiber pointer to call
 * @param [in] argnum number of arguments to pass
 * @return 0 on success, -1 on failure
 *
 * Behind the scenes this is a wrapper for fbr_vcall with leave_info of 1.
 * @see fbr_vcall
 */
int fbr_call(FBR_P_ fbr_id_t fiber, int argnum, ...)
	__attribute__ ((warn_unused_result));

/**
 * Yields execution to other fiber.
 *
 * When a fiber is waiting for some incoming event --- it should yield. This
 * will pop current fiber from the fiber stack and transfer the execution
 * context to the next fiber from the stack making that fiber a new current
 * one.
 *
 * It loops through all fibers subscribed to specified multicast group id.
 * @see fbr_call
 */
void fbr_yield(FBR_P);

/**
 * Allocates memory in current fiber's pool.
 * @param [in] size size of the requested memory block
 * @return allocated memory chunk
 *
 * When a fiber is reclaimed, this memory will be freed. Prior to that a
 * destructor will be called if any specified.
 * @see fbr_calloc
 * @see fbr_alloc_set_destructor
 * @see fbr_alloc_destructor_func_t
 * @see fbr_free
 */
void *fbr_alloc(FBR_P_ size_t size);

/**
 * Sets destructor for a memory chunk.
 * @param [in] ptr address of a memory chunk
 * @param [in] func destructor function
 * @param [in] context user supplied context pointer
 *
 * Setting new destructor simply changes it without calling old one or queueing
 * them.
 *
 * You can allocate 0 sized memory chunk and never free it just for the purpose
 * of calling destructor with some context when fiber is reclaimed. This way
 * you can for example close some file descriptors or do some other required
 * cleanup.
 * @see fbr_alloc
 * @see fbr_free
 */
void fbr_alloc_set_destructor(FBR_P_ void *ptr, fbr_alloc_destructor_func_t func,
		void *context);

/**
 * Allocates a set of initialized objects in fiber's pool.
 * @param [in] nmemb number of members
 * @param [in] size size of a single member
 * @return zero-filled allocated memory chunk
 *
 * Same as fbr_alloc called with nmemb multiplied by size.
 * @see fbr_alloc
 * @see fbr_free
 */
void *fbr_calloc(FBR_P_ unsigned int nmemb, size_t size);

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
 * Explicitly frees allocated memory chunk.
 * @param [in] ptr chunk address
 *
 * Explicitly frees a fiber pool chunk without calling the destructor.
 * @see fbr_alloc
 * @see fbr_calloc
 * @see fbr_alloc_set_destructor
 */
void fbr_free_nd(FBR_P_ void *ptr);

/**
 * Fetches next call info.
 * @param [in,out] info_ptr pointer to info pointer
 * @return 1 if more are pending, 0 otherwise
 *
 * Should be used in a loop until returns 0. Afterwards a fiber probably needs
 * to yield and to expect more call infos available after fbr_yield returns.
 *
 * Function writes new info pointer into specified location. If that location
 * contains an address of previous info --- it will be freed and that's
 * probably the behavior you want. Just ensure that you set your pointer to
 * NULL before passing it to this function first time.
 *
 * Also bear in mind that the first invocation of a fiber that might be
 * considered the starting (or initializing) one is still queued into a call
 * list and you need to fetch if you want to fetch anything else. If you are
 * not interested in call info --- just pass NULL as location (i.e. info_ptr).
 * Next call info will just be freed in this case.
 * @see fbr_call
 * @see fbr_call_info
 */
int fbr_next_call_info(FBR_P_ struct fbr_call_info **info_ptr);

/**
 * Utility function to make file descriptor non-blocking.
 * @param [in] fd file descriptor to make non-blocking
 * @returns -1 on error with f_errno set, 0 upon success
 *
 * In case of failure FBR_ESYSTEM is set as f_errno ans user should consult
 * system errno for details.
 *
 */
int fbr_fd_nonblock(FBR_P_ int fd);

/**
 * Fiber friendly libc read wrapper.
 * @param [in] fd file descriptor to read from
 * @param [in] buf pointer to some user-allocated buffer
 * @param [in] count maximum number of bytes to read
 * @return number of bytes read on success, -1 in case of error and errno set
 *
 * Attempts to read up to count bytes from file descriptor fd into the buffer
 * starting at buf. Calling fiber will be blocked until something arrives at
 * fd.
 *
 * Possible errno values are described in read man page. The only special case
 * is EINTR which is handled internally and is returned to the caller only in
 * case when non-root fiber called the fiber waiting in fbr_read.
 *
 * @see fbr_read_all
 * @see fbr_read_line
 */
ssize_t fbr_read(FBR_P_ int fd, void *buf, size_t count);

/**
 * Even more fiber friendly libc read wrapper.
 * @param [in] fd file descriptor to read from
 * @param [in] buf pointer to some user-allocated buffer
 * @param [in] count desired number of bytes to read
 * @return number of bytes read on success, -1 in case of error and errno set
 *
 * Attempts to read exactly count bytes from file descriptor fd into the buffer
 * starting at buf. Calling fiber will be blocked until the required amount of
 * data or EOF arrive at fd. If latter occurs too early returned number of
 * bytes will be less that required.
 *
 * Possible errno values are described in read man page. Unlike fbr_read this
 * function will never return -1 with EINTR and will silently ignore any
 * attempts to call this fiber from other non-root fibers (call infos are still
 * queued if the called desired to do so).
 *
 * @see fbr_read
 * @see fbr_read_line
 */
ssize_t fbr_read_all(FBR_P_ int fd, void *buf, size_t count);

/**
 * Utility function to read a line.
 * @param [in] fd file descriptor to read from
 * @param [in] buffer pointer to some user-allocated buffer
 * @param [in] n maximum number of bytes to read
 * @return number of bytes read on success, -1 in case of error and errno set
 *
 * Attempts to read at most count bytes from file descriptor fd into the buffer
 * starting at buf, but stops if newline is encountered. Calling fiber will be
 * blocked until the required amount of data, EOF or newline arrive at fd.
 *
 * Possible errno values are described in read man page. As with fbr_read_all this
 * function will never return -1 with EINTR.
 *
 * @see fbr_read
 * @see fbr_read_all
 */
ssize_t fbr_readline(FBR_P_ int fd, void *buffer, size_t n);

/**
 * Fiber friendly libc write wrapper.
 * @param [in] fd file descriptor to write to
 * @param [in] buf pointer to some user-allocated buffer
 * @param [in] count maximum number of bytes to write
 * @return number of bytes written on success, -1 in case of error and errno set
 *
 * Attempts to write up to count bytes to file descriptor fd from the buffer
 * starting at buf. Calling fiber will be blocked until the data is written.
 *
 * Possible errno values are described in write man page. The only special case
 * is EINTR which is handled internally and is returned to the caller only in
 * case when non-root fiber called the fiber sitting in fbr_write.
 *
 * @see fbr_write_all
 */
ssize_t fbr_write(FBR_P_ int fd, const void *buf, size_t count);

/**
 * Even more fiber friendly libc write wrapper.
 * @param [in] fd file descriptor to write to
 * @param [in] buf pointer to some user-allocated buffer
 * @param [in] count desired number of bytes to write
 * @return number of bytes read on success, -1 in case of error and errno set
 *
 * Attempts to write exactly count bytes to file descriptor fd from the buffer
 * starting at buf. Calling fiber will be blocked until the required amount of
 * data is written to fd.
 *
 * Possible errno values are described in write man page. Unlike fbr_write this
 * function will never return -1 with EINTR and will silently ignore any
 * attempts to call this fiber from other non-root fibers (call infos are still
 * queued if the called desired to do so).
 *
 * @see fbr_write
 */
ssize_t fbr_write_all(FBR_P_ int fd, const void *buf, size_t count);

/**
 * Fiber friendly libc recvfrom wrapper.
 * @param [in] sockfd file descriptor to read from
 * @param [in] buf pointer to some user-allocated buffer
 * @param [in] len maximum number of bytes to read
 * @param [in] flags just flags, see man recvfrom for details
 * @param [in] src_addr source address
 * @param [in] addrlen size of src_addr
 * @return number of bytes read on success, -1 in case of error and errno set
 *
 * This function is used to receive messages from a socket.
 *
 * Possible errno values are described in recvfrom man page. The only special case
 * is EINTR which is handled internally and is returned to the caller only in
 * case when non-root fiber called the fiber sitting in fbr_recvfrom.
 *
 */
ssize_t fbr_recvfrom(FBR_P_ int sockfd, void *buf, size_t len, int flags,
		struct sockaddr *src_addr, socklen_t *addrlen);

/**
 * Fiber friendly libc sendto wrapper.
 * @param [in] sockfd file descriptor to write to
 * @param [in] buf pointer to some user-allocated buffer
 * @param [in] len maximum number of bytes to write
 * @param [in] flags just flags, see man sendto for details
 * @param [in] dest_addr destination address
 * @param [in] addrlen size of dest_addr
 * @return number of bytes written on success, -1 in case of error and errno set
 *
 * This function is used to send messages to a socket.
 *
 * Possible errno values are described in sendto man page. The only special case
 * is EINTR which is handled internally and is returned to the caller only in
 * case when non-root fiber called the fiber sitting in fbr_sendto.
 *
 */
ssize_t fbr_sendto(FBR_P_ int sockfd, const void *buf, size_t len, int flags, const
		struct sockaddr *dest_addr, socklen_t addrlen);

/**
 * Fiber friendly libc accept wrapper.
 * @param [in] sockfd file descriptor to accept on
 * @param [in] addr client address
 * @param [in] addrlen size of addr
 * @return client socket fd on success, -1 in case of error and errno set
 *
 * This function is used to accept a connection on a listening socket.
 *
 * Possible errno values are described in accept man page. The only special case
 * is EINTR which is handled internally and is returned to the caller only in
 * case when non-root fiber called the fiber sitting in fbr_accept.
 *
 */
int fbr_accept(FBR_P_ int sockfd, struct sockaddr *addr, socklen_t *addrlen);

/**
 * Puts current fiber to sleep.
 * @param [in] seconds maximum number of seconds to sleep
 * @return number of seconds actually being asleep
 *
 * This function is used to put current fiber into sleep. It will wake up after
 * the desired time has passed or earlier if some other fiber has called it.
 */
ev_tstamp fbr_sleep(FBR_P_ ev_tstamp seconds);

/**
 * Prints fiber call stack to stderr.
 *
 * useful while debugging obscure fiber call problems.
 */
void fbr_dump_stack(FBR_P_ fbr_logutil_func_t log);

/**
 * Creates a mutex.
 * @return newly allocated mutex
 *
 * Mutexes are helpful when your fiber has a critical code section including
 * several fbr_* calls. In this case execution of multiple copies of your fiber
 * may get mixed up.
 *
 * @see fbr_mutex_lock
 * @see fbr_mutex_trylock
 * @see fbr_mutex_unlock
 * @see fbr_mutex_destroy
 */
struct fbr_mutex *fbr_mutex_create(FBR_P);

/**
 * Locks a mutex.
 * @param [in] mutex pointer to mutex created by fbr_mutex_create
 *
 * Attempts to lock a mutex. If mutex is already locked then the calling fiber
 * is suspended until the mutex is eventually freed.
 *
 * @see fbr_mutex_create
 * @see fbr_mutex_trylock
 * @see fbr_mutex_unlock
 * @see fbr_mutex_destroy
 */
void fbr_mutex_lock(FBR_P_ struct fbr_mutex *mutex);

/**
 * Tries to locks a mutex.
 * @param [in] mutex pointer to mutex created by fbr_mutex_create
 * @return 1 if lock was successful, 0 otherwise
 *
 * Attempts to lock a mutex. Returns immediately despite of locking being
 * successful or not.
 *
 * @see fbr_mutex_create
 * @see fbr_mutex_lock
 * @see fbr_mutex_unlock
 * @see fbr_mutex_destroy
 */
int fbr_mutex_trylock(FBR_P_ struct fbr_mutex *mutex);

/**
 * Unlocks a mutex.
 * @param [in] mutex pointer to mutex created by fbr_mutex_create
 *
 * Unlocks the given mutex. An other fiber that is waiting for it (if any) will
 * be called upon next libev loop iteration.
 *
 * @see fbr_mutex_create
 * @see fbr_mutex_lock
 * @see fbr_mutex_trylock
 * @see fbr_mutex_destroy
 */
void fbr_mutex_unlock(FBR_P_ struct fbr_mutex *mutex);

/**
 * Frees a mutex.
 * @param [in] mutex pointer to mutex created by fbr_mutex_create
 *
 * Frees used resources. It does not unlock the mutex.
 *
 * @see fbr_mutex_create
 * @see fbr_mutex_lock
 * @see fbr_mutex_unlock
 * @see fbr_mutex_trylock
 */
void fbr_mutex_destroy(FBR_P_ struct fbr_mutex *mutex);

/**
 * Creates a conditional variable.
 *
 * Conditional variable is useful primitive for fiber synchronisation. A set of
 * fibers may be waiting until certain condition is met. Another fiber can
 * trigger this condition for one or all waiting fibers.
 *
 * @see fbr_cond_destroy
 * @see fbr_cond_wait
 * @see fbr_cond_broadcast
 * @see fbr_cond_signal
 */
struct fbr_cond_var *fbr_cond_create(FBR_P);

/**
 * Destroys a conditional variable.
 *
 * This just frees used resources. No signals are sent to waiting fibers.
 *
 * @see fbr_cond_create
 * @see fbr_cond_wait
 * @see fbr_cond_broadcast
 * @see fbr_cond_signal
 */
void fbr_cond_destroy(FBR_P_ struct fbr_cond_var *cond);

/**
 * Waits until condition is met.
 *
 * Current fiber is suspended until a signal is sent via fbr_cond_signal or
 * fbr_cond_broadcast to the corresponding conditional variable.
 *
 * A mutex must be acquired by the calling fiber prior to waiting for a
 * condition. Internally mutex is released and reacquired again before
 * returning. Upon successful return calling fiber will hold the mutex.
 *
 * @see fbr_cond_create
 * @see fbr_cond_destroy
 * @see fbr_cond_broadcast
 * @see fbr_cond_signal
 */
int fbr_cond_wait(FBR_P_ struct fbr_cond_var *cond, struct fbr_mutex *mutex);

/**
 * Broadcasts a signal to all fibers waiting for condition.
 *
 * All fibers waiting for a condition will be added to run queue (and will
 * eventually be run, one per event loop iteration).
 *
 * @see fbr_cond_create
 * @see fbr_cond_destroy
 * @see fbr_cond_wait
 * @see fbr_cond_signal
 */
void fbr_cond_broadcast(FBR_P_ struct fbr_cond_var *cond);

/**
 * Signals to first fiber waiting for condition.
 *
 * Exactly one fiber (first one) waiting for a condition will be added to run queue (and will
 * eventually be run, one per event loop iteration).
 *
 * @see fbr_cond_create
 * @see fbr_cond_destroy
 * @see fbr_cond_wait
 * @see fbr_cond_signal
 */
void fbr_cond_signal(FBR_P_ struct fbr_cond_var *cond);

#endif
