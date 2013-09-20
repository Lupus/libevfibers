local ffi = require("ffi")
ffi.cdef[[
struct fbr_context_private;
struct fbr_logger;
struct fbr_async;






typedef uint64_t fbr_id_t;

extern const fbr_id_t FBR_ID_NULL;

static inline int fbr_id_eq(fbr_id_t a, fbr_id_t b)
{
 return a == b;
}

static inline int fbr_id_isnull(fbr_id_t a)
{
 return fbr_id_eq(a, FBR_ID_NULL);
}
enum fbr_error_code {
 FBR_SUCCESS = 0,
 FBR_EINVAL,
 FBR_ENOFIBER,
 FBR_ESYSTEM,
 FBR_EBUFFERMMAP,
 FBR_ENOKEY,
 FBR_EASYNC,
 FBR_EPROTOBUF,
 FBR_ENOTFOREIGN,
};
struct fbr_context {
 struct fbr_context_private *__p;

 enum fbr_error_code f_errno;
 struct fbr_logger *logger;
};
typedef void (*fbr_fiber_func_t)(struct fbr_context *fctx, void *_arg);
typedef void (*fbr_alloc_destructor_func_t)(struct fbr_context *fctx, void *ptr, void *context);






enum fbr_log_level {
 FBR_LOG_ERROR = 0,
 FBR_LOG_WARNING,
 FBR_LOG_NOTICE,
 FBR_LOG_INFO,
 FBR_LOG_DEBUG
};

struct fbr_logger;
typedef void (*fbr_log_func_t)(struct fbr_context *fctx, struct fbr_logger *logger,
  enum fbr_log_level level, const char *format, va_list ap);
typedef void (*fbr_logutil_func_t)(struct fbr_context *fctx, const char *format, ...);






struct fbr_logger {
 fbr_log_func_t logv;
 enum fbr_log_level level;
 void *data;
};
static inline int fbr_need_log(struct fbr_context *fctx, enum fbr_log_level level)
{
 return level <= fctx->logger->level;
}




static inline void fbr_set_log_level(struct fbr_context *fctx, enum fbr_log_level desired_level)
{
 fctx->logger->level = desired_level;
}





enum fbr_ev_type {
 FBR_EV_WATCHER = 1,
 FBR_EV_MUTEX,
 FBR_EV_COND_VAR,
};

struct fbr_ev_base;
typedef void (*fbr_destructor_func_t)(struct fbr_context *fctx, void *arg);
struct fbr_destructor {
 fbr_destructor_func_t func;
 void *arg;
 struct { struct fbr_destructor *tqe_next; struct fbr_destructor * *tqe_prev; } entries;
 int active;
};







struct fbr_id_tailq;

struct fbr_id_tailq_i {

 fbr_id_t id;
 struct fbr_ev_base *ev;
 struct { struct fbr_id_tailq_i *tqe_next; struct fbr_id_tailq_i * *tqe_prev; } entries;
 struct fbr_destructor dtor;
 struct fbr_id_tailq *head;
};

struct fbr_id_tailq { struct fbr_id_tailq_i *tqh_first; struct fbr_id_tailq_i * *tqh_last; };
struct fbr_ev_base {
/* Replacing type with type_ */
 enum fbr_ev_type type_;
 fbr_id_t id;
 int arrived;
 struct fbr_context *fctx;
 void *data;
 struct fbr_id_tailq_i item;
};
struct fbr_ev_watcher {
 ev_watcher *w;
 struct fbr_ev_base ev_base;
};
struct fbr_ev_mutex {
 struct fbr_mutex *mutex;
 struct fbr_ev_base ev_base;
};
struct fbr_ev_cond_var {
 struct fbr_cond_var *cond;

 struct fbr_mutex *mutex;
 struct fbr_ev_base ev_base;
};
struct fbr_mutex {
 fbr_id_t locked_by;
 struct fbr_id_tailq pending;
 struct { struct fbr_mutex *tqe_next; struct fbr_mutex * *tqe_prev; } entries;
};
struct fbr_cond_var {
 struct fbr_mutex *mutex;
 struct fbr_id_tailq waiting;
};

struct vrb;







struct fbr_buffer {
 struct vrb *vrb;
 size_t prepared_bytes;
 size_t waiting_bytes;
 struct fbr_cond_var committed_cond;
 struct fbr_mutex write_mutex;
 struct fbr_cond_var bytes_freed_cond;
 struct fbr_mutex read_mutex;
};
typedef unsigned int fbr_key_t;
void fbr_destructor_add(struct fbr_context *fctx, struct fbr_destructor *dtor);
void fbr_destructor_remove(struct fbr_context *fctx, struct fbr_destructor *dtor,
  int call);
static inline void fbr_destructor_init(struct fbr_destructor *dtor)
{
 memset(dtor, 0x00, sizeof(*dtor));
}
void fbr_ev_watcher_init(struct fbr_context *fctx, struct fbr_ev_watcher *ev, ev_watcher *w);
void fbr_ev_mutex_init(struct fbr_context *fctx, struct fbr_ev_mutex *ev,
  struct fbr_mutex *mutex);
void fbr_ev_cond_var_init(struct fbr_context *fctx, struct fbr_ev_cond_var *ev,
  struct fbr_cond_var *cond, struct fbr_mutex *mutex);
int fbr_ev_wait_one(struct fbr_context *fctx, struct fbr_ev_base *one);
int fbr_ev_wait(struct fbr_context *fctx, struct fbr_ev_base *events[]);
int fbr_ev_wait_to(struct fbr_context *fctx, struct fbr_ev_base *events[], ev_tstamp timeout);
int fbr_transfer(struct fbr_context *fctx, fbr_id_t to);
void fbr_init(struct fbr_context *fctx, struct ev_loop *loop);
void fbr_destroy(struct fbr_context *fctx);
void fbr_enable_backtraces(struct fbr_context *fctx, int enabled);







const char *fbr_strerror(struct fbr_context *fctx, enum fbr_error_code code);
void fbr_log_e(struct fbr_context *fctx, const char *format, ...)
 __attribute__ ((format (printf, 2, 3)));
void fbr_log_w(struct fbr_context *fctx, const char *format, ...)
 __attribute__ ((format (printf, 2, 3)));
void fbr_log_n(struct fbr_context *fctx, const char *format, ...)
 __attribute__ ((format (printf, 2, 3)));
void fbr_log_i(struct fbr_context *fctx, const char *format, ...)
 __attribute__ ((format (printf, 2, 3)));
void fbr_log_d(struct fbr_context *fctx, const char *format, ...)
 __attribute__ ((format (printf, 2, 3)));
fbr_id_t fbr_create(struct fbr_context *fctx, const char *name, fbr_fiber_func_t func, void *arg,
  size_t stack_size);
fbr_id_t fbr_create_foreign(struct fbr_context *fctx, const char *name);
int fbr_has_pending_events(struct fbr_context *fctx, fbr_id_t id);
int fbr_ev_wait_prepare(struct fbr_context *fctx, struct fbr_ev_base *events[]);
int fbr_ev_wait_finish(struct fbr_context *fctx, struct fbr_ev_base *events[]);
enum fbr_foreign_flag {
 FBR_FF_TRANSFER_PENDING = 1<<1,
 FBR_FF_RECLAIM_PENDING = 1<<2,
};
int fbr_foreign_get_flags(struct fbr_context *fctx, fbr_id_t id, enum fbr_foreign_flag *flags);
int fbr_foreign_set_flags(struct fbr_context *fctx, fbr_id_t id, enum fbr_foreign_flag flags);
fbr_id_t *fbr_foreign_get_transfer_pending(struct fbr_context *fctx, size_t *size);
int fbr_foreign_enter(struct fbr_context *fctx, fbr_id_t id);
int fbr_foreign_leave(struct fbr_context *fctx, fbr_id_t id);
static inline int fbr_is_foreign(__attribute__((unused)) struct fbr_context *fctx, fbr_id_t id)
{

 return 0 == (id >> 32);
}
struct fbr_ev_base *fbr_ev_watcher_base(struct fbr_ev_watcher *e);
fbr_id_t fbr_restart(struct fbr_context *fctx, fbr_id_t id);
const char *fbr_get_name(struct fbr_context *fctx, fbr_id_t id);
int fbr_set_name(struct fbr_context *fctx, fbr_id_t id, const char *name);
int fbr_disown(struct fbr_context *fctx, fbr_id_t parent);
fbr_id_t fbr_parent(struct fbr_context *fctx);
int fbr_reclaim(struct fbr_context *fctx, fbr_id_t fiber);

int fbr_set_reclaim(struct fbr_context *fctx, fbr_id_t fiber);
int fbr_set_noreclaim(struct fbr_context *fctx, fbr_id_t fiber);
int fbr_want_reclaim(struct fbr_context *fctx, fbr_id_t fiber);






int fbr_is_reclaimed(struct fbr_context *fctx, fbr_id_t fiber);





fbr_id_t fbr_self(struct fbr_context *fctx);






int fbr_key_create(struct fbr_context *fctx, fbr_key_t *key);





int fbr_key_delete(struct fbr_context *fctx, fbr_key_t key);





int fbr_key_set(struct fbr_context *fctx, fbr_id_t id, fbr_key_t key, void *value);





void *fbr_key_get(struct fbr_context *fctx, fbr_id_t id, fbr_key_t key);
void fbr_yield(struct fbr_context *fctx);
void fbr_cooperate(struct fbr_context *fctx);
void *fbr_alloc(struct fbr_context *fctx, size_t size);
void fbr_alloc_set_destructor(struct fbr_context *fctx, void *ptr, fbr_alloc_destructor_func_t func,
  void *context);
void *fbr_calloc(struct fbr_context *fctx, unsigned int nmemb, size_t size);
void fbr_free(struct fbr_context *fctx, void *ptr);
void fbr_free_nd(struct fbr_context *fctx, void *ptr);
int fbr_fd_nonblock(struct fbr_context *fctx, int fd);
ssize_t fbr_read(struct fbr_context *fctx, int fd, void *buf, size_t count);
ssize_t fbr_read_all(struct fbr_context *fctx, int fd, void *buf, size_t count);
ssize_t fbr_readline(struct fbr_context *fctx, int fd, void *buffer, size_t n);
ssize_t fbr_write(struct fbr_context *fctx, int fd, const void *buf, size_t count);
ssize_t fbr_write_all(struct fbr_context *fctx, int fd, const void *buf, size_t count);
ssize_t fbr_recvfrom(struct fbr_context *fctx, int sockfd, void *buf, size_t len, int flags,
  struct sockaddr *src_addr, socklen_t *addrlen);
ssize_t fbr_sendto(struct fbr_context *fctx, int sockfd, const void *buf, size_t len, int flags, const
  struct sockaddr *dest_addr, socklen_t addrlen);
int fbr_accept(struct fbr_context *fctx, int sockfd, struct sockaddr *addr, socklen_t *addrlen);
ev_tstamp fbr_sleep(struct fbr_context *fctx, ev_tstamp seconds);






void fbr_dump_stack(struct fbr_context *fctx, fbr_logutil_func_t log);
void fbr_mutex_init(struct fbr_context *fctx, struct fbr_mutex *mutex);
void fbr_mutex_lock(struct fbr_context *fctx, struct fbr_mutex *mutex);
int fbr_mutex_trylock(struct fbr_context *fctx, struct fbr_mutex *mutex);
void fbr_mutex_unlock(struct fbr_context *fctx, struct fbr_mutex *mutex);
void fbr_mutex_destroy(struct fbr_context *fctx, struct fbr_mutex *mutex);
void fbr_cond_init(struct fbr_context *fctx, struct fbr_cond_var *cond);
void fbr_cond_destroy(struct fbr_context *fctx, struct fbr_cond_var *cond);
int fbr_cond_wait(struct fbr_context *fctx, struct fbr_cond_var *cond, struct fbr_mutex *mutex);
void fbr_cond_broadcast(struct fbr_context *fctx, struct fbr_cond_var *cond);
void fbr_cond_signal(struct fbr_context *fctx, struct fbr_cond_var *cond);
int fbr_buffer_init(struct fbr_context *fctx, struct fbr_buffer *buffer, size_t size);
void fbr_buffer_destroy(struct fbr_context *fctx, struct fbr_buffer *buffer);
void *fbr_buffer_alloc_prepare(struct fbr_context *fctx, struct fbr_buffer *buffer, size_t size);
void fbr_buffer_alloc_commit(struct fbr_context *fctx, struct fbr_buffer *buffer);
void fbr_buffer_alloc_abort(struct fbr_context *fctx, struct fbr_buffer *buffer);
void *fbr_buffer_read_address(struct fbr_context *fctx, struct fbr_buffer *buffer, size_t size);
void fbr_buffer_read_advance(struct fbr_context *fctx, struct fbr_buffer *buffer);
void fbr_buffer_read_discard(struct fbr_context *fctx, struct fbr_buffer *buffer);
size_t fbr_buffer_bytes(struct fbr_context *fctx, struct fbr_buffer *buffer);
size_t fbr_buffer_free_bytes(struct fbr_context *fctx, struct fbr_buffer *buffer);

size_t fbr_buffer_size(struct fbr_context *fctx, struct fbr_buffer *buffer);
int fbr_buffer_resize(struct fbr_context *fctx, struct fbr_buffer *buffer, size_t size);






static inline struct fbr_cond_var *fbr_buffer_cond_read(__attribute__((unused)) struct fbr_context *fctx,
  struct fbr_buffer *buffer)
{
 return &buffer->committed_cond;
}






static inline struct fbr_cond_var *fbr_buffer_cond_write(__attribute__((unused)) struct fbr_context *fctx,
  struct fbr_buffer *buffer)
{
 return &buffer->bytes_freed_cond;
}
static inline int fbr_buffer_wait_read(struct fbr_context *fctx, struct fbr_buffer *buffer,
  size_t size)
{
 struct fbr_mutex mutex;
 int retval;
 fbr_mutex_init(fctx, &mutex);
 fbr_mutex_lock(fctx, &mutex);
 while (fbr_buffer_bytes(fctx, buffer) < size) {
  retval = fbr_cond_wait(fctx, &buffer->committed_cond, &mutex);
  ((0 == retval) ? (void) (0) : __assert_fail ("0 == retval", "/home/kolkhovskiy/git/libevfibers/include/evfibers/fiber.h", 1560, __PRETTY_FUNCTION__));
 }
 fbr_mutex_unlock(fctx, &mutex);
 fbr_mutex_destroy(fctx, &mutex);
 return 1;
}
static inline int fbr_buffer_can_read(struct fbr_context *fctx, struct fbr_buffer *buffer,
  size_t size)
{
 return fbr_buffer_bytes(fctx, buffer) >= size;
}
static inline int fbr_buffer_wait_write(struct fbr_context *fctx, struct fbr_buffer *buffer,
  size_t size)
{
 struct fbr_mutex mutex;
 int retval;
 fbr_mutex_init(fctx, &mutex);
 fbr_mutex_lock(fctx, &mutex);
 while (fbr_buffer_free_bytes(fctx, buffer) < size) {
  retval = fbr_cond_wait(fctx, &buffer->bytes_freed_cond,
    &mutex);
  ((0 == retval) ? (void) (0) : __assert_fail ("0 == retval", "/home/kolkhovskiy/git/libevfibers/include/evfibers/fiber.h", 1601, __PRETTY_FUNCTION__));
 }
 fbr_mutex_unlock(fctx, &mutex);
 fbr_mutex_destroy(fctx, &mutex);
 return 1;
}
static inline int fbr_buffer_can_write(struct fbr_context *fctx, struct fbr_buffer *buffer,
  size_t size)
{
 return fbr_buffer_free_bytes(fctx, buffer) >= size;
}
void *fbr_get_user_data(struct fbr_context *fctx, fbr_id_t id);
int fbr_set_user_data(struct fbr_context *fctx, fbr_id_t id, void *data);

struct fbr_async *fbr_async_create(struct fbr_context *fctx);
void fbr_async_destroy(struct fbr_context *fctx, struct fbr_async *async);
int fbr_async_fopen(struct fbr_context *fctx, struct fbr_async *async, const char *filename,
  const char *mode);
int fbr_async_fclose(struct fbr_context *fctx, struct fbr_async *async);
int fbr_async_fread(struct fbr_context *fctx, struct fbr_async *async, void *buf, size_t size);
int fbr_async_fwrite(struct fbr_context *fctx, struct fbr_async *async, void *buf,
  size_t size);
int fbr_async_fseek(struct fbr_context *fctx, struct fbr_async *async, size_t offset, int whence);
ssize_t fbr_async_ftell(struct fbr_context *fctx, struct fbr_async *async);
int fbr_async_fflush(struct fbr_context *fctx, struct fbr_async *async);
int fbr_async_ftruncate(struct fbr_context *fctx, struct fbr_async *async, size_t size);
int fbr_async_fsync(struct fbr_context *fctx, struct fbr_async *async);
int fbr_async_fdatasync(struct fbr_context *fctx, struct fbr_async *async);
]]
