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
 * sudo apt-get install cmake libev-dev valgrind check
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
#include <stddef.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <assert.h>
#include <ev.h>

#include <evfibers/config.h>

/**
 * Maximum allowed level of fbr_transfer nesting within fibers.
 */
#define FBR_CALL_STACK_SIZE 16
/**
 * Default stack size for a fiber of 64 KB.
 */
#define FBR_STACK_SIZE (64 * 1024) /* 64 KB */

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

/**
 * Just for convenience we have container_of macro here.
 *
 * Nothing specific. You can find the same one in the linux kernel tree.
 */
#define fbr_container_of(ptr, type, member) ({                       \
		const typeof( ((type *)0)->member ) *__mptr = (ptr); \
		(type *)( (char *)__mptr - offsetof(type,member) );  \
		})

struct fbr_context_private;
struct fbr_logger;

struct fbr_id_s {
	uint64_t g;
	void *p;
} __attribute__((packed));
/**
 * Fiber ID type.
 *
 * For you it's just an opaque type.
 */
typedef struct fbr_id_s fbr_id_t;

extern const fbr_id_t FBR_ID_NULL;

static inline int fbr_id_eq(fbr_id_t a, fbr_id_t b)
{
	return a.p == b.p && a.g == b.g;
}

static inline int fbr_id_isnull(fbr_id_t a)
{
	return fbr_id_eq(a, FBR_ID_NULL);
}

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
	FBR_EBUFFERMMAP,
	FBR_ENOKEY,
	FBR_EPROTOBUF,
	FBR_EBUFFERNOSPACE,
	FBR_EEIO,
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
 * Same as FBR_P but with unused attribute.
 */
#define FBR_PU __attribute__((unused)) FBR_P

/**
 * Same as FBR_P, but with comma afterwards for use in functions that accept
 * more that one parameter (which itself is the context pointer).
 */
#define FBR_P_ FBR_P,

/**
 * Same as FBR_P_ but unused attribute.
 */
#define FBR_PU_ __attribute__((unused)) FBR_P_

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
typedef void (*fbr_log_func_t)(FBR_P_ struct fbr_logger *logger,
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

/**
 * Convenient function to test if certain log level will actually be logged.
 *
 * Useful when you need to perform some processing before logging. Wrapping
 * your processing in ``if'' statement based on this macros' result can perform
 * the processing only if its result will get logged.
 */
static inline int fbr_need_log(FBR_P_ enum fbr_log_level level)
{
	return level <= fctx->logger->level;
}

/**
 * Convenient function to set current log level.
 */
static inline void fbr_set_log_level(FBR_P_ enum fbr_log_level desired_level)
{
	fctx->logger->level = desired_level;
}

/**
 * Type of events supported by the library.
 * @see fbr_ev_wait
 */
enum fbr_ev_type {
	FBR_EV_WATCHER = 1, /*!< libev watcher event */
	FBR_EV_MUTEX, /*!< fbr_mutex event */
	FBR_EV_COND_VAR, /*!< fbr_cond_var event */
	FBR_EV_EIO, /*!< libeio event */
};

struct fbr_ev_base;

/**
 * Destructor function type.
 * @param [in] arg user-defined data argument
 *
 * This function gets called when containing fiber dies or destructor is
 * removed with call flag set to 1.
 * @see fbr_destructor
 * @see fbr_log_func_t
 */
typedef void (*fbr_destructor_func_t)(FBR_P_ void *arg);

/**
 * Destructor structure.
 *
 * This structure holds information required for destruction. As it's defined
 * in public interface, it may be used as stack-allocatable destructor (it's
 * used internally the same way).
 *
 * Stack-allocated destructor might be useful if one has some resource (e.g.
 * file descriptor), which needs to be destructed in some way, and it's
 * lifespan continues across several fbr_* calls. While being in some library
 * call, a fiber may be reclaimed, but it's stack remains intact until
 * reclaimed. Destructor is called before the stack becomes dangerous to use
 * and guarantees resource destruction.
 *
 * User is supposed to fill in the func and arg fields.
 * @see fbr_destructor_func_t
 * @see fbr_destructor_add
 * @see fbr_destructor_remove
 */
struct fbr_destructor {
	fbr_destructor_func_t func; /*!< destructor function */
	void *arg; /*!< destructor function argument (optional) */
	TAILQ_ENTRY(fbr_destructor) entries; //Private
	int active; //Private
};

#define FBR_DESTRUCTOR_INITIALIZER { \
	.func   = NULL,              \
	.arg    = NULL,              \
	.active = 0,                 \
};

struct fbr_id_tailq;

struct fbr_id_tailq_i {
	/* Private structure */
	fbr_id_t id;
	struct fbr_ev_base *ev;
	TAILQ_ENTRY(fbr_id_tailq_i) entries;
	struct fbr_destructor dtor;
	struct fbr_id_tailq *head;
};

TAILQ_HEAD(fbr_id_tailq, fbr_id_tailq_i);

