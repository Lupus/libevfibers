--[[

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

   ]]

-- vim: set syntax=terra:

local S = require("std")
local talloc = require("talloc")
local golike = require("golike")
local util = require("util")
local inspect = require("inspect")

terralib.includepath = terralib.includepath .. ";../include;../build/include"

local C = terralib.includecstring[[

#include <ev.h>

int wrap_ev_is_active(ev_watcher *w) {
	return ev_is_active(w);
}

int wrap_ev_is_pending(ev_watcher *w) {
	return ev_is_pending(w);
}

#define SPECIALIZE_EV_INIT(_type_)                                            \
void wrap_ev_##_type_##_init(ev_##_type_ *w, void (*cb)(struct ev_loop *loop, \
		struct ev_##_type_ *w, int revents)) {                        \
	ev_init(w, cb);                                                       \
}

SPECIALIZE_EV_INIT(io)
SPECIALIZE_EV_INIT(timer)
SPECIALIZE_EV_INIT(async)

void wrap_ev_io_set (ev_io *w, int fd, int events) {
	ev_io_set(w, fd, events);
}

void wrap_ev_timer_set (ev_timer *w, ev_tstamp after, ev_tstamp repeat) {
	ev_timer_set(w, after, repeat);
}

int wrap_ev_async_pending(ev_async *w) {
	return ev_async_pending(w);
}

]]

terralib.linklibrary("libev.so")

local M = {}


M.UNDEF     = C.EV_UNDEF
M.NONE      = C.EV_NONE
M.READ      = C.EV_READ
M.WRITE     = C.EV_WRITE
M.TIMER     = C.EV_TIMER
M.PERIODIC  = C.EV_PERIODIC
M.SIGNAL    = C.EV_SIGNAL
M.CHILD     = C.EV_CHILD
M.STAT      = C.EV_STAT
M.IDLE      = C.EV_IDLE
M.CHECK     = C.EV_CHECK
M.PREPARE   = C.EV_PREPARE
M.FORK      = C.EV_FORK
M.ASYNC     = C.EV_ASYNC
M.EMBED     = C.EV_EMBED
M.ERROR     = C.EV_ERROR

M.AUTO      = C.EVFLAG_AUTO
M.NOENV     = C.EVFLAG_NOENV
M.FORKCHECK = C.EVFLAG_FORKCHECK

M.SELECT    = C.EVBACKEND_SELECT
M.POLL      = C.EVBACKEND_POLL
M.EPOLL     = C.EVBACKEND_EPOLL
M.KQUEUE    = C.EVBACKEND_KQUEUE
M.DEVPOLL   = C.EVBACKEND_DEVPOLL
M.PORT      = C.EVBACKEND_PORT

M.NOWAIT    = C.EVRUN_NOWAIT
M.ONCE      = C.EVRUN_ONCE

M.how = {
	ONE = C.EVBREAK_ONE,
	ALL = C.EVBREAK_ALL,
}


local struct Loop(talloc.Object) {
	loop: &C.ev_loop
}

M.Loop = Loop

Loop.methods.__init = terralib.overloadedfunction("__init")
Loop.methods.__init:adddefinition(terra(self: &Loop)
	self.loop = C.ev_default_loop(M.AUTO)
end)
Loop.methods.__init:adddefinition(terra(self: &Loop, loop: &C.ev_loop)
	self.loop = loop
end)

Loop.metamethods.__eq = terra(a: Loop, b: Loop)
	return a.loop == b.loop
end

Loop.metamethods.__ne = terra(a: Loop, b: Loop)
	return a.loop ~= b.loop
end

