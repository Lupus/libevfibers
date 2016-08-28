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
local golike = require("golike")
local inspect = require("inspect")

local C = terralib.includecstring[[

#include <string.h>

/**
 * `asprintf.c' - asprintf
 *
 * copyright (c) 2014 joseph werle <joseph.werle@gmail.com>
 *
 * https://github.com/littlstar/asprintf.c/blob/master/asprintf.c
 */


#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

int
vasprintf (char **str, const char *fmt, va_list args) {
  int size = 0;
  va_list tmpa;

  // copy
  va_copy(tmpa, args);

  // apply variadic arguments to
  // sprintf with format to get size
  size = vsnprintf(NULL, size, fmt, tmpa);

  // toss args
  va_end(tmpa);

  // return -1 to be compliant if
  // size is less than 0
  if (size < 0) { return -1; }

  // alloc with size plus 1 for `\0'
  *str = (char *) malloc(size + 1);

  // return -1 to be compliant
  // if pointer is `NULL'
  if (NULL == *str) { return -1; }

  // format string with original
  // variadic arguments and set new size
  size = vsprintf(*str, fmt, args);
  return size;
}

int
asprintf (char **str, const char *fmt, ...) {
  int size = 0;
  va_list args;

  // init variadic argumens
  va_start(args, fmt);

  // format and get size
  size = vasprintf(str, fmt, args);

  // toss args
  va_end(args);

  return size;
}

]]

local M = {}

M.trap = terralib.intrinsic("llvm.trap", {} -> {})

M.assert = macro(function(check, msg)
	local loc = check.tree.filename..":"..check.tree.linenumber
	if not msg then
		msg = "assertion failed!"
	end
	return quote
		if not check then
			C.fprintf(C.stderr, "%s: %s\n", loc, msg)
			C.abort()
		end
	end
end)

local function starts(str,start)
	return string.sub(str,1,string.len(start))==start
end

local function fname(i)
	return ("arg_%d"):format(i)
end

local next_id = 1

