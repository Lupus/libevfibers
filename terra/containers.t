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

int wrap_trie_block_size() {
	return TOMMY_TRIE_BLOCK_SIZE;
}

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

function M.value_comparator(T, how)
	if how == nil then
		how = M.ASCENDING
	end
	if how == M.ASCENDING then
		return terra(a: &T, b: &T)
			if @a < @b then
				return -1
			elseif @a > @b then
				return 1
			else
				return 0
			end
		end
	elseif how == M.DESCENDING then
		return terra(a: &T, b: &T)
			if @a > @b then
				return -1
			elseif @a < @b then
				return 1
			else
				return 0
			end
		end
	else
		error("invalid sort order")
	end
end

function M.eq(T, field)
	return terra(a: &T, b: &T)
		if a.[field] == b.[field] then
			return 0
		else
			return -1
		end
	end
end

function M.field_key(T, field)
	return terra(s: &T)
		return [C.tommy_key_t](s.[field])
	end
end

function M.value_eq(T, field)
	return terra(a: &T, b: &T)
		if @a == @b then
			return 0
		else
			return -1
		end
	end
end

function M.hasher(T, field)
	local F
	for _, entry in ipairs(T.entries) do
		if entry.field == field then
			F = entry.type
		end
	end
	assert(F, "field not found")
	if F == int32 or F == uint32 then
		return terra(v: &T)
			return C.tommy_inthash_u32(v.[field])
		end
	else
		return terra(v: &T)
			return C.tommy_hash_u32(&v.[field], sizeof(v.[field]))
		end
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

local stack_defalt_options = {
}

function M.Stack(T, options)
	if not options then
		options = stack_defalt_options
	end
	local type_name = type_string_simple(T)
	local impl_name = ("StackImpl_%s"):format(type_name)
	local stack_impl = terralib.types.newstruct(impl_name)
	local TArray = M.Array(T, options.array_options)
	stack_impl.entries:insert({
		field = "array",
		type = &TArray,
	})
	stack_impl.entries:insert({
		field = "top",
		type = int,
	})
	talloc.Object(stack_impl)
	terra stack_impl:__init()
		self.top = -1
		self.array = TArray.talloc(self)
	end
	terra stack_impl:empty()
		return self.top == -1
	end
	terra stack_impl:count()
		return self.top + 1
	end
	terra stack_impl:push(value: T)
		self.top = self.top + 1
		self.array:grow(self.top + 1)
		self.array:set(self.top, value)
	end
	terra stack_impl:pop()
		var ret : T
		if self.top == -1 then
			return ret, false
		end
		ret = self.array:get(self.top)
		self.top = self.top - 1
		return ret, true
	end
	return stack_impl
end

local map_hook_default_options = {
	tag = "default",
}

function M.map_hook(options)
	if not options then
		options = map_hook_default_options
	end
	local hook = {}
	hook.options = options
	hook.fld = function(name)
		return ("_map_%s_%s"):format(options.tag, name)
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


local function pair_type(K, V, hook)
	local key_type_name = type_string_simple(K)
	local value_type_name = type_string_simple(V)
	local impl_name = ("PairImpl_%s_%s"):format(key_type_name,
			value_type_name)
	local pair_impl = terralib.types.newstruct(impl_name)
	pair_impl.entries:insert({
		field = "_1",
		type = K,
	})
	pair_impl.entries:insert({
		field = "_2",
		type = V,
	})
	hook.Base(pair_impl)
	pair_impl.metamethods.__gt = terra(a: &pair_impl, b: &pair_impl)
		return a._1 > b._1
	end
	pair_impl.metamethods.__lt = terra(a: &pair_impl, b: &pair_impl)
		return a._1 < b._1
	end
	return pair_impl
end

