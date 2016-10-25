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
local talloc = require("talloc")
local golike = require("golike")
local errors = require("errors")
local fbr = require("evfibers")

local CString = primitive.CString
local ByteSlice = primitive.ByteSlice
local IError = errors.IError

local C = terralib.includecstring[[

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/fcntl.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <errno.h>
#include <err.h>
#include <string.h>

const char *errno_to_str() {
	return strerror(errno);
}

#include <evfibers/fiber.h>

]]

local M = {}

local EFD = errors.new("FD")
local EAddress = errors.new("Address")
local EGAI = errors.new("GAI")

local ecall2 = macro(function(ctx, fname, ...)
	fname = fname:asvalue()
	local fn = C[fname]
	local args = {...}
	assert(fn, "function not found in C: " .. tostring(fname))
	return quote
		var err : &EFD = nil
		var result = fn([args])
		if result == -1 then
			err = EFD.talloc(ctx, "%s(): %s", fname,
					C.errno_to_str())
		end
	in
		result, err
	end
end)

local ecall = macro(function(ctx, fname, ...)
	local args = {...}
	return quote
		var result, err = ecall2(ctx, fname, [args])
	in
		err
	end
end)

local function location(what)
	return ("%s:%d"):format(what.tree.filename, what.tree.linenumber)
end

local struct Address(talloc.Object) {
	ss: C.sockaddr_storage
	len: C.socklen_t
	kind: CString,
}

M.Address = Address

Address.metamethods.__cast = function(from,to,exp)
	local emsg_family = ("invalid cast of %s to %s: wrong family"):format(
			tostring(exp), tostring(to))
	if to == &C.sockaddr_storage then
		return `exp.ss
	elseif to == &C.sockaddr then
		return `[&C.sockaddr](&exp.ss)
	elseif to == &opaque then
		return `[&opaque](&exp.ss)
	elseif to == &C.sockaddr_in then
		return quote
			util.assert(exp.ss.ss_family == C.PF_INET, emsg_family)
		in
			[&C.sockaddr_in](exp.ss)
		end
	elseif to == &C.sockaddr_in6 then
		return quote
			util.assert(exp.ss.ss_family == C.PF_INET6, emsg_family)
		in
			[&C.sockaddr_in6](exp.ss)
		end
	elseif to == &C.sockaddr_un then
		return quote
			util.assert(exp.ss.ss_family == C.PF_LOCAL, emsg_family)
		in
			[&C.sockaddr_un](exp.ss)
		end
	end
	error(("invalid cast from %s to %s"):format(from, to))
end

local value_sizeof = macro(function(what)
	return `sizeof([what:gettype()])
end)

local terra parse_kind(kind: CString)
	var family : int
	var type : int
	if kind == "tcp" then
		family = C.PF_UNSPEC
		type = C.SOCK_STREAM
	elseif kind == "tcp4" then
		family = C.PF_INET
		type = C.SOCK_STREAM
	elseif kind == "tcp6" then
		family = C.PF_INET6
		type = C.SOCK_STREAM
	elseif kind == "udp" then
		family = C.PF_UNSPEC
		type = C.SOCK_DGRAM
	elseif kind == "udp4" then
		family = C.PF_INET
		type = C.SOCK_DGRAM
	elseif kind == "udp6" then
		family = C.PF_INET6
		type = C.SOCK_DGRAM
	elseif kind == "unix" then
		family = C.PF_LOCAL
		type = C.SOCK_STREAM
	elseif kind == "unixgram" then
		family = C.PF_LOCAL
		type = C.SOCK_DGRAM
	elseif kind == "unixpacket" then
		family = C.PF_LOCAL
		type = C.SOCK_SEQPACKET
	else
		return 0, 0, EAddress.talloc(nil, "invalid address kind: `%s`",
				kind)
	end
	return family, type, nil
end

