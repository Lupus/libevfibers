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
local ev = require("ev")
local fbr = require("evfibers")

local C = terralib.includecstring[[

#include <evfibers/fiber.h>
#include <evfibers/eio.h>

]]

local M = {}

local TestFunc = {&ev.Loop, &fbr.Context, int, &opaque} -> {}

local struct TestRunnerFiber(fbr.Fiber) {
	loop: &ev.Loop
	i: int
	fn: TestFunc
}

terra TestRunnerFiber:__init(loop: &ev.Loop, i: int, fn: TestFunc)
	self.loop = loop
	self.i = i
	self.fn = fn
end

terra TestRunnerFiber:run(fctx: &fbr.Context)
	fctx:sleep(0)
	fctx:log_d("in fiber!")
	self.fn(self.loop, fctx, self.i, self)
end

M.fiber_wrap = macro(function(test_fn)
	return terra(i: int)
		var ctx = talloc.new(nil)
		var loop = ev.Loop.talloc(ctx)
		var fctx = fbr.Context.talloc(ctx, loop)
		fctx:set_log_level(fbr.LOG_DEBUG)
		TestRunnerFiber.create(fctx, ctx, loop, i, test_fn):transfer()
		loop:run()
		ctx:free()
	end
end)

M.basic_wrap = macro(function(test_fn)
	return terra(i: int)
		var ctx = talloc.new(nil)
		test_fn(i, ctx)
		ctx:free()
	end
end)

return M
