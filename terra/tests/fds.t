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
local talloc = require("talloc")
local fds = require("fds")

terra test_basic(i: int, ctx: &opaque)
	var fd = fds.FD.talloc(ctx, 666)
	var err = fd:close()
	check.assert(err ~= nil)
	S.printf("err: %s\n", err:string())
	err:free()
	fd, err = fds.socket(ctx, "tcp6")
	check.assert(err == nil)
	err = fd:close()
	check.assert(err == nil)
	fd, err = fds.socket(ctx, "udp4")
	check.assert(err == nil)
	err = fd:close()
	check.assert(err == nil)
	fd, err = fds.socket(ctx, "unixpacket")
	check.assert(err == nil)
	err = fd:close()
	check.assert(err == nil)
end

local twrap = macro(function(test_fn)
	return terra(i: int)
		var ctx = talloc.new(nil)
		test_fn(i, ctx)
		ctx:free()
	end
end)

return {
	tcase = terra()
		var tc = check.TCase.alloc("fds")
		tc:add_test(twrap(test_basic))
		return tc
	end
}
