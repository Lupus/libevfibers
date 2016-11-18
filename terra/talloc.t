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

local inspect = require("inspect")

local C = terralib.includecstring[[

#include <talloc.h>

]]

terralib.linklibrary("libtalloc.so")

local M = {}

local function location(what)
	return ("%s:%d"):format(what.tree.filename, what.tree.linenumber)
end

local talloc = macro(function(ctx, typ)
	typ = typ:astype()
	return `[&typ](C.talloc_named_const(ctx, sizeof(typ), [tostring(typ)]))
end)

M.talloc = talloc

M.lua_talloc = function(ctx, typ)
	return terralib.cast(&typ, C.talloc_named_const(ctx,
			terralib.sizeof(typ), tostring(typ)))
end

M.init = macro(function(fmt, ...)
	assert(fmt:gettype() == &int8,
			"format must be string literal")
	assert(terralib.isconstant(fmt, "format must be a constant"))
	return `C.talloc_init(fmt, [{...}])
end)

M.free = macro(function(ctx)
	return quote
		var rv = C._talloc_free(ctx, [location(ctx)])
	in
		rv == 0
	end
end)

M.defer_free = macro(function(ctx)
	return quote
		defer C._talloc_free(ctx, [location(ctx)])
	end
end)

M.free_children = terra(ctx: &opaque)
	C.talloc_free_children(ctx)
end