M.partial = macro(function(fn, ...)
	local fn_type
	local args = {...}
	if fn.tree.type:isfunction() then
		fn_type = fn.tree.type
	elseif fn.tree.type:ispointertofunction() then
		fn_type = fn.tree.type.type
	elseif fn.tree.type:ispointertostruct() then
		local s_type = fn.tree.type.type
		if not starts(tostring(s_type), "util_partial_") then
			error("found unexpected pointer to struct")
		end
		assert(s_type.entries[1].field == "fn",
				"invalid partial structure")
		assert(s_type.entries[1].type:ispointertofunction(),
				"not a function pointer")
		local new_args = {}
		for i = 2,#s_type.entries-1 do -- -1 due to .invoke
			table.insert(new_args, `fn.[fname(i-1)]) 
		end
		for _, v in ipairs(args) do
			table.insert(new_args, v)
		end
		args = new_args
		fn_type = s_type.entries[1].type.type
		fn = `fn.fn
	else
		error("fn must be a function (or pointer to function)")
	end

	local pdata = terralib.types.newstruct(("util_partial_%s_%d"):format(
			fn.tree.name, next_id))
	next_id = next_id + 1
	pdata.entries:insert({
		field = "fn",
		type = &fn_type,
	})
	local inv_params,inv_rets = terralib.newlist(), fn_type.returntype
	for i, v in ipairs(fn_type.parameters) do
		if i > #args then
			inv_params:insert(v)
		else
			pdata.entries:insert({
				field = fname(i),
				type = v,
			})
		end
	end
	local partial_fn_type = inv_params -> inv_rets
	pdata.entries:insert({
		field = "invoke",
		type = partial_fn_type,
	})
	S.Object(pdata)
	
	local partial = global(&pdata)

	pdata.metamethods.__apply = macro(function(self, ...)
		return `self.invoke([{...}])
	end)
	pdata.metamethods.__cast = function(from,to,exp)
		if to:ispointertofunction() then
			if tostring(partial_fn_type) == tostring(to) then
				return quote
					S.assert(exp ~= nil)
				in
					exp.invoke
				end
			end
		end
		error(("invalid cast from %s to %s"):format(from, to))
	end
	pdata.methods.__destruct = terra(self: &pdata)
		partial = nil
	end

	local fn_args = {}
	for i, _ in ipairs(args) do
		table.insert(fn_args, `partial.[fname(i)])
	end
	local arg_syms = {}
	for _, v in ipairs(inv_params) do
		local s = symbol(v)
		table.insert(fn_args, s)
		table.insert(arg_syms, s)
	end
	
	local invoke = terra([arg_syms])
		return [partial].fn([fn_args])
	end

	local lpartial = symbol(&pdata)
	local stmts = {}
	table.insert(stmts, quote
		var [lpartial] = pdata.alloc()
		lpartial.fn = fn
		lpartial.invoke = invoke
	end)
	for i, v in ipairs(args) do
		table.insert(stmts, quote
			lpartial.[fname(i)] = v
		end)
	end
	return quote
		[stmts]
		[partial] = lpartial
	in
		lpartial
	end
end)

--[[
M.partial = macro(function(fn, ...)
	local fn_type
	if fn.tree.type:isfunction() then
		fn_type = fn.tree.type
	elseif fn.tree.type:ispointertofunction() then
		fn_type = fn.tree.type.type
	else
		error("fn must be a function (or pointer to function)")
	end

	local args = {...}
	local free_args = {}
	for i=#args+1,#fn_type.parameters do
		local s = symbol(fn_type.parameters[i])
		table.insert(free_args, s)
		table.insert(args, s)
	end
	return terra([free_args])
		return fn([args])
	end
end)
]]

local struct CString(S.Object) {
	ptr: &int8
	is_heap: bool,
}

M.CString = CString

CString.metamethods.__cast = function(from,to,exp)
	if to == &int8 then
		return `exp.ptr
	elseif from == &int8 and terralib.isconstant(exp) then
		return `CString { exp, false }
	end
	error(("invalid cast from %s to %s"):format(from, to))
end

do
	local obj = CString
	local terra obj_init(o: &obj, c_string: &int8, is_heap: bool)
		o.ptr = c_string
		o.is_heap = is_heap
	end
	local old_alloc = obj.methods.alloc
	obj.methods.alloc = terralib.overloadedfunction("alloc")
	obj.methods.alloc:adddefinition(old_alloc)
	obj.methods.alloc:adddefinition(terra(c_string: &int8, is_heap: bool)
		var o = old_alloc()
		obj_init(o, c_string, is_heap)
		return o
	end)
	local old_salloc = obj.methods.salloc
	obj.methods.salloc = macro(function(c_string, is_heap)
		if not c_string then
			c_string = `nil
			is_heap = `false
		else
			assert(is_heap, "is_heap is expected")
		end
		return quote
			var o = old_salloc()
			obj_init(o, c_string, is_heap)
		in
			o
		end
	end)
end

terra CString:__destruct()
	if self.is_heap then
		C.free(self.ptr)
	end
end

CString.methods.sprintf = macro(function(format, ...)
	assert(format:gettype() == &int8, "format must be string literal")
	assert(terralib.isconstant(format, "format must be a constant"))
	return quote
		var cstr = CString.alloc()
		C.asprintf(&cstr.ptr, format, [{...}])
		cstr.is_heap = true
	in
		cstr
	end
end)


local IError = golike.Interface({
	to_string = {} -> &CString
})

M.IError = IError

local struct inv_struct {}

M.inv_start = terralib.intrinsic("llvm.invariant.start",
		{int64,&int8} -> {&inv_struct})
M.inv_end = terralib.intrinsic("llvm.invariant.end",
		{&inv_struct,int64,&int8} -> {})

return M
