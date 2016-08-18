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

local m = global(&fbr.Mutex)

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

terra test_one(i: int)
	var loop = ev.Loop.salloc()
	var fctx = fbr.Context.salloc(loop)
	fctx:set_log_level(fbr.LOG_DEBUG)
	m = fbr.Mutex.salloc(fctx)
	var id1 = fctx:create("my fiber", fbr.simple_fiber(fiber_1))
	fctx:transfer(id1)
	var id2 = fctx:create("my fiber 2", fbr.simple_fiber(fiber_2))
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

terra test_wait(i: int)
	var loop = ev.Loop.salloc()
	var fctx = fbr.Context.salloc(loop)
	fctx:set_log_level(fbr.LOG_DEBUG)
	m = fbr.Mutex.salloc(fctx)
	var id1 = fctx:create("my fiber", fbr.simple_fiber(fiber_3))
	fctx:transfer(id1)
	loop:run()
end

terra basic_tc()
	var tc = check.TCase.alloc("bacis")
	tc:add_test(test_one)
	tc:add_test(test_wait)
	return tc
end

terra evfibers_suite()
	var suite = check.Suite.alloc("evfibers-terra")
	suite:add_tcase([require("tests.util").tcase]())
	suite:add_tcase([require("tests.ev").tcase]())
	suite:add_tcase(basic_tc())
	return suite
end

terra run_tests()
	var suite = evfibers_suite()
	var srunner = check.SRunner.alloc(suite)
	srunner:run_all()
end

run_tests()

terralib.saveobj("run_tests", "executable", {
	main = run_tests
}, {
	"-lcheck", "-lm", "-lrt", "-lev",
	"-L../build",
	"-Wl,-rpath", "-Wl,../build",
	"-levfibers",
	"-pthread"
})