function M.KVMap(K, V, cmp)
	local map_hook = M.map_hook()
	local T = pair_type(K, V, map_hook)

	if not cmp then
		cmp = M.value_comparator(T)
	end

	local tommy_prefix = "tommy_tree"
	local tommy_type = C[tommy_prefix]
	local method = function(name)
		return C[("%s_%s"):format(tommy_prefix, name)]
	end
	local impl_name = ("KVMapImpl_%s_%s"):format(type_string_simple(K),
			type_string_simple(V))
	local map_impl = terralib.types.newstruct(impl_name)
	map_impl.entries:insert({
		field = "tm_map",
		type = tommy_type,
	})
	talloc.Object(map_impl)
	map_impl.metamethods.__cast = function(from,to,exp)
		if to:ispointer() and to.type == tommy_type then
			return `&exp.tm_map
		end
		error(("invalid cast from %s to %s"):format(from, to))
	end

	terra map_impl:__init()
		[method("init")](self, [&C.tommy_compare_func](cmp))
	end
	local node = macro(function(item)
		return `&item.[map_hook.fld("node")]
	end)
	terra map_impl:insert(key: K, value: V)
		var item = T.talloc(self)
		item._1 = key
		item._2 = value
		[method("insert")](self, node(item), item)
	end
	terra map_impl:search(key: K)
		var ret : V
		var item : T
		item._1 = key
		var ptr = [&T]([method("search")](self, &item))
		if ptr ~= nil then
			ret = ptr._2
			return ret, true
		else
			return ret, false
		end
	end
	terra map_impl:remove(key: K)
		var item : T
		item._1 = key
		var ptr = [&T]([method("search")](self, &item))
		if ptr ~= nil then
			[method("remove_existing")](self, node(ptr))
			ptr:free()
		end
	end
	terra map_impl:count()
		return [method("count")](self)
	end
	terra map_impl:memory_usage()
		return [method("memory_usage")](self)
	end
	map_impl.metamethods.__for = function(self, body)
		local NodeStack = M.Stack(&C.tommy_node)
		return quote
			var s = NodeStack.talloc(&self)
			defer s:free()
			var current = self.tm_map.root
			while true do
				if current ~= nil then
					s:push(current)
					current = current.prev
				else
					var ok : bool
					current, ok = s:pop()
					if not ok then
						break
					end
					var value = [&T](current.data)
					var n = current.next
					[body(`value._1, `value._2)]
					current = n
				end
			end
		end
	end
	return map_impl
end

function M.Map(T, hook, cmp)
	if not cmp then
		cmp = M.value_comparator(T)
	end

	local tommy_prefix = "tommy_tree"
	local tommy_type = C[tommy_prefix]
	local method = function(name)
		return C[("%s_%s"):format(tommy_prefix, name)]
	end
	local type_name = type_string_simple(T)
	local impl_name = ("MapImpl_%s_%s"):format(type_name, hook.options.tag)
	local map_impl = terralib.types.newstruct(impl_name)
	map_impl.entries:insert({
		field = "tm_map",
		type = tommy_type,
	})
	talloc.Object(map_impl)
	map_impl.metamethods.__cast = function(from,to,exp)
		if to:ispointer() and to.type == tommy_type then
			return `&exp.tm_map
		end
		error(("invalid cast from %s to %s"):format(from, to))
	end

	terra map_impl:__init()
		[method("init")](self, [&C.tommy_compare_func](cmp))
	end
	local node = macro(function(item)
		return `&item.[hook.fld("node")]
	end)
	terra map_impl:insert(item: &T)
		[method("insert")](self, node(item), item)
	end
	terra map_impl:remove(item: &T)
		[method("remove")](self, item)
	end
	terra map_impl:search(item: &T)
		[method("search")](self, item)
	end
	terra map_impl:remove_existing(item: &T)
		[method("remove_existing")](self, node(item))
	end
	terra map_impl:count()
		return [method("count")](self)
	end
	terra map_impl:memory_usage()
		return [method("memory_usage")](self)
	end
	map_impl.metamethods.__for = function(self, body)
		local NodeStack = M.Stack(&C.tommy_node)
		return quote
			var s = NodeStack.talloc(&self)
			defer s:free()
			var current = self.tm_map.root
			while true do
				if current ~= nil then
					s:push(current)
					current = current.prev
				else
					var ok : bool
					current, ok = s:pop()
					if not ok then
						break
					end
					var value = [&T](current.data)
					var n = current.next
					[body(value)]
					current = n
				end
			end
		end
	end
	return map_impl
end

local unordered_map_defalt_options = {
	flavour = "hashdyn",
}