M.set_destructor = macro(function(ctx, destructor)
	assert(destructor:gettype():ispointertofunction(),
			"destructor must be pointer to function")
	assert(#destructor:gettype().type.parameters == 1,
			"destructor should take one parameter")
	local arg_type = destructor:gettype().type.parameters[1]
	assert(tostring(arg_type) == tostring(ctx:gettype()),
			("context and destructor argument types mismatch: \z
			%s and %s"):format(ctx:gettype(), arg_type))
	local df
	local rets = destructor:gettype().type.returntype
	if rets:isstruct(rets) then
		assert(#rets.entries == 0,
				"invalid return type for the destructor")
		df = terra(ptr: &opaque)
			destructor([arg_type](ptr))
			return 0
		end
	else
		assert(rets == bool,
				"destructor return value must be a boolean")
		df = terra(ptr: &opaque)
			if destructor([arg_type](ptr)) then
				return 0
			else
				return -1
			end
		end
	end
	return `C._talloc_set_destructor(ctx, df)
end)

local function assert_ptr_get_type(ptr)
	local typ = ptr:gettype()
	if not typ:ispointer() then
		error(("%s is not a pointer at %s"):format(ptr, location(ptr)))
	end
	return typ
end

M.steal = macro(function(new_ctx, ptr)
	local typ = assert_ptr_get_type(ptr)
	return `[&typ](C._talloc_steal_loc(new_ctx, ptr, [location(new_ctx)]))
end)

M.set_name = macro(function(ptr, fmt, ...)
	assert(fmt:gettype() == &int8,
			"format must be string literal")
	assert(terralib.isconstant(fmt, "format must be a constant"))
	local args = {...}
	if #args == 0 then
		return `C.talloc_set_name_const(ptr, fmt)
	end
	return `C.talloc_set_name(ptr, fmt, [args])
end)

M.move = macro(function(new_ctx, ptr)
	local typ = assert_ptr_get_type(ptr)
	return `[&typ](C._talloc_move(new_ctx, &ptr))
end)

M.named = macro(function(ctx, size, fmt, ...)
	assert(fmt:gettype() == &int8,
			"format must be string literal")
	assert(terralib.isconstant(fmt, "format must be a constant"))
	local args = {...}
	if #args == 0 then
		return `C.talloc_named_const(ctx, size, fmt)
	end
	return `C.talloc_named(ctx, size, fmt, [args])
end)

M.sized = macro(function(ctx, size)
	return `C.talloc_named_const(ctx, size, [location(ctx)])
end)

local struct TallocTmp {}

TallocTmp.methods.free = macro(function(tmp)
	return `C._talloc_free(&tmp, [location(tmp)])
end)

M.new = macro(function(ctx)
	local name = ("talloc_new: %s"):format(location(ctx))
	return `[&TallocTmp](C.talloc_named_const(ctx, 0, name))
end)

M.zero = macro(function(ctx, typ)
	typ = typ.tree.value -- get underlying type from luaobjecttype
	return `[&typ](C._talloc_zero(ctx, sizeof(typ), [tostring(typ)]))
end)

M.zero_sized = macro(function(ctx, size)
	return `C._talloc_zero(ctx, size, [location(ctx)])
end)

M.get_name = C.talloc_get_name

M.check_name = macro(function(ctx, name)
	local typ = assert_ptr_get_type(ctx)
	return `[&typ](C.talloc_check_name(ctx, name))
end)

M.parent = C.talloc_parent
M.parent_name = C.talloc_parent_name
M.total_size = C.talloc_total_size
M.total_blocks = C.talloc_total_blocks

M.memdup = macro(function(new_ctx, ptr, size)
	local typ = assert_ptr_get_type(ptr)
	return `[&typ](C._talloc_memdup(new_ctx, ptr, size,
			[location(new_ctx)]))
end)

M.find_parent_byname = C.talloc_find_parent_byname
M.find_parent_bytype = macro(function(ctx, typ)
	typ = typ.tree.value -- get underlying type from luaobjecttype
	return `C.talloc_find_parent_byname(ctx, [tostring(typ)])
end)

M.reference = macro(function(ctx, ptr)
	local typ = assert_ptr_get_type(ptr)
	return `[typ](C._talloc_reference_loc(ctx, ptr, [location(ctx)]))
end)

M.unlink = macro(function(ctx, ptr)
	return quote
		var rv = C.talloc_unlink(ctx, ptr)
	in
		rv == 0
	end
end)

M.autofree_context = C.talloc_autofree_context
M.get_size = C.talloc_get_size

M.array = macro(function(ctx, typ, count)
	typ = typ.tree.value -- get underlying type from luaobjecttype
	return `[&typ](C._talloc_array(ctx, sizeof(typ), count, [tostring(typ)]))
end)

M.array_sized = macro(function(ctx, size, count)
	return `C._talloc_array(ctx, size, count, [tostring(typ)])
end)

M.array_length = macro(function(ctx)
	return `C.talloc_get_size(ctx)/sizeof(@ctx)
end)

M.zero_array = macro(function(ctx, typ, count)
	typ = typ.tree.value -- get underlying type from luaobjecttype
	return `[&typ](C._talloc_zero_array(ctx, sizeof(typ), count,
			[tostring(typ)]))
end)

M.realloc_array = macro(function(ctx, ptr, typ, count)
	typ = typ.tree.value -- get underlying type from luaobjecttype
	return `[&typ](C._talloc_realloc_array(ctx, ptr, sizeof(typ), count,
			[location(ctx)]))
end)

M.realloc_size = macro(function(ctx, ptr, size)
	local typ = assert_ptr_get_type(ptr)
	return `[&typ](C._talloc_realloc(ctx, ptr, size, [location(ctx)]))
end)


M.strdup = C.talloc_strdup
M.strdup_append = C.talloc_strdup_append
M.strdup_append_buffer = C.talloc_strdup_append_buffer
M.strndup = C.talloc_strndup
M.strndup_append = C.talloc_strndup_append
M.strndup_append_buffer = C.talloc_strndup_append_buffer

M.asprintf = macro(function(ctx, fmt, ...)
	assert(fmt:gettype() == &int8,
			"format must be string literal")
	assert(terralib.isconstant(fmt, "format must be a constant"))
	local args = {...}
	if #args == 0 then
		return `C.talloc_strdup(fmt)
	end
	return `C.talloc_asprintf(ctx, fmt, [args])
end)

M.asprintf_append = macro(function(s, fmt, ...)
	assert(fmt:gettype() == &int8,
			"format must be string literal")
	assert(terralib.isconstant(fmt, "format must be a constant"))
	local args = {...}
	if #args == 0 then
		return `C.talloc_strdup_append(s, fmt)
	end
	return `C.talloc_asprintf_append(s, fmt, [args])
end)

M.asprintf_append_buffer = macro(function(s, fmt, ...)
	assert(fmt:gettype() == &int8,
			"format must be string literal")
	assert(terralib.isconstant(fmt, "format must be a constant"))
	local args = {...}
	if #args == 0 then
		return `C.talloc_strdup_append_buffer(s, fmt)
	end
	return `C.talloc_asprintf_append_buffer(s, fmt, [args])
end)


M.report_depth_file = C.talloc_report_depth_file
M.report_full = C.talloc_report_full
M.report = C.talloc_report
M.enable_leak_report = C.talloc_enable_leak_report
M.enable_leak_report_full = C.talloc_enable_leak_report_full
M.set_log_fn = C.talloc_set_log_fn
M.set_log_stderr = C.talloc_set_log_stderr
M.set_memlimit = C.talloc_set_memlimit

local defalt_options = {
	enable_salloc = false,
}

local function install_mt(T, options)
	if not options then
		options = defalt_options
	end
	terra T.methods.free(self: &T)
		M.free(self)
	end
end

function M.Object(T)
	install_mt(T)
end

function M.ObjectWithOptions(options)
	return function(T)
		install_mt(T, options)
	end
end

function M.complete_type(T, options)
	if not options then
		options = defalt_options
	end
	local ctor = T:getmethod("__init")
	local dtor = T:getmethod("__destruct")
	if ctor and type(ctor.gettype) ~= "function" then
		-- it's a macro
		T.methods.talloc = macro(function(ctx, ...)
			local stmts = terralib.newlist()
			local ptr = symbol(&T)
			local args = {ptr, ...}
			stmts:insert(`ctor([args]))
			if dtor then
				stmts:insert(`M.set_destructor(ptr, dtor))
			end
			return quote
				var [ptr] = M.talloc(ctx, [T])
				[stmts]
			in
				ptr
			end
		end)
	else
		-- it's a terra function (or no ctor at all)
		local ptr = symbol(&T)
		local stmts = terralib.newlist()
		local ctor_extra_args = terralib.newlist()
		if ctor then
			for i, v in ipairs(ctor:gettype().parameters) do
				if i > 1 then -- ignore first `self` parameter
					ctor_extra_args:insert(symbol(v))
				end
			end
			stmts:insert(`ctor(ptr, [ctor_extra_args]))
		end
		if T:getmethod("__destruct") then
			local dtor = T:getmethod("__destruct")
			stmts:insert(`M.set_destructor(ptr, dtor))
		end
		T.methods.talloc = terra(ctx: &opaque, [ctor_extra_args])
			var [ptr] = M.talloc(ctx, [T])
			[stmts]
			return ptr
		end
	end
	if options.enable_salloc then
		T.methods.salloc = macro(function(...)
			local ptr = symbol(&T)
			local stmts = terralib.newlist()
			local ctor = T:getmethod("__init")
			if ctor then
				local args = {ptr, ...}
				stmts:insert(`ctor([args]))
			end
			if T:getmethod("__destruct") then
				stmts:insert(quote
					defer [ptr]:__destruct()
				end)
			end
			return quote
				var data : T
				var [ptr] = &data
				[stmts]
			in
				ptr
			end
		end)
	end
end

return M
