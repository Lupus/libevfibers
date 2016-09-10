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
local talloc = require("talloc")

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


local struct Context(talloc.Object) {
	fctx: C.fbr_context -- must be first for simple casts
}

M.Context = Context

Context.metamethods.__cast = function(from,to,exp)
	if to == &C.fbr_context then
		return `&exp.fctx
	end

	error(("invalid cast from %s to %s"):format(from, to))
end


terra Context:__init(loop: &ev.Loop)
	C.fbr_init(self, loop)
end

terra Context:__destruct()
	C.fbr_destroy(self)
end

local ErrorCode = int
M.ErrorCode = ErrorCode

local struct Error(talloc.Object) {
	fctx: &Context
	code: ErrorCode
	errno: int
}

terra Error:__init(fctx: &Context, code: ErrorCode, errno: int)
	self.fctx = fctx
	talloc.reference(self, fctx)
	self.code = code
	self.errno = errno
end

terra Error:__destruct()
	util.assert(talloc.unlink(self, self.fctx), "unable to \z
				talloc.unlink Context from self")
end

terra Error:to_string()
	if self.code == M.ESYSTEM then
		return CString.sprintf("system error: %s",
				C.strerror(self.errno))
	else
		return CString.sprintf("evfibers error: %s",
				C.fbr_strerror(self.fctx, self.code))
	end
end


terra Context:last_error()
	-- FIXME: allocate this per fiber
	return Error.talloc(self, self, self.fctx.f_errno, C.wrap_errno())
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

terra Context:yield()
	return C.fbr_yield(self)
end

terra Context:sleep(duration: C.ev_tstamp)
	return C.fbr_sleep(self, duration)
end

local IFiber = golike.Interface({
	run = {&Context} -> {}
})

M.IFiber = IFiber

local struct SimpleFiber(talloc.Object) {
	func: {&Context} -> {}
}

terra SimpleFiber:__init(fn: {&Context} -> {})
	self.func = fn
end

terra SimpleFiber:run(fctx: &Context)
	self.func(fctx)
end

terra M.simple_fiber(ctx: &opaque, fn: {&Context} -> {}) : IFiber
	return SimpleFiber.talloc(ctx, fn)
end

local struct TrampolineArg(talloc.Object) {
	fiber: IFiber
}

terra TrampolineArg:__init(fiber: IFiber)
	self.fiber = fiber
end

terra fiber_trampoline(fiber_context: &C.fbr_context, arg_: &opaque)
	-- we assume fiber_context is pointer to the first member of Context,
	-- and thus we can just cast from one to another
	var fctx = [&Context](fiber_context)
	var arg = [&TrampolineArg](arg_)
	C.fbr_set_noreclaim(fctx, fctx:self())
	arg.fiber:run(fctx)
	C.fbr_set_reclaim(fctx, fctx:self())
	arg.fiber:free()
end

terra Context:create(name: CString, fiber: IFiber) : FiberID
	var arg = TrampolineArg.talloc(fiber, fiber)
	return C.fbr_create(&self.fctx, name, fiber_trampoline, [&int8](arg), 0)
end

local struct Mutex(talloc.Object) {
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

terra Mutex:__init(fctx: &Context)
	self.fctx = fctx
	C.fbr_mutex_init(fctx, self)
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


local struct CondVar(talloc.Object) {
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

terra CondVar:__init(fctx: &Context)
	self.fctx = fctx
	C.fbr_cond_init(fctx, self)
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
	C.fbr_cond_broadcast(self.fctx, self)
end

Context.methods.ev_wait = macro(function(self, ...)
	local args = {...}
	local wait_impl = terralib.types.newstruct("EVWaitImpl")
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
		elseif arg:gettype() == &CondVar then
			wait_impl.entries:insert({
				field = base(i),
				type = C.fbr_ev_cond_var
			})
		elseif ev.is_watcher(arg:gettype()) then
			wait_impl.entries:insert({
				field = base(i),
				type = C.fbr_ev_watcher
			})
		else
			error("unexpected type for ev_wait: " ..
					tostring(arg:gettype()))
		end
	end
	for i, arg in ipairs(args) do
		local j = i - 1
		if arg:gettype() == &Mutex then
			init:insert(quote
				var ev_mutex = &impl.[base(i)]
				C.fbr_ev_mutex_init(self, ev_mutex, arg)
				impl.events[j] = &ev_mutex.ev_base
			end)
		elseif arg:gettype() == &CondVar then
			init:insert(quote
				var ev_cond_var = &impl.[base(i)]
				C.fbr_ev_cond_var_init(self, ev_cond_var, arg,
						nil)
				impl.events[j] = &ev_cond_var.ev_base
			end)
		elseif ev.is_watcher(arg:gettype()) then
			init:insert(quote
				var ev_watcher = &impl.[base(i)]
				C.fbr_ev_watcher_init(self, ev_watcher, arg)
				impl.events[j] = &ev_watcher.ev_base
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

function M.Fiber(T)
	local fld = function(name) return ("_fiber_%s"):format(name) end
	T.entries:insert({
		field = "fctx",
		type = &Context,
	})
	T.entries:insert({
		field = "fid",
		type = FiberID,
	})
	talloc.Object(T)

	T.methods.create = macro(function(fctx, ...)
		local args = {...}
		return quote
			var f = T.talloc([args])
			f.fctx = fctx
			var id = fctx:create([tostring(T)], f)
			f.fid = id
		in
			f
		end
	end)
	terra T:transfer()
		self.fctx:transfer(self.fid)
	end
	terra T:yield()
		self.fctx:yield()
	end
end

return M
