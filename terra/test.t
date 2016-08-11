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


terra test_one(i: int)
	var loop = ev.Loop.alloc()
	var f = fbr.Context.alloc()
end

terra basic_tc()
	var tc = check.TCase.alloc("bacis")
	tc:add_test(test_one)
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

--[[

terralib.saveobj("run_tests", "executable", {
	main = run_tests
}, {"-lcheck", "-lm", "-lrt", "-lev", "-pthread"})

]]
