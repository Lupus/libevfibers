local ffi = require("ffi")
local so = ffi.load("ev")
require("fiber_h")

local loop

local ev_loop_t = ffi.typeof('struct ev_loop')
local ev_loop_mti = {}

function ev_loop_mti:run(flags)
	so.ev_run(self, flags)
end

function ev_loop_mti:now()
	return so.ev_now(self)
end

function ev_loop_mti:now_update()
	so.ev_now_update(self)
end

function ev_loop_mti:timer_start(timer)
	so.ev_timer_start(self, timer)
end

function ev_loop_mti:timer_stop(timer)
	so.ev_timer_stop(self, timer)
end

function ev_loop_mti:timer_again(timer)
	so.ev_timer_again(self, timer)
end

function ev_loop_mti:timer_remaining(timer)
	return so.ev_timer_remaining(self, ev_timer)
end

ffi.metatype(ev_loop_t, {__index = ev_loop_mti})

ev = {
	EV_UNDEF          = so.EV_UNDEF,
	EV_NONE           = so.EV_NONE,
	EV_READ           = so.EV_READ,
	EV_WRITE          = so.EV_WRITE,
	EV__IOFDSET       = so.EV__IOFDSET,
	EV_IO             = so.EV_IO,
	EV_TIMER          = so.EV_TIMER,
	EV_TIMEOUT        = so.EV_TIMEOUT,
	EV_PERIODIC       = so.EV_PERIODIC,
	EV_SIGNAL         = so.EV_SIGNAL,
	EV_CHILD          = so.EV_CHILD,
	EV_STAT           = so.EV_STAT,
	EV_IDLE           = so.EV_IDLE,
	EV_PREPARE        = so.EV_PREPARE,
	EV_CHECK          = so.EV_CHECK,
	EV_EMBED          = so.EV_EMBED,
	EV_FORK           = so.EV_FORK,
	EV_CLEANUP        = so.EV_CLEANUP,
	EV_ASYNC          = so.EV_ASYNC,
	EV_CUSTOM         = so.EV_CUSTOM,
	EV_ERROR          = so.EV_ERROR,
	EVFLAG_AUTO       = so.EVFLAG_AUTO,
	EVFLAG_NOENV      = so.EVFLAG_NOENV,
	EVFLAG_FORKCHECK  = so.EVFLAG_FORKCHECK,
	EVFLAG_NOINOTIFY  = so.EVFLAG_NOINOTIFY,
	EVFLAG_NOSIGFD    = so.EVFLAG_NOSIGFD,
	EVFLAG_SIGNALFD   = so.EVFLAG_SIGNALFD,
	EVFLAG_NOSIGMASK  = so.EVFLAG_NOSIGMASK,
	EVBACKEND_SELECT  = so.EVBACKEND_SELECT,
	EVBACKEND_POLL    = so.EVBACKEND_POLL,
	EVBACKEND_EPOLL   = so.EVBACKEND_EPOLL,
	EVBACKEND_KQUEUE  = so.EVBACKEND_KQUEUE,
	EVBACKEND_DEVPOLL = so.EVBACKEND_DEVPOLL,
	EVBACKEND_PORT    = so.EVBACKEND_PORT,
	EVBACKEND_ALL     = so.EVBACKEND_ALL,
	EVBACKEND_MASK    = so.EVBACKEND_MASK,
	EVRUN_NOWAIT      = so.EVRUN_NOWAIT,
	EVRUN_ONCE        = so.EVRUN_ONCE,
	EVBREAK_CANCEL    = so.EVBREAK_CANCEL,
	EVBREAK_ONE       = so.EVBREAK_ONE,
	EVBREAK_ALL       = so.EVBREAK_ALL,
}

function ev.version()
	return so.ev_version_major(), so.ev_version_minor()
end

function ev.default_loop(flags)
	if nil == flags then
		flags = 0
	end
	return so.ev_default_loop(flags)
end

return ev