function M.KVUnorderedMap(K, V, hash, eq, options)
	local map_hook = M.map_hook()
	local T = pair_type(K, V, map_hook)

	if not options then
		options = unordered_map_defalt_options
	end
	if not hash then
		hash = M.hasher(T, "_1")
	end
	if not eq then
		eq = M.eq(T, "_1")
	end
	assert(hash, "hash is required")
	assert(eq, "eq is required")
	local tommy_prefix
	if options.flavour == "hashdyn" then
		tommy_prefix = "tommy_hashdyn"
	elseif options.flavour == "hashlin" then
		tommy_prefix = "tommy_hashlin"
	else
		error("invalid map flavour: should be one of: ....")
	end
	local tommy_type = C[tommy_prefix]
	local method = function(name)
		return C[("%s_%s"):format(tommy_prefix, name)]
	end
	local type_name = type_string_simple(T)
	local impl_name = ("KVUnorderedMapImpl_%s_%s_%s"):format(
			type_string_simple(K), type_string_simple(V),
			options.flavour)
	local map_impl = terralib.types.newstruct(impl_name)
	map_impl.entries:insert({
		field = "tm_map",
		type = tommy_type,
	})
	talloc.Object(map_impl)
	map_impl.metamethods.__cast = function(from,to,exp)
		if to:ispointer() and to.type == tommy_type then
			return `&exp.tm_map
		end
		error(("invalid cast from %s to %s"):format(from, to))
	end

	if options.flavour == "hashdyn" or options.flavour == "hashlin" then
		terra map_impl:__init()
			[method("init")](self)
		end
	end
	terra map_impl:__destruct()
		[method("done")](self)
	end
	local node = macro(function(item)
		return `&item.[map_hook.fld("node")]
	end)
	terra map_impl:insert(key: K, value: V)
		var item = T.talloc(self)
		item._1 = key
		item._2 = value
		[method("insert")](self, node(item), item, hash(item))
	end
	terra map_impl:search(key: K)
		var ret : V
		var item : T
		item._1 = key
		var sf = [&C.tommy_search_func](eq)
		var ptr = [&T]([method("search")](self, sf, &item,
				hash(&item)))
		if ptr ~= nil then
			ret = ptr._2
			return ret, true
		else
			return ret, false
		end
	end
	terra map_impl:remove(key: K)
		var item : T
		item._1 = key
		var sf = [&C.tommy_search_func](eq)
		var ptr = [&T]([method("remove")](self, sf, &item,
				hash(&item)))
		if ptr ~= nil then
			ptr:free()
		end
	end
	terra map_impl:count()
		return [method("count")](self)
	end
	terra map_impl:memory_usage()
		return [method("memory_usage")](self)
	end
	return map_impl
end

function M.UnorderedMap(T, hook, hash, eq, options)
	if not options then
		options = unordered_map_defalt_options
	end
	assert(hash, "hash is required")
	assert(eq, "eq is required")
	local tommy_prefix
	if options.flavour == "hashdyn" then
		tommy_prefix = "tommy_hashdyn"
	elseif options.flavour == "hashlin" then
		tommy_prefix = "tommy_hashlin"
	else
		error("invalid map flavour: should be one of: ....")
	end
	local tommy_type = C[tommy_prefix]
	local method = function(name)
		return C[("%s_%s"):format(tommy_prefix, name)]
	end
	local type_name = type_string_simple(T)
	local impl_name = ("UnorderedMapImpl_%s_%s_%s"):format(type_name,
			options.flavour, hook.options.tag)
	local map_impl = terralib.types.newstruct(impl_name)
	map_impl.entries:insert({
		field = "tm_map",
		type = tommy_type,
	})
	talloc.Object(map_impl)
	map_impl.metamethods.__cast = function(from,to,exp)
		if to:ispointer() and to.type == tommy_type then
			return `&exp.tm_map
		end
		error(("invalid cast from %s to %s"):format(from, to))
	end

	if options.flavour == "hashdyn" or options.flavour == "hashlin" then
		terra map_impl:__init()
			[method("init")](self)
		end
	end
	terra map_impl:__destruct()
		[method("done")](self)
	end
	local node = macro(function(item)
		return `&item.[hook.fld("node")]
	end)
	terra map_impl:insert(item: &T)
		[method("insert")](self, node(item), item, hash(item))
	end
	terra map_impl:remove(item: &T)
		var sf = [&C.tommy_search_func](eq)
		[method("remove")](self, sf, item, hash(item))
	end
	terra map_impl:search(item: &T)
		var sf = [&C.tommy_search_func](eq)
		[method("search")](self, sf, item, hash(item))
	end
	terra map_impl:remove_existing(item: &T)
		[method("remove_existing")](self, node(item))
	end
	terra map_impl:count()
		return [method("count")](self)
	end
	terra map_impl:memory_usage()
		return [method("memory_usage")](self)
	end
	if options.flavour == "hashdyn" then
	elseif options.flavour == "hashlin" then
	end
	return map_impl
