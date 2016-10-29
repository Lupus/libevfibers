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
local fbr = require("evfibers")

local C = terralib.includecstring[[

#include <evfibers/fiber.h>
#include <evfibers/eio.h>

]]

local M = {}

local function fname(i)
	return ("arg_%d"):format(i)
end

local wrapper_cache = {}

local async_wrapper = function(func)
	assert(func:gettype():ispointertofunction(),
			"`func` must be a pointer to a function")
	local fn_type = func:gettype().type
	if wrapper_cache[fn_type] then
		return wrapper_cache[fn_type]
	end
	local data = terralib.types.newstruct(("async_data_for_%s"):format(
			func))
	for i, v in ipairs(fn_type.parameters) do
		data.entries:insert({
			field = fname(i),
			type = v,
		})
	end
	data.entries:insert({
		field = "ret",
		type = fn_type.returntype,
	})
	local data_ptr = symbol(&data)
	local inv_args = terralib.newlist()
	for i, v in ipairs(fn_type.parameters) do
		inv_args:insert(`[data_ptr].[fname(i)])
	end
	local terra eio_callback(ptr: &opaque) : C.eio_ssize_t
		var [data_ptr] = [&data](ptr)
		data_ptr.ret = func([inv_args])
		return 0
	end
	local fn_args = {}
	local arg_syms = {}
	local args_save_stmts = terralib.newlist()
	for i, v in ipairs(fn_type.parameters) do
		local s = symbol(v)
		table.insert(fn_args, s)
		table.insert(arg_syms, s)
		args_save_stmts:insert(quote [data_ptr].[fname(i)] = [s] end)
	end
	local terra wrapper(fctx: &fbr.Context, [arg_syms])
		var data_storage : data
		var [data_ptr] = &data_storage
		[args_save_stmts]
		C.fbr_eio_custom(fctx, eio_callback, data_ptr, 0)
		return data_ptr.ret
	end
	wrapper_cache[fn_type] = wrapper
	return wrapper
end

M.wrapper = async_wrapper

local async_invoke = macro(function(fctx, func, ...)
	local args = {...}
	local wrapper = async_wrapper(func)
	return `wrapper(fctx, [args])
end)

M.invoke = async_invoke

return M
