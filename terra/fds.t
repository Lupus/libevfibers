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

local talloc = require("talloc")
local errors = require("errors")

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
#include <errno.h>
#include <err.h>
#include <string.h>

const char *errno_to_str() {
	return strerror(errno);
}

]]

local M = {}

local EFD = errors.new("FD")

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

local struct FD(talloc.Object) {
	fd: int
}

M.FD = FD

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

M.socket = macro(function(ctx, kind)
	kind = kind:asvalue()
	local domain
	local type
	local protocol = 0
	if kind == "tcp" then
		domain = C.PF_INET
		type = C.SOCK_STREAM
	elseif kind == "tcp4" then
		domain = C.PF_INET
		type = C.SOCK_STREAM
	elseif kind == "tcp6" then
		domain = C.PF_INET6
		type = C.SOCK_STREAM
	elseif kind == "udp" then
		domain = C.PF_INET
		type = C.SOCK_DGRAM
	elseif kind == "udp4" then
		domain = C.PF_INET
		type = C.SOCK_DGRAM
	elseif kind == "udp6" then
		domain = C.PF_INET6
		type = C.SOCK_DGRAM
	elseif kind == "unix" then
		domain = C.PF_LOCAL
		type = C.SOCK_STREAM
	elseif kind == "unixgram" then
		domain = C.PF_LOCAL
		type = C.SOCK_DGRAM
	elseif kind == "unixpacket" then
		domain = C.PF_LOCAL
		type = C.SOCK_SEQPACKET
	else
		error("invalid socket kind: " .. tostring(kind))
	end
	return quote
		var fd : &FD = nil
		var result, err = ecall2(nil, "socket", domain, type, protocol)
		if err == nil then
			fd = FD.talloc(ctx, result)
		end
	in
		fd, err
	end
end)

return M
