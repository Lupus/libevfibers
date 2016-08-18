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
local ev = require("ev")
local util = require("util")
local golike = require("golike")

local CString = util.CString

terralib.includepath = terralib.includepath .. ";../include;../build/include"

local C = terralib.includecstring[[

#include <stdio.h>
#include <errno.h>

int wrap_errno() {
	return errno;
}

#include <evfibers/fiber.h>

]]

terralib.linklibrary("../build/libevfibers.so")

local M = {}

M.SUCCESS        = C.FBR_SUCCESS
M.EINVAL         = C.FBR_EINVAL
M.ENOFIBER       = C.FBR_ENOFIBER
M.ESYSTEM        = C.FBR_ESYSTEM
M.EBUFFERMMAP    = C.FBR_EBUFFERMMAP
M.ENOKEY         = C.FBR_ENOKEY
M.EPROTOBUF      = C.FBR_EPROTOBUF
M.EBUFFERNOSPACE = C.FBR_EBUFFERNOSPACE
M.EEIO           = C.FBR_EEIO

M.LOG_ERROR   = C.FBR_LOG_ERROR
M.LOG_WARNING = C.FBR_LOG_WARNING
M.LOG_NOTICE  = C.FBR_LOG_NOTICE
M.LOG_INFO    = C.FBR_LOG_INFO
M.LOG_DEBUG   = C.FBR_LOG_DEBUG

M.EV_WATCHER  = C.FBR_EV_WATCHER
M.EV_MUTEX    = C.FBR_EV_MUTEX
M.EV_COND_VAR = C.FBR_EV_COND_VAR
M.EV_EIO      = C.FBR_EV_EIO

local struct FiberID {
	id: C.fbr_id_t
}

M.FiberID = FiberID