Loop.metamethods.__cast = function(from,to,exp)
	if to:ispointer() and to.type == C.ev_loop then
		return `exp.loop
	end
	error(("invalid cast from %s to %s"):format(from, to))
end

terra Loop:__destruct()
end

terra Loop:is_default()
	return self.loop == C.ev_default_loop(M.AUTO)
end

Loop.methods.run = terralib.overloadedfunction("run")
Loop.methods.run:adddefinition(terra(self: &Loop)
	C.ev_run(self.loop, 0)
end)
Loop.methods.run:adddefinition(terra(self: &Loop, flags: int)
	C.ev_run(self.loop, flags)
end)

Loop.methods.break_ = terralib.overloadedfunction("break_")
Loop.methods.break_:adddefinition(terra(self: &Loop)
	C.ev_break(self.loop, M.how.ONE)
end)
Loop.methods.break_:adddefinition(terra(self: &Loop, how: int)
	C.ev_break(self.loop, how)
end)

terra Loop:post_fork()
	C.ev_loop_fork(self.loop)
end

terra Loop:backend()
	return C.ev_backend(self.loop)
end

terra Loop:now()
	return C.ev_now(self.loop)
end

terra Loop:ref()
	C.ev_ref(self.loop)
end

terra Loop:unref()
	C.ev_unref(self.loop)
end

terra Loop:feed_fd_event(fd: int, revents: int)
	C.ev_feed_fd_event(self.loop, fd, revents)
end

terra Loop:feed_signal_event(signum: int)
	C.ev_feed_signal_event(self.loop, signum)
end

talloc.complete_type(Loop)

local function gen_watcher(name)
	local w_type = C["ev_"..name]
	local watcher_impl = terralib.types.newstruct("WatcherImpl_"..name)
	watcher_impl.entries:insert({ field = "base", type = w_type })
	watcher_impl.entries:insert({ field = "loop", type = &Loop })
	local watcher_listener_iface = golike.Interface({
		on_event = {&watcher_impl, int} -> {}
	})
	watcher_impl.entries:insert({
		field = "listener",
		type = watcher_listener_iface
	})
	talloc.Object(watcher_impl)
	watcher_impl.metamethods.__cast = function(from,to,exp)
		if to:ispointer() and to.type == w_type then
			return `&exp.base
		elseif to:ispointer() and to.type == C.ev_watcher then
			return `[&C.ev_watcher](&exp.base)
		end
		error(("invalid cast from %s to %s"):format(from, to))
	end

	local terra libev_cb(loop: &C.ev_loop, w: &w_type, revents: int)
		var wi = [&watcher_impl](w.data)
		wi.listener:on_event(wi, revents)
	end

	terra watcher_impl:__init(loop: &Loop)
		self.loop = loop
		self.base.data = [&uint8](self)
		[C["wrap_ev_"..name.."_init"]](self, libev_cb)
	end
	terra watcher_impl:__destruct()
		self:stop()
	end

	terra watcher_impl:set_listener(l: watcher_listener_iface)
		self.listener = l
	end
	terra watcher_impl:is_active()
		return 1 == C.wrap_ev_is_active(self)
	end
	terra watcher_impl:is_pending()
		return 1 == C.wrap_ev_is_pending(self)
	end
	terra watcher_impl:feed_event(revents: int)
		C.ev_feed_event(self.loop, self, revents)
	end
	terra watcher_impl:start()
		[C["ev_"..name.."_start"]](self.loop, self)
	end
	terra watcher_impl:stop()
		[C["ev_"..name.."_stop"]](self.loop, self)
	end
	terra watcher_impl:feed(revents: int)
		C.ev_feed_event(self.loop, self, revents)
	end
	talloc.complete_type(watcher_impl)
	return watcher_impl
end

local wrap_active = function(fn)
	local args = {}
	for i=1,#fn.type.parameters do
		local s = symbol(fn.type.parameters[i])
		table.insert(args, s)
	end
	local self = args[1]
	return terra([args])
		var was_active = self:is_active()
		if was_active then self:stop() end
		var ret = fn([args])
		if was_active then self:start() end
		return ret
	end
end

local overload_start = function(watcher_impl)
	local old_start = watcher_impl.methods.start
	local set = watcher_impl.methods.set
	watcher_impl.methods.start = terralib.overloadedfunction("start")
	watcher_impl.methods.start:adddefinition(old_start)
	local args = {}
	for i=1,#set.type.parameters do
		local s = symbol(set.type.parameters[i])
		table.insert(args, s)
	end
	local self = args[1]
	watcher_impl.methods.start:adddefinition(terra([args])
		set([args])
		old_start(self)
	end)
end

local function gen_set_impl(wtype, impl)
	wtype.methods.set = wrap_active(impl)
	overload_start(wtype)
end

M.IO = gen_watcher("io")
gen_set_impl(M.IO, terra(self: &M.IO, fd: int, events: int)
	C.wrap_ev_io_set(&self.base, fd, events)
end)

M.Timer = gen_watcher("timer")
gen_set_impl(M.Timer, terra(self: &M.Timer, after: C.ev_tstamp,
		repeat_: C.ev_tstamp)
	C.wrap_ev_timer_set(&self.base, after, repeat_)
end)
terra M.Timer:again()
	return C.ev_timer_again(self.loop, &self.base)
end
terra M.Timer:remaining()
	return C.ev_timer_remaining(self.loop, &self.base)
end

M.Async = gen_watcher("async")
terra M.Async:send()
	return C.ev_async_send(self.loop, &self.base)
end
terra M.Async:async_pending()
	return 1 == C.wrap_ev_async_pending(&self.base)
end

function M.is_watcher(what)
	if what:ispointer() then
		what = what.type
	end
	return what == M.IO or
		what == M.Timer or
		what == M.Async
end

return M