/**
 * Base struct for all events.
 *
 * All other event structures ``inherit'' this one by inclusion of it as
 * ev_base member.
 * @see fbr_ev_upcast
 * @see fbr_ev_wait
 */
struct fbr_ev_base {
	enum fbr_ev_type type; /*!< type of the event */
	fbr_id_t id; /*!< id of a fiber that is waiting for this event */
	int arrived; /*!< flag indicating that this event has arrived */
	struct fbr_context *fctx; //Private
	void *data; //Private
	struct fbr_id_tailq_i item; //Private
};

/**
 * Convenience macro to save some typing.
 *
 * Allows you to cast fbr_ev_base to some other event struct via
 * fbr_container_of magic.
 * @see fbr_container_of
 * @see fbr_ev_base
 */
#define fbr_ev_upcast(ptr, type_no_struct) \
       fbr_container_of(ptr, struct type_no_struct, ev_base)

/**
 * libev watcher event.
 *
 * This event struct can represent any libev watcher which should be
 * initialized and started. You can safely pass NULL as a callback for the
 * watcher since the library sets up it's own callback.
 * @see fbr_ev_upcast
 * @see fbr_ev_wait
 */
struct fbr_ev_watcher {
	ev_watcher *w; /*!< libev watcher */
	struct fbr_ev_base ev_base;
};

/**
 * fbr_mutex event.
 *
 * This event struct can represent mutex aquisition waiting.
 * @see fbr_ev_upcast
 * @see fbr_ev_wait
 */
struct fbr_ev_mutex {
	struct fbr_mutex *mutex; /*!< mutex we're interested in */
	struct fbr_ev_base ev_base;
};

/**
 * fbr_cond_var event.
 *
 * This event struct can represent conditional variable waiting.
 * @see fbr_ev_upcast
 * @see fbr_ev_wait
 */
struct fbr_ev_cond_var {
	struct fbr_cond_var *cond; /*!< conditional variable we're interested
				     in */
	struct fbr_mutex *mutex; /*!< mutex to protect conditional variable*/
	struct fbr_ev_base ev_base;
};

/**
 * Mutex structure.
 *
 * This structure represent a mutex.
 * @see fbr_mutex_init
 * @see fbr_mutex_destroy
 */
struct fbr_mutex {
	fbr_id_t locked_by;
	struct fbr_id_tailq pending;
	TAILQ_ENTRY(fbr_mutex) entries;
};

/**
 * Conditional variable structure.
 *
 * This structure represent a conditional variable.
 * @see fbr_mutex_init
 * @see fbr_mutex_destroy
 */
struct fbr_cond_var {
	struct fbr_mutex *mutex;
	struct fbr_id_tailq waiting;
};

/**
 * Virtual ring buffer implementation.
 *
 * Low-level data structure implemented using memory mappings of the same
 * physical pages to provide no-copy overhead efficient ring buffer
 * imlementation.
 * @see fbr_vrb_init
 * @see fbr_vrb_destroy
 */
struct fbr_vrb {
	void *mem_ptr;
	size_t mem_ptr_size;
	void *lower_ptr;
	void *upper_ptr;
	size_t ptr_size;
	void *data_ptr;
	void *space_ptr;
};

/**
 * Inter-fiber communication pipe.
 *
 * This structure represent a communication pipe between two (or more) fibers.
 *
 * Higher level wrapper around fbr_vrb.
 * @see fbr_buffer_init
 * @see fbr_buffer_destroy
 * @see struct fbr_vrb
 */
struct fbr_buffer {
	struct fbr_vrb vrb;
	size_t prepared_bytes;
	size_t waiting_bytes;
	struct fbr_cond_var committed_cond;
	struct fbr_mutex write_mutex;
	struct fbr_cond_var bytes_freed_cond;
	struct fbr_mutex read_mutex;
};

/**
 * Fiber-local data key.
 *
 * @see fbr_key_create
 * @see fbr_key_delete
 * @see fbr_key_get_data
 * @see fbr_key_set_data
 */
typedef unsigned int fbr_key_t;


/**
 * Maximum numbef of fiber-local keys allowed.
 */
#define FBR_MAX_KEY 64


/**
 * Adds destructor to fiber list.
 * @param [in] dtor destructor to register
 *
 * This function registers a destructor. User must guarantee that destructor
 * object stays alive until fiber is reclaimed or destructor is removed,
 * whichever comes first.
 * @see fbr_destructor
 */
void fbr_destructor_add(FBR_P_ struct fbr_destructor *dtor);

/**
 * Removes destructor from fiber list.
 * @param [in] dtor destructor to unregister
 * @param [in] call flag indicating if destructor needs to be called
 *
 * This function unregisters a destructor. User may specify a flag for
 * destructor function to be called.
 * @see fbr_destructor
 */
void fbr_destructor_remove(FBR_P_ struct fbr_destructor *dtor,
		int call);

/**
 * Initializes a destructor.
 * @param [in] dtor destructor to initialize
 *
 * This function should be called before a newly allocated destructor is used.
 * Alternatively you may use FBR_DESTRUCTOR_INITIALIZER macro as a initializing
 * value upon declaration.
 * @see fbr_destructor
 */