FiberID.metamethods.__cast = function(from,to,exp)
	if to == C.fbr_id_s then
		return `exp.id
	elseif from == C.fbr_id_s then
		return `FiberID { exp }
	end

	error(("invalid cast from %s to %s, \z
		FiberID can only be cast to/from fbr_id_t"):format(from, to))
end

M.ID_NULL = global(FiberID)

local terra init_id_null()
	M.ID_NULL = C.FBR_ID_NULL
end

init_id_null()

FiberID.metamethods.__eq = terra(a: FiberID, b: FiberID)
	return a.id.p == b.id.p and a.id.g == b.id.g
end

FiberID.metamethods.__ne = terra(a: FiberID, b: FiberID)
	return not (a == b)
end


local struct Context(S.Object) {
	fctx: C.fbr_context -- must be first for simple casts
}

M.Context = Context

Context.metamethods.__cast = function(from,to,exp)
	if to == &C.fbr_context then
		return `&exp.fctx
	end

	error(("invalid cast from %s to %s"):format(from, to))
end


do
	local obj = Context
	local terra obj_init(o: &obj, loop: &ev.Loop)
		C.fbr_init(o, loop)
	end
	local old_alloc = obj.methods.alloc
	obj.methods.alloc = terra(loop: &ev.Loop)
		var o = old_alloc()
		obj_init(o, loop)
		return o
	end
	local old_salloc = obj.methods.salloc
	obj.methods.salloc = macro(function(loop)
		assert(loop, "loop is required")
		return quote
			var o = old_salloc()
			obj_init(o, loop)
		in
			o
		end
	end)
end

local ErrorCode = int
M.ErrorCode = ErrorCode

local struct Error(S.Object) {
	fctx: &Context
	code: ErrorCode
	errno: int
}

terra Error:to_string()
	if self.code == M.ESYSTEM then
		return CString.sprintf("system error: %s",
				C.strerror(self.errno))
	else
		return CString.sprintf("evfibers error: %s",
				C.fbr_strerror(self.fctx, self.code))
	end
end

do
	local obj = Error
	local old_alloc = obj.methods.alloc
	local terra obj_init(o: &obj, fctx: &Context,
			code: ErrorCode, errno: int)
		o.fctx = fctx
		o.code = code
		o.errno = errno
	end
	obj.methods.alloc = terra(fctx: &Context, code: ErrorCode, errno: int)
		var o = old_alloc()
		obj_init(o, fctx, code, errno)
		return o
	end
	local old_salloc = obj.methods.salloc
	obj.methods.salloc = macro(function(fctx, code, errno)
		assert(fctx, "fctx is required")
		assert(code, "code is required")
		assert(errno, "errno is required")
		return quote
			var o = old_salloc()
			obj_init(o, fctx, code, errno)
		in
			o
		end
	end)
end

terra Context:last_error()
	return Error.alloc(self, self.fctx.f_errno, C.wrap_errno())
end

terra Context:__destruct()
	C.fbr_destroy(self)
end

local LogLevel = int
M.LogLevel = LogLevel

terra Context:need_log(level: LogLevel)
	return level <= self.fctx.logger.level
end

terra Context:set_log_level(desired_level: LogLevel)
	self.fctx.logger.level = desired_level
end

local function gen_logger(letter)
	Context.methods["log_"..letter] = macro(function(self, format, ...)
		assert(self:gettype() == Context, "self must be Context")
		assert(format:gettype() == &int8,
				"format must be string literal")
		assert(terralib.isconstant(format, "format must be a constant"))
		return `C.["fbr_log_"..letter](self, format, [{...}])
	end)
end

for _, letter in ipairs({"d", "i", "n", "w", "e"}) do
	gen_logger(letter)
end

terra Context:self() : FiberID
	return C.fbr_self(self)
end

terra Context:transfer(id: FiberID)
	return C.fbr_transfer(self, id)
end

terra Context:sleep(duration: C.ev_tstamp)
	return C.fbr_sleep(self, duration)
end

local IFiber = golike.Interface({
	run = {&Context} -> {}
})

M.IFiber = IFiber

local struct SimpleFiber(S.Object) {
	func: {&Context} -> {}
}

terra SimpleFiber:run(fctx: &Context)
	self.func(fctx)
end

terra M.simple_fiber(fn: {&Context} -> {}) : IFiber
	var f = SimpleFiber.alloc()
	f.func = fn
	return f
end

local struct TrampolineArg(S.Object) {
	fiber: IFiber
}

terra fiber_trampoline(fiber_context: &C.fbr_context, arg_: &opaque)
	-- we assume fiber_context is pointer to the first member of Context,
	-- and thus we can just cast from one to another
	var fctx = [&Context](fiber_context)
	var arg = [&TrampolineArg](arg_)
	C.fbr_set_noreclaim(fctx, fctx:self())
	arg.fiber:run(fctx)
	C.fbr_set_reclaim(fctx, fctx:self())
	arg.fiber:delete()
	arg:delete()
end

terra Context:create(name: CString, fiber: IFiber) : FiberID
	var arg = TrampolineArg.alloc()
	arg.fiber = fiber
	return C.fbr_create(&self.fctx, name, fiber_trampoline, [&int8](arg), 0)
end

local struct Mutex(S.Object) {
	fctx: &Context
	mutex: C.fbr_mutex
}

M.Mutex = Mutex

Mutex.metamethods.__cast = function(from,to,exp)
	if to == &C.fbr_mutex then
		return `&exp.mutex
	end

	error(("invalid cast from %s to %s"):format(from, to))
end

do
	local obj = Mutex
	local old_alloc = obj.methods.alloc
	local terra obj_init(o: &obj, fctx: &Context)
		o.fctx = fctx
		C.fbr_mutex_init(fctx, o)
	end
	obj.methods.alloc = terra(fctx: &Context)
		var o = old_alloc()
		obj_init(o, fctx)
		return o
	end
	local old_salloc = obj.methods.salloc
	obj.methods.salloc = macro(function(fctx)
		assert(fctx, "fctx is required")
		return quote
			var o = old_salloc()
			obj_init(o, fctx)
		in
			o
		end
	end)
end

terra Mutex:__destruct()
	C.fbr_mutex_destroy(self.fctx, self)
end

terra Mutex:lock()
	C.fbr_mutex_lock(self.fctx, self)
end

terra Mutex:unlock()
	C.fbr_mutex_unlock(self.fctx, self)
end


local struct CondVar(S.Object) {
	fctx: &Context
	cond_var: C.fbr_cond_var
}

M.CondVar = CondVar

CondVar.metamethods.__cast = function(from,to,exp)
	if to == &C.fbr_cond_var then
		return `&exp.cond_var
	end

	error(("invalid cast from %s to %s"):format(from, to))
end

do
	local obj = CondVar
	local old_alloc = obj.methods.alloc
	local terra obj_init(o: &obj, fctx: &Context)
		o.fctx = fctx
		C.fbr_cond_init(fctx, o)
	end
	obj.methods.alloc = terra(fctx: &Context)
		var o = old_alloc()
		obj_init(o, fctx)
		return o
	end
	local old_salloc = obj.methods.salloc
	obj.methods.salloc = macro(function(fctx)
		assert(fctx, "fctx is required")
		return quote
			var o = old_salloc()
			obj_init(o, fctx)
		in
			o
		end
	end)
end

terra CondVar:__destruct()
	C.fbr_cond_destroy(self.fctx, self)
end

CondVar.methods.wait = terralib.overloadedfunction("wait")
CondVar.methods.wait:adddefinition(terra(self: &CondVar, m: &Mutex)
	var rv = C.fbr_cond_wait(self.fctx, self, m)
	if rv == -1 then
		return self.fctx:last_error()
	end
	return nil
end)
CondVar.methods.wait:adddefinition(terra(self: &CondVar)
	return self:wait(nil)
end)

terra CondVar:signal()
	C.fbr_cond_signal(self.fctx, self)
end

terra CondVar:broadcast()
	C.fbr_cond_signal(self.fctx, self)
end

Context.methods.ev_wait = macro(function(self, ...)
	local args = {...}
	local wait_impl = terralib.types.newstruct("EVWaitImpl")
	local function ev(i) return ("arg_%d"):format(i) end
	local function base(i) return ("arg_base_%d"):format(i) end
	local init = terralib.newlist()
	local impl = symbol(&wait_impl)
	wait_impl.entries:insert({
		field = "fctx",
		type = &Context,
	})
	wait_impl.entries:insert({
		field = "events",
		type = (&C.fbr_ev_base)[#args + 1],
	})
	for i, arg in ipairs(args) do
		local j = i - 1
		if arg:gettype() == &Mutex then
			wait_impl.entries:insert({
				field = base(i),
				type = C.fbr_ev_mutex
			})
			init:insert(quote
				var ev_mutex = &impl.[base(i)]
				C.fbr_ev_mutex_init(self, ev_mutex, arg)
				impl.events[j] = &ev_mutex.ev_base
			end)
		elseif arg:gettype() == &CondVar then
			wait_impl.entries:insert({
				field = base(i),
				type = &C.fbr_ev_cond_var
			})
			init:insert(quote
				C.fbr_ev_cond_var_init(self, &impl.[base(i)],
						arg)
			end)
		else
			error("unexpected type for ev_wait: " ..
					tostring(arg:gettype()))
		end
	end
	init:insert(quote
		impl.events[ [#args] ] = nil
		impl.fctx = &self
	end)
	terra wait_impl:wait()
		var nevents = C.fbr_ev_wait(self.fctx, self.events)
		if -1 == nevents then
			return 0, self.fctx:last_error()
		end
		return nevents, nil
	end
	wait_impl.methods.arrived = macro(function(self, what)
		for i, arg in ipairs(args) do
			local j = i - 1
			if tostring(what) == tostring(arg) then
				return `self.events[j].arrived == 1
			end
		end
		error("arrived called on object not in the wait set")
	end)

	return quote
		var wait_data : wait_impl
		var [impl] = &wait_data
		[init]
	in
		impl
	end
end)

return M
