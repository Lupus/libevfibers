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

local golike = require("golike")
local talloc = require("talloc")
local primitive = require("primitive")
local S = require("std")

local CString = primitive.CString

local C = terralib.includecstring[[

#define UNW_LOCAL_ONLY

#include <libunwind.h>

int wrap_unw_getcontext(unw_context_t *context) {
	return unw_getcontext(context);
}

int wrap_unw_init_local(unw_cursor_t *cursor, unw_context_t *context) {
	return unw_init_local(cursor, context);
}

int wrap_unw_step(unw_cursor_t *cursor) {
	return unw_step(cursor);
}

int wrap_unw_get_reg(unw_cursor_t *cursor, unw_regnum_t reg, unw_word_t *word) {
	return unw_get_reg(cursor, reg, word);
}

int wrap_unw_get_proc_name(unw_cursor_t *cursor, char *buf, size_t size,
		unw_word_t *word) {
	return unw_get_proc_name(cursor, buf, size, word);
}

]]

terralib.linklibrary("libunwind.so")

local M = {}

local backtrace_size = 128

local struct Frame {
	ip: &opaque,
	unw_name: CString,
}

local struct Backtrace(talloc.Object) {
	frames: Frame[backtrace_size]
	count: int
}

local ra = terralib.intrinsic("llvm.returnaddress", int32 -> &opaque )
local fa = terralib.intrinsic("llvm.frameaddress", int32 -> &opaque )

local bt_use_terra_rt = global(bool, false)
(terra() bt_use_terra_rt = true end)()
local is_debug = constant(bool,terralib.isdebug ~= 0)

terra Backtrace:capture()
	var cursor : C.unw_cursor_t
	var context : C.unw_context_t
	
	C.wrap_unw_getcontext(&context)
	C.wrap_unw_init_local(&cursor, &context)
	
	var n = 0
	while C.wrap_unw_step(&cursor) ~= 0 and n < backtrace_size do
		var ip : C.unw_word_t	
		C.wrap_unw_get_reg(&cursor, C.UNW_REG_IP, &ip)
		self.frames[n].ip = [&opaque](ip)
		var off : C.unw_word_t
		var sym : int8[256]
		if C.wrap_unw_get_proc_name(&cursor, sym, 256, &off) == 0 then
			self.frames[n].unw_name = talloc.asprintf(self,
					"%s+0x%lx", sym, off)
		else
			self.frames[n].unw_name = nil
		end
		n = n + 1
	end
	self.count = n - 1
end

terra Backtrace:string() : CString
	var ptr = talloc.strdup(self, "")
	for i = 0,self.count do
		var si : terralib.SymbolInfo
		var name = "???"
		var location = ""
		var ip = self.frames[i].ip
		if bt_use_terra_rt and terralib.lookupsymbol(ip, &si) then
			name = talloc.asprintf(self, "%.*s", si.namelength,
					si.name)
			var li : terralib.LineInfo
			if terralib.lookupline(si.addr, ip, &li) then
				location = talloc.asprintf(self, " at %.*s:%d",
						li.namelength, li.name,
						[int](li.linenum))
			end
		elseif self.frames[i].unw_name ~= nil then
			name = self.frames[i].unw_name
		end
		ptr = talloc.asprintf_append(ptr, "#%d  %p in %s ()%s\n",
				i, self.frames[i].ip, name, location)
	end
	return ptr
end

talloc.complete_type(Backtrace)


local IError = golike.Interface({
	string = {} -> &CString,
	verbose = {} -> &CString,
	caused_by = {} -> golike.This,
	link = {golike.This} -> golike.This,
})

M.IError = IError

function M.new(name)
	local error_impl = terralib.types.newstruct(name)
	error_impl.entries:insert({ field = "msg", type = CString })
	error_impl.entries:insert({ field = "msg_verbose", type = CString })
	error_impl.entries:insert({ field = "location", type = CString })
	error_impl.entries:insert({ field = "backtrace", type = &Backtrace })
	error_impl.entries:insert({ field = "cause", type = IError })
	talloc.Object(error_impl)
	error_impl.methods.__init = macro(function(self, ...)
		local args = {...}
		assert(#args > 1, "at least two arguments are required")
		local loc = ("%s:%d"):format(args[1].tree.filename,
				args[1].tree.linenumber)
		return quote
			self.msg = talloc.asprintf(self, "Error(%s): ",
					[tostring(error_impl)])
			self.msg = talloc.asprintf_append(self.msg, [args])
			self.msg_verbose = nil
			self.location = [loc]
			if is_debug then
				self.backtrace = Backtrace.talloc(self)
				self.backtrace:capture()
			else
				self.backtrace = nil
			end
			self.cause = nil
		end
	end)
	terra error_impl:__destruct()
		if self.cause ~= nil then
			self.cause:free()
		end
	end
	terra error_impl:string()
		return self.msg
	end
	terra error_impl:caused_by()
		return self.cause
	end
	terra error_impl:link(other: IError)
		talloc.steal(self, [&opaque](other))
		self.cause = other
		return self
	end
	terra error_impl:verbose()
		if self.msg_verbose ~= nil then
			return self.msg_verbose
		end
		if is_debug then
			self.msg_verbose = talloc.asprintf(self,
					"%s\nAt: %s\nFull backtrace:\n%s\n",
					self:string(), self.location,
					self.backtrace:string())
		else
			self.msg_verbose = talloc.asprintf(self,
					"%s\nAt: %s\n",
					self:string(), self.location)
		end
		return self.msg_verbose
	end
	talloc.complete_type(error_impl)
	return error_impl
end

terra M.format_chain(cause: IError) : CString
	var ptr = talloc.strdup(cause, "")
	if cause == nil then
		return ptr
	end
	ptr = talloc.asprintf_append(ptr, "%s", cause:verbose())
	if cause:caused_by() ~= nil then
		repeat
			cause = cause:caused_by()
			ptr = talloc.asprintf_append(ptr, "\nCaused by: %s",
					cause:verbose())
		until cause ~= nil
	end
	return ptr
end

return M