static inline void fbr_destructor_init(struct fbr_destructor *dtor)
{
	memset(dtor, 0x00, sizeof(*dtor));
}

/**
 * Initializer for libev watcher event.
 *
 * This functions properly initializes fbr_ev_watcher struct. You should not do
 * it manually.
 * @see fbr_ev_watcher
 * @see fbr_ev_wait
 */
void fbr_ev_watcher_init(FBR_P_ struct fbr_ev_watcher *ev, ev_watcher *w);

/**
 * Initializer for mutex event.
 *
 * This functions properly initializes fbr_ev_mutex struct. You should not do
 * it manually.
 * @see fbr_ev_mutex
 * @see fbr_ev_wait
 */
void fbr_ev_mutex_init(FBR_P_ struct fbr_ev_mutex *ev,
		struct fbr_mutex *mutex);

/**
 * Initializer for conditional variable event.
 *
 * This functions properly initializes fbr_ev_cond_var struct. You should not do
 * it manually.
 * @see fbr_ev_cond_var
 * @see fbr_ev_wait
 */
void fbr_ev_cond_var_init(FBR_P_ struct fbr_ev_cond_var *ev,
		struct fbr_cond_var *cond, struct fbr_mutex *mutex);

/**
 * Event awaiting function (one event only wrapper).
 * @param [in] one the event base pointer of the event to wait for
 * @returns 0 on success, -1 upon error
 *
 * This functions wraps fbr_ev_wait passing only one event to it.
 * @see fbr_ev_base
 * @see fbr_ev_wait
 */
int fbr_ev_wait_one(FBR_P_ struct fbr_ev_base *one);

/**
 * Event awaiting function (generic one).
 * @param [in] events array of event base pointers
 * @returns the number of events arrived or -1 upon error
 *
 * This function waits until any event from events array arrives. Only one
 * event can arrive at a time. It returns a pointer to the same event that was
 * passed in events array.
 * @see fbr_ev_base
 * @see fbr_ev_wait_one
 */
int fbr_ev_wait(FBR_P_ struct fbr_ev_base *events[]);

/**
 * Event awaiting function with timeout.
 * @param [in] events array of event base pointers
 * @param [in] timeout in seconds to wait for the events
 * @returns the number of events arrived or -1 upon error
 *
 * This function is a convenient wrapper around fbr_ev_wait, it just creates a
 * timer watcher and makes new events array with the timer watcher included.
 * Timer event is not counted in the number of returned events.
 * @see fbr_ev_wait
 */
int fbr_ev_wait_to(FBR_P_ struct fbr_ev_base *events[], ev_tstamp timeout);

/**
 * Transfer of fiber context to another fiber.
 * @param [in] to callee id
 * @returns 0 on success, -1 on failure with f_errno set.
 *
 * This function transfers the execution context to other fiber. It returns as
 * soon as the called fiber yields. In case of error it returns immediately.
 * @see fbr_yield
 */
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
 * Maximum length of fiber's name.
 */
#define FBR_MAX_FIBER_NAME 64

/**
 * Creates a new fiber.
 * @param [in] name fiber name, used for identification it
 * backtraces, etc.
 * @param [in] func function used as a fiber's ``main''.
 * @param [in] stack_size stack size (0 for default).
 * @param [in] arg user supplied argument to a fiber.
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
 * Retrieve a name of the fiber.
 * @param [in] id identificator of a fiber
 * @return pointer to charater buffer or NULL on error
 *
 * The name is located in the statically allocated buffer of size
 * FBR_MAX_FIBER_NAME.
 *
 * Don't try to free it!
 *
 * @see fbr_create
 * @see fbr_set_name
 */
const char *fbr_get_name(FBR_P_ fbr_id_t id);

/**
 * Sets a name for the fiber.
 * @param [in] id identificator of a fiber
 * @param [in] name new name for a fiber
 * @return 0 on success, -1 on error.
 *
 * The name is located in the statically allocated buffer of size
 * FBR_MAX_FIBER_NAME. If your name does not fit, it will be trimmed.
 *
 * @see fbr_get_name
 */
int fbr_set_name(FBR_P_ fbr_id_t id, const char *name);

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

int fbr_set_reclaim(FBR_P_ fbr_id_t fiber);
int fbr_set_noreclaim(FBR_P_ fbr_id_t fiber);
int fbr_want_reclaim(FBR_P_ fbr_id_t fiber);

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
 * Fiber-local key creation.
 *
 * This created a new unique key and stores it in key.
 */
int fbr_key_create(FBR_P_ fbr_key_t *key);

/**
 * Fiber-local key deletion.
 * This explicitly destroys a key.
 */
int fbr_key_delete(FBR_P_ fbr_key_t key);

/**
 * Sets fiber-local key data.
 * This stores a value under a key.
 */
int fbr_key_set(FBR_P_ fbr_id_t id, fbr_key_t key, void *value);

