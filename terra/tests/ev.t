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

local ffi = require("ffi")
local S = require("std")
local check = require("check")
local ev = require("ev")
local fbr = require("evfibers")

local C = terralib.includecstring[[

#include <sys/types.h> 
#include <sys/socket.h>
#include <unistd.h>

]]

terra test_default_loop(i: int)
	var loop = ev.Loop.alloc()
	check.assert_msg(loop)
	check.assert(loop.loop)
end

local struct CountingListener(S.Object) {
	count: int
	last_revents: int
}

terra CountingListener:on_event(io: &ev.IO, revents: int)
	self.count = self.count + 1
	self.last_revents = revents
end

terra test_io_watcher_feed(i: int)
	var loop = ev.Loop.salloc()
	
	var fd = C.socket(C.PF_INET, C.SOCK_STREAM, 0)
	check.assert(fd >= 0)
	defer C.close(fd)

	var w = ev.IO.salloc(loop)
	w:set(fd, ev.READ)
	var cl = CountingListener { 0 }
	w:set_listener(&cl)
	w:start()
	loop:feed_fd_event(fd, ev.READ)
	loop:run(ev.ONCE)
	w:stop()
	check.assert(cl.count == 2)
end

terra test_io_watcher(i: int)
	var loop = ev.Loop.alloc()
	var w = ev.IO.alloc(loop)
	check.assert(w)
	w:set(0, ev.READ)
	w:start()
	w:stop()
	w:delete()
	loop:delete()
end

terra test_io_watcher_salloc(i: int)
	var loop = ev.Loop.salloc()
	var w = ev.IO.salloc(loop)
	check.assert(w)
	check.assert(w.loop == loop)
	w:set(0, ev.READ)
	w:start()
	w:stop()
end

terra test_timer_watcher_feed(i: int)
	var loop = ev.Loop.salloc()
	
	var w = ev.Timer.salloc(loop)
	w:set(2.0, 0.0)
	var cl = CountingListener { 0 }
	w:set_listener(&cl)
	w:start(1.0, 0.0)
	w:feed_event(0)
	loop:run(ev.ONCE)
	check.assert(w:remaining() < 0)
	w:stop()
	check.assert(cl.count == 2)
end

terra test_async_send(i: int)
	var loop = ev.Loop.salloc()
	
	var w = ev.Async.salloc(loop)
	var cl = CountingListener { 0 }
	w:set_listener(&cl)
	w:start()
	check.assert(w:async_pending() == false)
	w:send()
	check.assert(w:async_pending() == true)
	loop:run(ev.ONCE)
	w:stop()
	check.assert(cl.count == 1)
end

return {
	tcase = terra()
		var tc = check.TCase.alloc("ev-basic")
		tc:add_test(test_default_loop)
		tc:add_test(test_io_watcher)
		tc:add_test(test_io_watcher_salloc)
		tc:add_test(test_io_watcher_feed)
		tc:add_test(test_timer_watcher_feed)
		tc:add_test(test_async_send)
		return tc
	end
}
