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

local C = terralib.includecstring[[

#define tommy_inline

#include "tommyds/tommyds/tommy.h"
#include "tommyds/tommyds/tommy.c"

]]

local M = {}

local function type_string_simple(T)
	if T:ispointer() then
		return ("ptr_to_%s"):format(T.type)
	else
		return ("%s"):format(T)
	end
end

M.ASCENDING = "ascending"
M.DESCENDING = "descending"

function M.comparator(T, field, how)
	if how == nil then
		how = M.ASCENDING
	end
	if how == M.ASCENDING then
		return terra(a: &T, b: &T)
			if a.[field] < b.[field] then
				return -1
			elseif a.[field] > b.[field] then
				return 1
			else
				return 0
			end
		end
	elseif how == M.DESCENDING then
		return terra(a: &T, b: &T)
			if a.[field] > b.[field] then
				return -1
			elseif a.[field] < b.[field] then
				return 1
			else
				return 0
			end
		end
	else
		error("invalid sort order")
	end
end

local list_defalt_options = {
	tag = "default",
}

function M.list_hook(options)
	if not options then
		options = list_defalt_options
	end
	local hook = {}
	hook.options = options
	hook.fld = function(name)
		return ("_list_%s_%s"):format(options.tag, name)
	end
	hook.Base = function(T)
		T.entries:insert({
			field = hook.fld("node"),
			type = C.tommy_node,
		})
		talloc.Object(T)
	end
	return hook
end

function M.List(T, hook)
	local type_name = type_string_simple(T)
	local impl_name = ("ListImpl_%s_%s"):format(type_name, hook.options.tag)
	local list_impl = terralib.types.newstruct(impl_name)
	list_impl.entries:insert({
		field = "tm_list",
		type = C.tommy_list,
	})
	talloc.Object(list_impl)
	list_impl.metamethods.__cast = function(from,to,exp)
		if to:ispointer() and to.type == C.tommy_list then
			return `&exp.tm_list
		end
		error(("invalid cast from %s to %s"):format(from, to))
	end
	terra list_impl:__init()
		C.tommy_list_init(self)
	end
	terra list_impl:head()
		return [&T](C.tommy_list_head(self).data)
	end
	terra list_impl:tail()
		return [&T](C.tommy_list_tail(self).data)
	end
	local node = macro(function(item)
		return `&item.[hook.fld("node")]
	end)
	terra list_impl:insert_head(item: &T)
		C.tommy_list_insert_head(self, node(item), item)
	end
	terra list_impl:insert_tail(item: &T)
		C.tommy_list_insert_tail(self, node(item), item)
	end
	terra list_impl:remove_existing(item: &T)
		C.tommy_list_remove_existing(self, node(item))
	end
	terra list_impl:concat(other: &list_impl)
		C.tommy_list_concat(self, other)
	end
	terra list_impl:sort(cmp: {&T, &T} -> int)
		C.tommy_list_sort(self, [&C.tommy_compare_func](cmp))
	end
	terra list_impl:empty()
		return 1 == C.tommy_list_empty(self)
	end
	terra list_impl:count()
		return C.tommy_list_count(self)
	end
	list_impl.metamethods.__for = function(self, body)
		return quote
			var node = C.tommy_list_head(self)
			while node ~= nil do
				var item = [&T](node.data)
				node = node.next
				[body(item)]
			end
		end
	end
	return list_impl
end

local array_defalt_options = {
	flavour = "exp",
}

function M.Array(T, options)
	if not options then
		options = array_defalt_options
	end
	local tommy_prefix
	if options.flavour == "exp" then
		if T:ispointer() then
			tommy_prefix = "tommy_array"
		else
			tommy_prefix = "tommy_arrayof"
		end
	elseif options.flavour == "blk" then
		if T:ispointer() then
			tommy_prefix = "tommy_arrayblk"
		else
			tommy_prefix = "tommy_arrayblkof"
		end
	else
		error("invalid array flavour: should be 'exp' or 'blk'")
	end
	local tommy_type = C[tommy_prefix]
	local method = function(name)
		return C[("%s_%s"):format(tommy_prefix, name)]
	end
	local type_name = type_string_simple(T)
	local impl_name = ("ArrayImpl_%s_%s"):format(type_name, options.flavour)
	local array_impl = terralib.types.newstruct(impl_name)
	array_impl.entries:insert({
		field = "tm_array",
		type = tommy_type,
	})
	talloc.Object(array_impl)
	array_impl.metamethods.__cast = function(from,to,exp)
		if to:ispointer() and to.type == tommy_type then
			return `&exp.tm_array
		end
		error(("invalid cast from %s to %s"):format(from, to))
	end

	if T:ispointer() then
		terra array_impl:__init()
			[method("init")](self)
		end
	else
		terra array_impl:__init()
			[method("init")](self, sizeof(T))
		end
	end
	terra array_impl:__destruct()
		[method("done")](self)
	end
	terra array_impl:grow(size: C.tommy_count_t)
		[method("grow")](self, size)
	end
	terra array_impl:ref(pos: C.tommy_count_t)
		return [&T]([method("ref")](self, pos))
	end
	terra array_impl:size()
		return [method("size")](self)
	end
	if T:ispointer() then
		terra array_impl:set(pos: C.tommy_count_t, value: T)
			[method("set")](self, pos, value)
		end
		terra array_impl:get(pos: C.tommy_count_t)
			return [T]([method("get")](self, pos))
		end
		terra array_impl:insert(value: T)
			[method("insert")](self, value)
		end
	else
		terra array_impl:set(pos: C.tommy_count_t, value: T)
			@self:ref(pos) = value
		end
		terra array_impl:get(pos: C.tommy_count_t)
			return @self:ref(pos)
		end
		terra array_impl:insert(value: T)
			var pos = self:size()
			self:grow(pos + 1)
			self:set(pos, value)
		end
	end
	terra array_impl:memory_usage()
		return [method("memory_usage")](self)
	end
	array_impl.metamethods.__for = function(self, body)
		return quote
			for i = 0, self:size() do
				var value = self:get(i)
				[body(i, value)]
			end
		end
	end
	return array_impl
end

return M