/**
 * Gets fiber-local key data.
 * This retrieves the value under a key.
 */
void *fbr_key_get(FBR_P_ fbr_id_t id, fbr_key_t key);

/**
 * Yields execution to other fiber.
 *
 * When a fiber is waiting for some incoming event --- it should yield. This
 * will pop current fiber from the fiber stack and transfer the execution
 * context to the next fiber from the stack making that fiber a new current
 * one.
 *
 * It loops through all fibers subscribed to specified multicast group id.
 * @see fbr_transfer
 */
void fbr_yield(FBR_P);

/**
 * Yields execution to other fiber returning the execution at the next event
 * loop run.
 *
 * Useful inside of some busy loop with lots of iterations to play nicely with
 * other fibers which might start starving on the execution time.
 * @see fbr_yield
 * @see fbr_transfer
 */
void fbr_cooperate(FBR_P);

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
 * Fiber friendly connect wrapper.
 * @param [in] sockfd - socket file descriptor
 * @param [in] addr - pointer to struct sockaddr, containing connection details
 * @param [in] length of struct sockaddr
 * @return zero on success, -1 in case of error and errno set
 *
 * Connect wrapper, that connects the socket referred to by the file
 * descriptor sockfd to the address specified by addr.
 * starting at buf. Calling fiber will be blocked until sockfd is connected or
 * error is occured
 *
 * Possible errno values are described in connect man page. The only special case
 * is EINPROGRESS which is handled internally.
 */
int fbr_connect(FBR_P_ int sockfd, const struct sockaddr *addr,
                   socklen_t addrlen);

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
 * Fiber friendly libc recv wrapper.
 * @param [in] sockfd file descriptor to read from
 * @param [in] buf pointer to some user-allocated buffer
 * @param [in] len maximum number of bytes to read
 * @param [in] flags just flags, see man recvfrom for details
 * @return number of bytes read on success, -1 in case of error and errno set
 *
 * This function is used to receive messages from a socket.
 *
 * Possible errno values are described in recv man page.
 */
ssize_t fbr_recv(FBR_P_ int sockfd, void *buf, size_t len, int flags);

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
 *
 * Fiber friendly libc send wrapper.
 * @param [in] sockfd file descriptor to write to
 * @param [in] buf pointer to some user-allocated buffer
 * @param [in] len maximum number of bytes to write
 * @param [in] flags just flags, see man sendto for details
 * @return number of bytes written on success, -1 in case of error and errno set
 *
 * This function is used to send messages to a socket.
 */
ssize_t fbr_send(FBR_P_ int sockfd, const void *buf, size_t len, int flags);

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
 * Initializes a mutex.
 * @param [in] mutex a mutex structure to initialize
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
void fbr_mutex_init(FBR_P_ struct fbr_mutex *mutex);

/**
 * Locks a mutex.
 * @param [in] mutex pointer to a mutex
 *
 * Attempts to lock a mutex. If mutex is already locked then the calling fiber
 * is suspended until the mutex is eventually freed.
 *
 * @see fbr_mutex_init
 * @see fbr_mutex_trylock
 * @see fbr_mutex_unlock
 * @see fbr_mutex_destroy
 */
void fbr_mutex_lock(FBR_P_ struct fbr_mutex *mutex);

/**
 * Tries to locks a mutex.
 * @param [in] mutex pointer to a mutex
 * @return 1 if lock was successful, 0 otherwise
 *
 * Attempts to lock a mutex. Returns immediately despite of locking being
 * successful or not.
 *
 * @see fbr_mutex_init
 * @see fbr_mutex_lock
 * @see fbr_mutex_unlock
 * @see fbr_mutex_destroy
 */
int fbr_mutex_trylock(FBR_P_ struct fbr_mutex *mutex);

/**
 * Unlocks a mutex.
 * @param [in] mutex pointer to a mutex
 *
 * Unlocks the given mutex. An other fiber that is waiting for it (if any) will
 * be called upon next libev loop iteration.
 *
 * @see fbr_mutex_init
 * @see fbr_mutex_lock
 * @see fbr_mutex_trylock
 * @see fbr_mutex_destroy
 */
void fbr_mutex_unlock(FBR_P_ struct fbr_mutex *mutex);

/**
 * Destroys a mutex.
 * @param [in] mutex pointer to mutex
 *
 * Frees used resources. It does not unlock the mutex.
 *
 * @see fbr_mutex_init
 * @see fbr_mutex_lock
 * @see fbr_mutex_unlock
 * @see fbr_mutex_trylock
 */
void fbr_mutex_destroy(FBR_P_ struct fbr_mutex *mutex);

/**
 * Initializes a conditional variable.
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
void fbr_cond_init(FBR_P_ struct fbr_cond_var *cond);

/**
 * Destroys a conditional variable.
 *
 * This just frees used resources. No signals are sent to waiting fibers.
 *
 * @see fbr_cond_init
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
 * @see fbr_cond_init
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
 * @see fbr_cond_init
 * @see fbr_cond_destroy
 * @see fbr_cond_wait
 * @see fbr_cond_signal
 */
