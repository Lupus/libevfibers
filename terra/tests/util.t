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
local util = require("util")

terra call_fn(fn: {} -> int)
	return fn()
end

terra foo(a: int, b: int)
	return a + b
end

terra test_partial(i: int)
	for i=0,100000 do
	var p = util.partial(foo, 10)
	defer p:delete()
	var p2 = util.partial(p, 3)
	defer p2:delete()
	check.assert(call_fn(p2) == 13)
	end
end

return {
	tcase = terra()
		var tc = check.TCase.alloc("util")
		tc:add_test(test_partial)
		return tc
	end
}
