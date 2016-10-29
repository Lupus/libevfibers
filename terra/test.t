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
local talloc = require("talloc")
local ev = require("ev")
local fbr = require("evfibers")

local m = global(&fbr.Mutex)
local c1 = global(&fbr.CondVar)
local c2 = global(&fbr.CondVar)
local c3 = global(&fbr.CondVar)

terra fiber_1(fctx: &fbr.Context)
	fctx:log_d("hello from fiber 1!")
	m:lock()
	defer m:unlock()
	fctx:sleep(1.0)

	fctx:log_d("hello from fiber 1!")
	return
end

terra fiber_2(fctx: &fbr.Context)
	fctx:log_d("hello from fiber 2!")
	m:lock()
	defer m:unlock()
	fctx:sleep(1.0)

	fctx:log_d("hello from fiber 2!")
	return
end

terra test_one(i: int, ctx: &opaque)
	var loop = ev.Loop.talloc(ctx)
	var fctx = fbr.Context.talloc(ctx, loop)
	fctx:set_log_level(fbr.LOG_DEBUG)
	m = fbr.Mutex.talloc(ctx, fctx)
	var id1 = fctx:create("my fiber", fbr.simple_fiber(ctx, fiber_1))
	fctx:transfer(id1)
	var id2 = fctx:create("my fiber 2", fbr.simple_fiber(ctx, fiber_2))
	fctx:transfer(id2)
	loop:run()
end

terra fiber_3(fctx: &fbr.Context)
	fctx:log_d("hello from fiber 3!")
	var w = fctx:ev_wait(m)
	var nevents, err = w:wait()
	check.assert(err == nil)
	check.assert(nevents == 1)
	check.assert(w:arrived(m) == true)
	defer m:unlock()
	fctx:sleep(1.0)

	fctx:log_d("hello from fiber 3!")
	return
end

terra test_wait(i: int, ctx: &opaque)
	var loop = ev.Loop.talloc(ctx)
	var fctx = fbr.Context.talloc(ctx, loop)
	fctx:set_log_level(fbr.LOG_DEBUG)
	m = fbr.Mutex.talloc(ctx, fctx)
	var id1 = fctx:create("my fiber", fbr.simple_fiber(ctx, fiber_3))
	fctx:transfer(id1)
	loop:run()
end

terra fiber_4(fctx: &fbr.Context)
	fctx:log_d("hello from fiber 4!")
	var w = fctx:ev_wait(m)
	var nevents, err = w:wait()
	check.assert(err == nil)
	check.assert(nevents == 1)
	check.assert(w:arrived(m) == true)
	defer m:unlock()
	fctx:sleep(1.0)

	fctx:log_d("hello from fiber 4!")
	return
end

local narrived = global(int)

terra fiber_5(fctx: &fbr.Context)
	fctx:log_d("waiting for events")
	var w = fctx:ev_wait(c1, c2, c3)
	var nevents, err = w:wait()
	check.assert(err == nil)
	check.assert(nevents == 3)
	check.assert(w:arrived(c1) == true)
	check.assert(w:arrived(c2) == true)
	check.assert(w:arrived(c3) == true)

	fctx:log_d("all events arrived")
	narrived = narrived + 1
end

terra fiber_6(fctx: &fbr.Context)
	fctx:log_d("hello from fiber 6!")
	fctx:sleep(1.0)
	c1:broadcast()
	c2:broadcast()
	c3:broadcast()
	fctx:log_d("hello from fiber 6!")
end

terra test_wait2(i: int, ctx: &opaque)
	var loop = ev.Loop.talloc(ctx)
	var fctx = fbr.Context.talloc(ctx, loop)
	fctx:set_log_level(fbr.LOG_DEBUG)
	c1 = fbr.CondVar.talloc(ctx, fctx)
	c2 = fbr.CondVar.talloc(ctx, fctx)
	c3 = fbr.CondVar.talloc(ctx, fctx)

	var id1 = fctx:create("my fiber a", fbr.simple_fiber(ctx, fiber_5))
	var id2 = fctx:create("my fiber b", fbr.simple_fiber(ctx, fiber_5))
	var id3 = fctx:create("my fiber c", fbr.simple_fiber(ctx, fiber_5))

	narrived = 0

	fctx:transfer(id3)
	fctx:transfer(id1)
	fctx:transfer(id2)

	var id4 = fctx:create("my fiber 2", fbr.simple_fiber(ctx, fiber_6))
	fctx:transfer(id4)
	loop:run()

	check.assert(narrived == 3)
end

local tm = global(&ev.Timer)

terra fiber_7(fctx: &fbr.Context)
	fctx:log_d("waiting for events")
	var w = fctx:ev_wait(tm)
	var nevents, err = w:wait()
	check.assert(err == nil)
	check.assert(nevents == 1)
	check.assert(w:arrived(tm) == true)

	fctx:log_d("all events arrived")
	narrived = narrived + 1
end

terra test_wait3(i: int, ctx: &opaque)
	var loop = ev.Loop.talloc(ctx)
	var fctx = fbr.Context.talloc(ctx, loop)
	fctx:set_log_level(fbr.LOG_DEBUG)
	tm = ev.Timer.talloc(ctx, loop)
	tm:start(1.0, 0.0)

	narrived = 0

	var id1 = fctx:create("my fiber", fbr.simple_fiber(ctx, fiber_7))
	fctx:transfer(id1)

	loop:run()

	check.assert(narrived == 1)
end

local struct SampleFiber(fbr.Fiber) {
	i: int
}

terra SampleFiber:__init(i: int)
	self.i = i
end

terra SampleFiber:run(fctx: &fbr.Context)
	self.i = self.i + 1
	self:yield()
	self.i = self.i + 1
	self:yield()
	self.i = self.i + 1
	self:yield()
end

terra test_fiber_mt(i: int, ctx: &opaque)
	var loop = ev.Loop.talloc(ctx)
	var fctx = fbr.Context.talloc(ctx, loop)
	var sf = SampleFiber.create(fctx, ctx, 10)
	sf:transfer()
	sf:transfer()
	sf:transfer()

	check.assert(sf.i == 13)
end

local twrap = macro(function(test_fn)
	return terra(i: int)
		var ctx = talloc.new(nil)
		test_fn(i, ctx)
		ctx:free()
	end
end)

terra basic_tc()
	var tc = check.TCase.alloc("bacis")
	tc:add_test(twrap(test_one))
	tc:add_test(twrap(test_wait))
	tc:add_test(twrap(test_wait2))
	tc:add_test(twrap(test_wait3))
	tc:add_test(twrap(test_fiber_mt))
	return tc
end

terra evfibers_suite()
	var suite = check.Suite.alloc("evfibers-terra")
	suite:add_tcase([require("tests.talloc").tcase]())
	suite:add_tcase([require("tests.async").tcase]())
	suite:add_tcase([require("tests.fds").tcase]())
	suite:add_tcase([require("tests.util").tcase]())
	suite:add_tcase([require("tests.ev").tcase]())
	suite:add_tcase([require("tests.containers").tcase]())
	suite:add_tcase(basic_tc())
	return suite
end

terra run_tests()
	fbr.static_init()

	var suite = evfibers_suite()
	var srunner = check.SRunner.alloc(suite)
	srunner:run_all()
	srunner:delete()
	return 0
end

terralib.saveobj("run_tests", "executable", {
	main = run_tests
}, {
	"-lcheck", "-lm", "-lrt", "-lev",
	"-L../build",
	"-Wl,-rpath", "-Wl,../build",
	"-levfibers",
	"-ltalloc",
	"-lunwind",
	"-pthread"
})

run_tests()
