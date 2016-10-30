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
local primitive = require("primitive")
local check = require("check")
local twraps = require("twraps")
local talloc = require("talloc")
local fds = require("fds")
local errors = require("errors")
local ev = require("ev")
local fbr = require("evfibers")

local CString = primitive.CString
local IError = errors.IError

terra test_address(loop: &ev.Loop, fctx: &fbr.Context, i: int, ctx: &opaque)
	var addr = fds.Address.talloc(ctx)
	var err : IError
	err = addr:resolve("tcp", "1.2.3.4", "http")
	check.assert(err == nil)

	var fd : &fds.AsyncFD
	fd, err = fds.socket(ctx, addr)
	check.assert(err == nil)

	var kind : CString
	var name : CString
	var service : CString
	kind, name, service, err = addr:name_info()
	check.assert(err == nil)
	check.assert(kind == "tcp")
	check.assert(name == "1.2.3.4")
	check.assert(service == "http")
	kind, name, service, err = addr:name_info("numeric_host",
			"numeric_service")
	check.assert(err == nil)
	check.assert(kind == "tcp")
	check.assert(name == "1.2.3.4")
	check.assert(service == "80")
	kind, name, service, err = addr:name_info("name_required")
	check.assert(err ~= nil)
	S.printf("name_info err: %s\n", err:string())
	err:free()

	err = addr:resolve("unix", "/tmp/mysocket.sock", "")
	check.assert(err == nil)
	fd, err = fds.socket(ctx, addr)
	check.assert(err == nil)
	kind, name, service, err = addr:name_info()
	check.assert(err == nil)
	check.assert(kind == "unix")
	check.assert(name == "/tmp/mysocket.sock")
	check.assert(service == "")
end

terra fiber_server(fctx: &fbr.Context)
	var ctx = talloc.new(nil)
	talloc.defer_free(ctx)

	var err : IError
	var l : &fds.AsyncFDListener
	l, err = fds.listen(ctx, fctx, "tcp", "127.0.0.1", "12345")
	check.assert(err == nil)

	var conn : &fds.AsyncFDConn
	conn, err = l:accept(ctx)
	check.assert(err == nil)

	fctx:log_d("hello from reader!")
	return
end

terra fiber_client(fctx: &fbr.Context)
	var ctx = talloc.new(nil)
	talloc.defer_free(ctx)

	var err : IError
	var conn : &fds.AsyncFDConn
	conn, err = fds.dial(ctx, fctx, "tcp", "127.0.0.1", "12345")
	check.assert(err == nil)

	fctx:log_d("hello from writer!")
	return
end

terra test_client_server(loop: &ev.Loop, fctx: &fbr.Context, i: int,
		ctx: &opaque)
	var id1 = fctx:create("server", fbr.simple_fiber(fiber_server))
	fctx:transfer(id1)
	var id2 = fctx:create("client", fbr.simple_fiber(fiber_client))
	fctx:transfer(id2)
end

terra test_basic(loop: &ev.Loop, fctx: &fbr.Context, i: int, ctx: &opaque)
	var fd = fds.AsyncFD.talloc(ctx, 666)
	var err : IError = fd:close()
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

return {
	tcase = terra()
		var tc = check.TCase.alloc("fds")
		tc:add_test(twraps.fiber_wrap(test_address))
		tc:add_test(twraps.fiber_wrap(test_basic))
		tc:add_test(twraps.fiber_wrap(test_client_server))
		return tc
	end
}