Address.methods.capacity = macro(function(self)
	return `value_sizeof(self.ss)
end)

terra Address:resolve(kind: CString, name: CString, service: CString) : IError
	var family, type, err = parse_kind(kind)
	if err ~= nil then
		return err
	end
	self.kind = talloc.strdup(self, kind)
	if family == C.PF_LOCAL then
		var sun = [&C.sockaddr_un](&self.ss)
		sun.sun_family = family
		C.strncpy(sun.sun_path, name, value_sizeof(sun.sun_path))
	else
		var hints : C.addrinfo
		var res : &C.addrinfo
		C.memset(&hints, 0, sizeof(C.addrinfo))
		hints.ai_family = family
		hints.ai_socktype = type
		var result = C.getaddrinfo(name, service, &hints, &res)
		if result ~= 0 then
			return EGAI.talloc(nil, "%s", C.gai_strerror(result))
		end
		C.memcpy(&self.ss, res.ai_addr, res.ai_addrlen)
		self.len = res.ai_addrlen
	end
	return nil
end

local name_info_flags = {
	name_required   = C.NI_NAMEREQD,
	dgram           = C.NI_DGRAM,
	no_fqdn         = C.NI_NOFQDN,
	numeric_host    = C.NI_NUMERICHOST,
	numeric_service = C.NI_NUMERICSERV,
}

terra Address:_name_info_wrapper(flags: int)
		: {CString, CString, IError}
	var hbuf : CString = talloc.named(self, C.NI_MAXHOST, "NI_MAXHOST buf")
	var sbuf : CString = talloc.named(self, C.NI_MAXSERV, "NI_MAXSERV buf")
	var result = C.getnameinfo(self, self.len, hbuf, C.NI_MAXHOST,
			sbuf, C.NI_MAXSERV, flags)
	if result ~= 0 then
		talloc.free(hbuf)
		talloc.free(sbuf)
		return nil, nil, EGAI.talloc(nil, "%s", C.gai_strerror(result))
	end
	return hbuf, sbuf, nil
end

Address.methods.name_info = macro(function(self, ...)
	local flags = {...}
	local call_flags = symbol(int)
	local flag_stmts = terralib.newlist()
	flag_stmts:insert(quote call_flags = 0 end)
	for _, flag in ipairs(flags) do
		flag = flag:asvalue()
		local nif = name_info_flags[flag]
		if not nif then
			error("invalid flag: " .. tostring(flag))
		end
		flag_stmts:insert(quote call_flags = call_flags or nif end)
	end
	return quote
		var [call_flags]
		[flag_stmts]
		var err : IError = nil
		var kind = self.kind
		var name : CString = ""
		var service : CString = ""
		if self.ss.ss_family == C.PF_LOCAL then
			var sun = [&C.sockaddr_un](&self.ss)
			name = talloc.strndup(self, sun.sun_path,
					value_sizeof(sun.sun_path))
		else
			name, service, err = self:_name_info_wrapper(call_flags)
		end
		kind = talloc.strdup(self, kind)
		name = talloc.strdup(self, name)
		service = talloc.strdup(self, service)
	in
		kind, name, service, err
	end
end)

local IFD = golike.Interface({
	close = {} -> {}
})

local function fd_common(FD)
	FD.metamethods.__cast = function(from,to,exp)
		if to == int then
			return `exp.fd
		end
		error(("invalid cast from %s to %s"):format(from, to))
	end

	terra FD:__init(fd: int)
		self.fd = fd
	end

	terra FD:__destruct()
		if not self:is_valid() then
			return
		end
		self:close()
	end

	terra FD:is_valid()
		return self.fd >= 0
	end

	terra FD:close()
		var err = ecall(self, "close", self)
		self.fd = -1
		return err
	end

	terra FD:listen(backlog: int)
		return ecall(self, "listen", self, backlog)
	end

	terra FD:bind(addr: &Address)
		return ecall(self, "bind", self, addr, addr.len)
	end
end

local struct AsyncFD(talloc.Object) {
	fd: int
}

M.AsyncFD = AsyncFD

fd_common(AsyncFD)

local terra socket_wrap(ctx: &opaque, domain: int, type: int, protocol: int)
	var fd : &AsyncFD = nil
	var result, err = ecall2(nil, "socket", domain, type, protocol)
	if err == nil then
		fd = AsyncFD.talloc(ctx, result)
	end
	return fd, err
end

local socket = terralib.overloadedfunction("socket")

M.socket = socket

socket:adddefinition(terra(ctx: &opaque, kind: CString) : {&AsyncFD, IError}
	var family, type, err = parse_kind(kind)
	if err ~= nil then
		return nil, err
	end
	return socket_wrap(ctx, family, type, 0)
end)

socket:adddefinition(terra(ctx: &opaque, addr: &Address) : {&AsyncFD, IError}
	var family, type, err = parse_kind(addr.kind)
	if err ~= nil then
		return nil, err
	end
	return socket_wrap(ctx, addr.ss.ss_family, type, 0)
end)


terra AsyncFD:read(bs: &ByteSlice) : {int, IError}
end

terra AsyncFD:write(bs: ByteSlice) : {int, IError}
end

local struct AsyncFDListener(talloc.Object) {
	fctx: &fbr.Context
	fd: &AsyncFD
	addr: &Address
}

M.AsyncFDListener = AsyncFDListener

terra M.listen(ctx: &opaque, fctx: &fbr.Context, kind: CString, name: CString, service: CString)
		: {&AsyncFDListener, IError}

	var l = AsyncFDListener.talloc(ctx)
	l.fctx = fctx
	l.addr = Address.talloc(l)
	var err : IError
	err = l.addr:resolve(kind, name, service)
	if err ~= nil then goto on_error end

	var fd : &AsyncFD
	fd, err = socket(l, l.addr)
	if err ~= nil then goto on_error end
	l.fd = fd

	err = l.fd:bind(l.addr)
	if err ~= nil then goto on_error end

	err = l.fd:listen(64)
	if err ~= nil then goto on_error end

	do return l, nil end

	::on_error::
	l:free()
	return nil, err
end

local struct AsyncFDConn(talloc.Object) {
	fctx: &fbr.Context
	fd: &AsyncFD
	local_addr: &Address
	remote_addr: &Address
}

M.AsyncFDConn = AsyncFDConn

terra AsyncFDListener:accept(ctx: &opaque) : {&AsyncFDConn, IError}
	var conn = AsyncFDConn.talloc(ctx)
	var err : IError
	conn.local_addr = talloc.reference(conn, self.addr)
	conn.remote_addr = Address.talloc(conn)
	conn.remote_addr.len = conn.remote_addr:capacity()
	var result = C.fbr_accept(self.fctx, self.fd, conn.remote_addr,
			&conn.remote_addr.len)
	if result ~= 0 then
		err = self.fctx:last_error()
		goto on_error
	end
	do return conn, nil end

	::on_error::
	conn:free()
	return nil, err
end

terra M.dial(ctx: &opaque, fctx: &fbr.Context, kind: CString, name: CString,
		service: CString) : {&AsyncFDConn, IError}

	var conn = AsyncFDConn.talloc(ctx)
	var err : IError
	conn.remote_addr = Address.talloc(conn)
	err = conn.remote_addr:resolve(kind, name, service)
	if err ~= nil then goto on_error end

	conn.fd, err = socket(conn, conn.remote_addr)
	if err ~= nil then goto on_error end

	var result = C.fbr_connect(fctx, conn.fd, conn.remote_addr,
			conn.remote_addr.len)
	if result ~= 0 then
		err = fctx:last_error()
		goto on_error
	end

	conn.local_addr = Address.talloc(conn)
	conn.local_addr.len = conn.local_addr:capacity()
	err = ecall(ctx, "getsockname", conn.fd, conn.local_addr,
			&conn.local_addr.len)
	if err ~= nil then goto on_error end

	do return conn, nil end

	::on_error::
	conn:free()
	return nil, err
end

local struct BlockingFD(talloc.Object) {
	fd: int
}

M.BlockingFD = BlockingFD

fd_common(BlockingFD)

return M