void fbr_cond_broadcast(FBR_P_ struct fbr_cond_var *cond);

/**
 * Signals to first fiber waiting for a condition.
 *
 * Exactly one fiber (first one) waiting for a condition will be added to run
 * queue (and will eventually be run, one per event loop iteration).
 *
 * @see fbr_cond_init
 * @see fbr_cond_destroy
 * @see fbr_cond_wait
 * @see fbr_cond_signal
 */
void fbr_cond_signal(FBR_P_ struct fbr_cond_var *cond);

/**
 * Initializes memory mappings.
 * @param [in] vrb a pointer to fbr_vrb
 * @param [in] size length of the data
 * @param [in] file_pattern file name patterm for underlying mmap storage
 * @returns 0 on succes, -1 on error.
 *
 * This function mmaps adjacent virtual memory regions of required size which
 * correspond to the same physical memory region. Also it adds two page-sized
 * regions on the left and on the right with PROT_NONE access as a guards.
 *
 * It does mmaps on the same file, which is unlinked and closed afterwards, so
 * it will not pollute file descriptor space of a process and the filesystem.
 *
 * @see struct fbr_vrb
 * @see fbr_vrb_destroy
 */
int fbr_vrb_init(struct fbr_vrb *vrb, size_t size, const char *file_pattern);


/**
 * Destroys mappings.
 * @param [in] vrb a pointer to fbr_vrb
 *
 * This function unmaps all the mappings done by fbr_vrb_init.
 *
 * @see fbr_vrb_init
 */
void fbr_vrb_destroy(struct fbr_vrb *vrb);

/**
 * Retrieves data length.
 * @param [in] vrb a pointer to fbr_vrb
 * @returns length of the data area.
 *
 * @see struct fbr_vrb
 */
static inline size_t fbr_vrb_data_len(struct fbr_vrb *vrb)
{
	return vrb->space_ptr - vrb->data_ptr;
}

/**
 * Retrieves space length.
 * @param [in] vrb a pointer to fbr_vrb
 * @returns length of the space area.
 *
 * @see struct fbr_vrb
 */
static inline size_t fbr_vrb_space_len(struct fbr_vrb *vrb)
{
	return vrb->data_ptr + vrb->ptr_size - vrb->space_ptr;
}

/**
 * Retrieves total buffer capacity.
 * @param [in] vrb a pointer to fbr_vrb
 * @returns maximum length of data this vrb can hold.
 *
 * @see struct fbr_vrb
 */
static inline size_t fbr_vrb_capacity(struct fbr_vrb *vrb)
{
	return vrb->ptr_size;
}

/**
 * Retrieves space area pointer.
 * @param [in] vrb a pointer to fbr_vrb
 * @returns pointer to the start of space area.
 *
 * @see struct fbr_vrb
 */
static inline void *fbr_vrb_space_ptr(struct fbr_vrb *vrb)
{
	return vrb->space_ptr;
}

/**
 * Retrieves data area pointer.
 * @param [in] vrb a pointer to fbr_vrb
 * @returns pointer to the start of data area.
 *
 * @see struct fbr_vrb
 */
static inline void *fbr_vrb_data_ptr(struct fbr_vrb *vrb)
{
	return vrb->data_ptr;
}

/**
 * Give data to a vrb.
 * @param [in] vrb a pointer to fbr_vrb
 * @param [in] size length of the data
 * @returns 0 on succes, -1 on error.
 *
 * This function marks size bytes of space area as data starting from the
 * beginning of space area. It's your responsibility to fill those area with
 * the actual data.
 *
 * @see struct fbr_vrb
 * @see fbr_vrb_take
 */
static inline int fbr_vrb_give(struct fbr_vrb *vrb, size_t size)
{
	if (size > fbr_vrb_space_len(vrb))
		return -1;
	vrb->space_ptr += size;
	return 0;
}

/**
 * Take data from a vrb.
 * @param [in] vrb a pointer to fbr_vrb
 * @param [in] size length of the data
 * @returns 0 on succes, -1 on error.
 *
 * This function marks size bytes of data area as space starting from the
 * beginning of data area. It's your responsibility to drop any references to
 * the region as it might be overwritten later.
 *
 * @see struct fbr_vrb
 * @see fbr_vrb_give
 */
static inline int fbr_vrb_take(struct fbr_vrb *vrb, size_t size)
{
	if (size > fbr_vrb_data_len(vrb))
		return -1;
	vrb->data_ptr += size;

	if (vrb->data_ptr >= vrb->upper_ptr) {
		vrb->data_ptr -= vrb->ptr_size;
		vrb->space_ptr -= vrb->ptr_size;
	}

	return 0;
}

/**
 * Resets a vrb.
 * @param [in] vrb a pointer to fbr_vrb
 *
 * This function resets data and space pointers to their initial value,
 * efficiently marking vrb as empty.
 *
 * @see struct fbr_vrb
 */
