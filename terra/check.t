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
local inspect = require("inspect")

local C = terralib.includecstring[[

#include <check.h>

]]

terralib.linklibrary("./libcheck.so")

local M = {}

M.SILENT  = C.CK_SILENT
M.MINIMAL = C.CK_MINIMAL
M.NORMAL  = C.CK_NORMAL
M.VERBOSE = C.CK_VERBOSE
M.ENV     = C.CK_ENV

local function trim(s)
	return (s:gsub("^%s*(.-)%s*$", "%1"))
end

local function assert_msg_(expr, ...)
	local to_bool
	if expr.tree.type:ispointer() then
		to_bool = `expr ~= nil
	elseif expr.tree.type == niltype then
		to_bool = `false
	else
		to_bool = `expr
	end
	local res = quote
		var result: int
		if to_bool then
			result = 1
		else
			result = 0
		end
	in
		result
	end
	local args = {
		res,
		expr.tree.filename,
		expr.tree.linenumber,
		"Assertion '" .. trim(tostring(expr)) .. "' failed",
	}
	for _,v in ipairs({...}) do
		table.insert(args, v)
	end
	table.insert(args, `nil)
	return quote
		C._ck_assert_msg(args)
		C._mark_point([expr.tree.filename], [expr.tree.linenumber])
	end
end

M.assert_msg = macro(assert_msg_)

M.assert = macro(function(expr)
	return assert_msg_(expr, nil)
end)

M.abort = macro(function()
	return assert_msg_(`false, nil)
end)

M.abort_msg = macro(function(...)
	return assert_msg_(`false, ...)
end)

local struct TCase(S.Object) {
	tcase: &C.TCase
}

M.TCase = TCase

do
	local old_alloc = TCase.methods.alloc
	TCase.methods.alloc = terra(name: &int8)
		var tc = old_alloc()
		tc.tcase = C.tcase_create(name)
		return tc
	end
end

TCase.methods.add_test = macro(function(self, tf)
	local f = terra(i: int)
		C.tcase_fn_start([tf.tree.name], [tf.tree.filename],
				[tf.tree.linenumber])
		tf(i)
	end
	return quote
		C._tcase_add_test(self.tcase, f, [tf.tree.name], 0, 0, 0, 1)
	end
end)

local struct Suite(S.Object) {
	suite: &C.Suite
}

M.Suite = Suite

do
	local old_alloc = Suite.methods.alloc
	Suite.methods.alloc = terra(name: &int8)
		var s = old_alloc()
		s.suite = C.suite_create(name)
		return s
	end
end

terra Suite:add_tcase(tc: &TCase)
	C.suite_add_tcase(self.suite, tc.tcase)
end

local struct SRunner(S.Object) {
	srunner: &C.SRunner
}

M.SRunner = SRunner

do
	local old_alloc = SRunner.methods.alloc
	SRunner.methods.alloc = terra(suite: &Suite)
		var sr = old_alloc()
		sr.srunner = C.srunner_create(suite.suite)
		return sr
	end
end

SRunner.methods.run_all = terralib.overloadedfunction("run_all")
SRunner.methods.run_all:adddefinition(terra(self: &SRunner)
	C.srunner_run_all(self.srunner, M.NORMAL)
end)
SRunner.methods.run_all:adddefinition(terra(self: &SRunner, print_mode: int)
	C.srunner_run_all(self.srunner, print_mode)
end)

return M