end

function M.KVTrieMap(V)
	local K = C.tommy_key_t
	local map_hook = M.map_hook()
	local T = pair_type(K, V, map_hook)

	local tommy_prefix = "tommy_trie"
	local tommy_type = C[tommy_prefix]
	local method = function(name)
		return C[("%s_%s"):format(tommy_prefix, name)]
	end
	local type_name = type_string_simple(T)
	local impl_name = ("KVTrieMapImpl_%s"):format(type_string_simple(V))
	local map_impl = terralib.types.newstruct(impl_name)
	map_impl.entries:insert({
		field = "tm_map",
		type = tommy_type,
	})
	map_impl.entries:insert({
		field = "tm_alloc",
		type = C.tommy_allocator,
	})
	talloc.Object(map_impl)
	map_impl.metamethods.__cast = function(from,to,exp)
		if to:ispointer() and to.type == tommy_type then
			return `&exp.tm_map
		end
		error(("invalid cast from %s to %s"):format(from, to))
	end

	terra map_impl:__init()
		C.tommy_allocator_init(&self.tm_alloc,
				C.wrap_trie_block_size(),
				C.wrap_trie_block_size())
		[method("init")](self, &self.tm_alloc)
	end
	terra map_impl:__destruct()
		C.tommy_allocator_done(&self.tm_alloc)
	end
	local node = macro(function(item)
		local ntype = C[tommy_prefix.."_node"]
		return `[&ntype](&item.[map_hook.fld("node")])
	end)
	terra map_impl:insert(key: K, value: V)
		var item = T.talloc(self)
		item._1 = key
		item._2 = value
		[method("insert")](self, node(item), item, key)
	end
	terra map_impl:search(key: K)
		var ret : V
		var ptr = [&T]([method("search")](self, key))
		if ptr ~= nil then
			ret = ptr._2
			return ret, true
		else
			return ret, false
		end
	end
	terra map_impl:remove(key: K)
		var ptr = [&T]([method("remove")](self, key))
		if ptr ~= nil then
			ptr:free()
		end
	end
	terra map_impl:count()
		return [method("count")](self)
	end
	terra map_impl:memory_usage()
		return [method("memory_usage")](self)
	end
	return map_impl
end

function M.TrieMap(T, hook, key_func)
	local tommy_prefix = "tommy_trie"
	local tommy_type = C[tommy_prefix]
	local method = function(name)
		return C[("%s_%s"):format(tommy_prefix, name)]
	end
	local type_name = type_string_simple(T)
	local impl_name = ("TrieMapImpl_%s"):format(type_string_simple(T))
	local map_impl = terralib.types.newstruct(impl_name)
	map_impl.entries:insert({
		field = "tm_map",
		type = tommy_type,
	})
	map_impl.entries:insert({
		field = "tm_alloc",
		type = C.tommy_allocator,
	})
	talloc.Object(map_impl)
	map_impl.metamethods.__cast = function(from,to,exp)
		if to:ispointer() and to.type == tommy_type then
			return `&exp.tm_map
		end
		error(("invalid cast from %s to %s"):format(from, to))
	end

	terra map_impl:__init()
		C.tommy_allocator_init(&self.tm_alloc,
				C.wrap_trie_block_size(),
				C.wrap_trie_block_size())
		[method("init")](self, &self.tm_alloc)
	end
	terra map_impl:__destruct()
		C.tommy_allocator_done(&self.tm_alloc)
	end
	local node = macro(function(item)
		return `&item.[hook.fld("node")]
	end)
	terra map_impl:insert(item: &T)
		[method("insert")](self, node(item), item, key_func(item))
	end
	terra map_impl:remove(item: &T)
		[method("remove")](self, key_func(item))
	end
	terra map_impl:search(item: &T)
		return [&T]([method("search")](self, key_func(item)))
	end
	terra map_impl:remove_existing(item: &T)
		[method("remove_existing")](self, node(item))
	end
	terra map_impl:count()
		return [method("count")](self)
	end
	terra map_impl:memory_usage()
		return [method("memory_usage")](self)
	end
	return map_impl
end

return M