static inline void fbr_vrb_reset(struct fbr_vrb *vrb)
{
	vrb->data_ptr = vrb->lower_ptr;
	vrb->space_ptr = vrb->lower_ptr;
}

/* Private function */
int fbr_vrb_resize_do(struct fbr_vrb *vrb, size_t new_size,
		const char *file_pattern);

/**
 * Resizes a vrb.
 * @param [in] vrb a pointer to fbr_vrb
 * @param [in] new_size new length of the data
 * @param [in] file_pattern file name patterm for underlying mmap storage
 * @returns 0 on succes, -1 on error.
 *
 * This function does new mappings and copies the data over. Old mappings will
 * be destroyed and all pointers to old data will be invalid after this
 * operation.
 *
 * @see struct fbr_vrb
 * @see fbr_vrb_init
 */
static inline int fbr_vrb_resize(struct fbr_vrb *vrb, size_t new_size,
		const char *file_pattern)
{
	struct fbr_vrb tmp;
	int rv;
	if (fbr_vrb_capacity(vrb) >= new_size)
		return 0;
	memcpy(&tmp, vrb, sizeof(tmp));
	rv = fbr_vrb_init(vrb, new_size, file_pattern);
	if (rv) {
		memcpy(vrb, &tmp, sizeof(tmp));
		return rv;
	}
	memcpy(fbr_vrb_space_ptr(vrb), fbr_vrb_data_ptr(&tmp),
			fbr_vrb_data_len(&tmp));
	fbr_vrb_give(vrb, fbr_vrb_data_len(&tmp));
	fbr_vrb_destroy(&tmp);
	return 0;
}


/**
 * Initializes a circular buffer with pipe semantics.
 * @param [in] buffer fbr_buffer structure to initialize
 * @param [in] size size hint for the buffer
 * @returns 0 on succes, -1 upon failure with f_errno set.
 *
 * This allocates a buffer with pipe semantics: you can write into it and later
 * read what you have written. The buffer will occupy size rounded up to page
 * size in physical memory, while occupying twice this size in virtual process
 * memory due to usage of two mirrored adjacent mmaps.
 */
int fbr_buffer_init(FBR_P_ struct fbr_buffer *buffer, size_t size);

/**
 * Amount of bytes filled with data.
 * @param [in] buffer a pointer to fbr_buffer
 * @returns number of bytes written to the buffer
 *
 * This function can be used to check if fbr_buffer_read_address will block.
 * @see fbr_buffer_free_bytes
 */
static inline size_t fbr_buffer_bytes(FBR_PU_ struct fbr_buffer *buffer)
{
	return fbr_vrb_data_len(&buffer->vrb);
}

/**
 * Amount of free bytes.
 * @param [in] buffer a pointer to fbr_buffer
 * @returns number of free bytes in the buffer
 *
 * This function can be used to check if fbr_buffer_alloc_prepare will block.
 * @see fbr_buffer_bytes
 */
static inline size_t fbr_buffer_free_bytes(FBR_PU_ struct fbr_buffer *buffer)
{
	return fbr_vrb_space_len(&buffer->vrb);
}

/**
 * Total capacity of a buffer.
 * @param [in] buffer a pointer to fbr_buffer
 * @returns maximum number of bytes the buffer may contain.
 *
 * This function may return total capacity larger that originally requested due
 * to size being rounded up to be a multiple of page size.
 * @see fbr_buffer_bytes
 * @see fbr_buffer_free_bytes
 */
static inline size_t fbr_buffer_size(FBR_PU_ struct fbr_buffer *buffer)
{
	return fbr_vrb_capacity(&buffer->vrb);
}

/**
 * Pointer to the start of ``space'' memory area.
 * @param [in] buffer a pointer to fbr_buffer
 * @returns pointer to space memory region start.
 *
 * This function returns a pointer to the start of free memory area inside the
 * buffer.
 *
 * You may use this function in case you have a fbr_buffer, that is not used as
 * an inter-fiber communication mechanism but only as a local circular data
 * buffer.
 *
 * Mixing space_ptr/data_ptr/give/take API with mutex-protected transactional
 * API might lead to corruption and is not recommended unless you know what you
 * are doing.
 * @see fbr_buffer_data_ptr
 * @see fbr_buffer_give
 * @see fbr_buffer_take
 */
static inline void *fbr_buffer_space_ptr(FBR_PU_ struct fbr_buffer *buffer)
{
	return fbr_vrb_space_ptr(&buffer->vrb);
}

/**
 * Pointer to the start of ``data'' memory area.
 * @param [in] buffer a pointer to fbr_buffer
 * @returns pointer to data memory region start.
 *
 * This function returns a pointer to the start of data memory area inside the
 * buffer.
 *
 * You may use this function in case you have a fbr_buffer, that is not used as
 * an inter-fiber communication mechanism but only as a local circular data
 * buffer.
 *
 * Mixing space_ptr/data_ptr/give/take API with mutex-protected transactional
 * API might lead to corruption and is not recommended unless you know what you
 * are doing.
 * @see fbr_buffer_data_ptr
 * @see fbr_buffer_give
 * @see fbr_buffer_take
 */
