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
local check = require("check")
local twraps = require("twraps")
local talloc = require("talloc")
local ev = require("ev")
local fbr = require("evfibers")
local async = require("async")

local C = terralib.includecstring[[
#include <unistd.h>
]]

terra test_invoke(loop: &ev.Loop, fctx: &fbr.Context, i: int, ctx: &opaque)
	var t1 = loop:now()
	var retval = async.invoke(fctx, C.sleep, 1)
	var t2 = loop:now()
	check.assert(t2 - t1 > 1.0)
	check.assert(retval == 0)
end

return {
	tcase = terra()
		var tc = check.TCase.alloc("async")
		tc:add_test(twraps.fiber_wrap(test_invoke))
		return tc
	end
}