static inline void *fbr_buffer_data_ptr(FBR_PU_ struct fbr_buffer *buffer)
{
	return fbr_vrb_data_ptr(&buffer->vrb);
}

/**
 * Resets a buffer.
 * @param [in] buffer a pointer to fbr_buffer
 *
 * This function resets space and data pointers of the buffer to it's start,
 * which results in whole buffer being marked as space.
 *
 * This function does not affect the state of mutexes and conditional
 * variables, so using it while the buffer is in use by multiple fibers in not
 * safe.
 *
 * @see fbr_buffer_init
 * @see fbr_buffer_destroy
 */
static inline void fbr_buffer_reset(FBR_PU_ struct fbr_buffer *buffer)
{
	fbr_vrb_reset(&buffer->vrb);
}


/**
 * Destroys a circular buffer.
 * @param [in] buffer a pointer to fbr_buffer to free
 *
 * This unmaps all mmaped memory for the buffer. It does not do any fancy stuff
 * like waiting until buffer is empty etc., it just frees it.
 */
void fbr_buffer_destroy(FBR_P_ struct fbr_buffer *buffer);

/**
 * Prepares a chunk of memory to be committed to buffer.
 * @param [in] buffer a pointer to fbr_buffer
 * @param [in] size required size
 * @returns pointer to memory reserved for commit.
 *
 * This function reserves a chunk of memory (or waits until there is one
 * available, blocking current fiber) and returns pointer to it.
 *
 * A fiber trying to reserve a chunk of memory after some other fiber already
 * reserved it leads to the former fiber being blocked until the latter one
 * commits or aborts.
 * @see fbr_buffer_alloc_commit
 * @see fbr_buffer_alloc_abort
 */
void *fbr_buffer_alloc_prepare(FBR_P_ struct fbr_buffer *buffer, size_t size);

/**
 * Commits a chunk of memory to the buffer.
 * @param [in] buffer a pointer to fbr_buffer
 *
 * This function commits a chunk of memory previously reserved.
 * @see fbr_buffer_alloc_prepare
 * @see fbr_buffer_alloc_abort
 */
void fbr_buffer_alloc_commit(FBR_P_ struct fbr_buffer *buffer);

/**
 * Aborts a chunk of memory in the buffer.
 * @param [in] buffer a pointer to fbr_buffer
 *
 * This function aborts prepared chunk of memory previously reserved. It will
 * not be committed and the next fiber may reuse it for it's own purposes.
 * @see fbr_buffer_alloc_prepare
 * @see fbr_buffer_alloc_commit
 */
void fbr_buffer_alloc_abort(FBR_P_ struct fbr_buffer *buffer);

/**
 * Aborts a chunk of memory in the buffer.
 * @param [in] buffer a pointer to fbr_buffer
 * @param [in] size number of bytes required
 * @returns read address containing size bytes
 *
 * This function reserves (or waits till data is available, blocking current
 * fiber) a chunk of memory for reading. While a chunk of memory is reserved
 * for reading no other fiber can read from this buffer blocking until current
 * read is advanced or discarded.
 * @see fbr_buffer_read_advance
 * @see fbr_buffer_read_discard
 */
void *fbr_buffer_read_address(FBR_P_ struct fbr_buffer *buffer, size_t size);

/**
 * Confirms a read of chunk of memory in the buffer.
 * @param [in] buffer a pointer to fbr_buffer
 *
 * This function confirms that bytes obtained with fbr_buffer_read_address are
 * read and no other fiber will be able to read them.
 * @see fbr_buffer_read_address
 * @see fbr_buffer_read_discard
 */
void fbr_buffer_read_advance(FBR_P_ struct fbr_buffer *buffer);

/**
 * Discards a read of chunk of memory in the buffer.
 * @param [in] buffer a pointer to fbr_buffer
 *
 * This function discards bytes obtained with fbr_buffer_read_address. Next
 * fiber trying to read something from a buffer may obtain those bytes.
 * @see fbr_buffer_read_address
 * @see fbr_buffer_read_advance
 */
void fbr_buffer_read_discard(FBR_P_ struct fbr_buffer *buffer);

/**
 * Resizes the buffer.
 * @param [in] buffer a pointer to fbr_buffer
 * @param [in] size a new buffer length
 * @returns 0 on success, -1 on error.
 *
 * This function allocates new memory mapping of sufficient size and copies the
 * content of a buffer into it. Old mapping is destroyed.
 *
 * This operation is expensive and involves several syscalls, so it is
 * beneficiary to allocate a buffer of siffucuent size from the start.
 *
 * This function acquires both read and write mutex, and may block until read
 * or write operation has finished.
 * @see fbr_buffer_reset
 */
int fbr_buffer_resize(FBR_P_ struct fbr_buffer *buffer, size_t size);

/**
 * Helper function, returning read conditional variable.
 * @param [in] buffer a pointer to fbr_buffer
 * @returns read conditional variable
 */
static inline struct fbr_cond_var *fbr_buffer_cond_read(FBR_PU_
		struct fbr_buffer *buffer)
{
	return &buffer->committed_cond;
}

/**
 * Helper function, returning write conditional variable.
 * @param [in] buffer a pointer to fbr_buffer
 * @returns write conditional variable
 */
static inline struct fbr_cond_var *fbr_buffer_cond_write(FBR_PU_
		struct fbr_buffer *buffer)
{
	return &buffer->bytes_freed_cond;
}

/**
 * Helper function, which waits until read is possible.
 * @param [in] buffer a pointer to fbr_buffer
 * @param [in] size required read size
 * @returns 1 to be while friendly
 *
 * This function is useful when you need to wait for data to arrive on a buffer
 * in a while loop.
 */
static inline int fbr_buffer_wait_read(FBR_P_ struct fbr_buffer *buffer,
		size_t size)
{
	struct fbr_mutex mutex;
	int retval;
	fbr_mutex_init(FBR_A_ &mutex);
	fbr_mutex_lock(FBR_A_ &mutex);
	while (fbr_buffer_bytes(FBR_A_ buffer) < size) {
		retval = fbr_cond_wait(FBR_A_ &buffer->committed_cond, &mutex);
		assert(0 == retval);
		(void)retval;
	}
	fbr_mutex_unlock(FBR_A_ &mutex);
	fbr_mutex_destroy(FBR_A_ &mutex);
	return 1;
}

/**
 * Helper function, which test if read is possible.
 * @param [in] buffer a pointer to fbr_buffer
 * @param [in] size required read size
 * @returns 1 if read is possible, 0 otherwise
 *
 * This function is useful when you need to test if you can read some data from
 * the buffer without blocking.
 */
static inline int fbr_buffer_can_read(FBR_P_ struct fbr_buffer *buffer,
		size_t size)
{
	return fbr_buffer_bytes(FBR_A_ buffer) >= size;
}

/**
 * Helper function, which waits until write is possible.
 * @param [in] buffer a pointer to fbr_buffer
 * @param [in] size required write size
 * @returns 1 to be while friendly
 *
 * This function is useful when you need to wait for free space on a buffer
 * in a while loop.
 */
static inline int fbr_buffer_wait_write(FBR_P_ struct fbr_buffer *buffer,
		size_t size)
{
	struct fbr_mutex mutex;
	int retval;
	fbr_mutex_init(FBR_A_ &mutex);
	fbr_mutex_lock(FBR_A_ &mutex);
	while (fbr_buffer_free_bytes(FBR_A_ buffer) < size) {
		retval = fbr_cond_wait(FBR_A_ &buffer->bytes_freed_cond,
				&mutex);
		assert(0 == retval);
		(void)retval;
	}
	fbr_mutex_unlock(FBR_A_ &mutex);
	fbr_mutex_destroy(FBR_A_ &mutex);
	return 1;
}

/**
 * Helper function, which test if write is possible.
 * @param [in] buffer a pointer to fbr_buffer
 * @param [in] size required write size
 * @returns 1 if write is possible, 0 otherwise
 *
 * This function is useful when you need to test if you can write some data to
 * the buffer without blocking.
 */
static inline int fbr_buffer_can_write(FBR_P_ struct fbr_buffer *buffer,
		size_t size)
{
	return fbr_buffer_free_bytes(FBR_A_ buffer) >= size;
}

/**
 * Gets fiber user data pointer.
 * @param [in] id fiber id
 * @returns user data pointer on success, NULL on failure with f_errno set
 *
 * This function allows you to retrieve user data pointer.
 * @see fbr_set_user_data
 */
void *fbr_get_user_data(FBR_P_ fbr_id_t id);

/**
 * Sets fiber user data pointer.
 * @param [in] id fiber id
 * @param [in] data pointer to user data
 * @returns 0 on success, -1 upon failure with f_errno set
 *
 * This function allows you to extend fiber with some user structure.
 * @see fbr_get_user_data
 */
int fbr_set_user_data(FBR_P_ fbr_id_t id, void *data);

pid_t fbr_popen3(FBR_P_ const char *filename, char *const argv[],
		char *const envp[], const char *working_dir,
		int *stdin_w_ptr, int *stdout_r_ptr, int *stderr_r_ptr);

static inline pid_t fbr_popen2(FBR_P_ const char *filename, char *const argv[],
		char *const envp[], const char *working_dir, int *stdin_w_ptr,
		int *stdout_r_ptr)
{
	return fbr_popen3(FBR_A_ filename, argv, envp, working_dir, stdin_w_ptr,
			stdout_r_ptr, NULL);
}

static inline pid_t fbr_popen0(FBR_P_ const char *filename, char *const argv[],
		char *const envp[], const char *working_dir)
{
	return fbr_popen3(FBR_A_ filename, argv, envp, working_dir, NULL, NULL,
			NULL);
}

int fbr_waitpid(FBR_P_ pid_t pid);

int fbr_system(FBR_P_ const char *filename, char *const argv[],
		char *const envp[], const char *working_dir);

#endif
