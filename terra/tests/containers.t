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
local check = require("check")
local talloc = require("talloc")
local containers = require("containers")

local list_hook = containers.list_hook({tag = "custom"})

local struct Foo(list_hook.Base) {
	i: int
}

local foo_cmp = containers.comparator(Foo, "i", containers.DESCENDING)

local FooList = containers.List(Foo, list_hook)

terra test_list(i: int, ctx: &opaque)
	var list = FooList.talloc(ctx)
	check.assert(list:empty() == true)
	var foo1 = Foo.talloc(list)
	foo1.i = 10
	var foo2 = Foo.talloc(list)
	foo2.i = 20
	list:insert_tail(foo1)
	check.assert(list:empty() == false)
	check.assert(list:count() == 1)
	list:insert_tail(foo2)
	check.assert(list:count() == 2)
	check.assert(list:head().i == 10)
	check.assert(list:tail().i == 20)
	var sum = 0
	for foo in list do
		sum = sum + foo.i
	end
	check.assert(sum == 30)
	list:sort(foo_cmp)
	check.assert(list:head().i == 20)
	check.assert(list:tail().i == 10)
	list:remove_existing(foo1)
	check.assert(list:count() == 1)
end

local FooPtrArray = containers.Array(&Foo)

terra test_array(i: int, ctx: &opaque)
	var foo_ptr_arr = FooPtrArray.talloc(ctx)
	check.assert(foo_ptr_arr:size() == 0)
	var foo1 = Foo.talloc(foo_ptr_arr)
	foo1.i = 10
	var foo2 = Foo.talloc(foo_ptr_arr)
	foo2.i = 20
	foo_ptr_arr:insert(foo1)
	check.assert(foo_ptr_arr:size() == 1)
	foo_ptr_arr:insert(foo2)
	check.assert(foo_ptr_arr:size() == 2)
	var sum = 0
	for i, v in foo_ptr_arr do
		sum = sum + v.i
	end
	check.assert(sum == 30)
	var foo3 = Foo.talloc(foo_ptr_arr)
	foo3.i = 30
	foo_ptr_arr:set(0, foo3)
	check.assert(foo_ptr_arr:get(0).i == foo3.i)
end

local FooArray = containers.Array(Foo, {flavour = "blk"})

terra test_array_by_value(i: int, ctx: &opaque)
	var foo_arr = FooArray.talloc(ctx)
	check.assert(foo_arr:size() == 0)
	var foo1 : Foo
	foo1.i = 10
	var foo2 : Foo
	foo2.i = 20
	foo_arr:insert(foo1)
	check.assert(foo_arr:size() == 1)
	foo_arr:insert(foo2)
	check.assert(foo_arr:size() == 2)
	var sum = 0
	for i, v in foo_arr do
		sum = sum + v.i
	end
	check.assert(sum == 30)
	var foo3 : Foo
	foo3.i = 30
	foo_arr:set(0, foo3)
	check.assert(foo_arr:get(0).i == foo3.i)
end

local IntStack = containers.Stack(int)

terra test_stack(i: int, ctx: &opaque)
	var istack = IntStack.talloc(ctx)
	check.assert(istack:count() == 0)
	check.assert(istack:empty())
	var val, ok = istack:pop()
	check.assert(not ok)
	for i = 0,10 do
		istack:push(i)
		check.assert(istack:count() == i + 1)
	end
	for i = 9,-1,-1 do
		var x, _ = istack:pop()
		check.assert(x == i)
		check.assert(istack:count() == i)
	end
	check.assert(istack:count() == 0)
	check.assert(istack:empty())
end

local map_hook = containers.map_hook({tag = "custom2"})

local struct Bar(map_hook.Base) {
	key: int
	value: int
}

Bar.metamethods.__gt = terra(a: &Bar, b: &Bar)
	return a.key > b.key
end

Bar.metamethods.__lt = terra(a: &Bar, b: &Bar)
	return a.key < b.key
end

local BarMap = containers.Map(Bar, map_hook)

terra test_map(i: int, ctx: &opaque)
	var map = BarMap.talloc(ctx)
	check.assert(map:count() == 0)
	var bar1 = Bar.talloc(map)
	bar1.key = 1
	bar1.value = 10
	map:insert(bar1)
	check.assert(map:count() == 1)
	var bar2 = Bar.talloc(map)
	bar2.key = 2
	bar2.value = 20
	map:insert(bar2)
	check.assert(map:count() == 2)
	var j = 1
	for bar in map do
		check.assert(bar.key == j)
		check.assert(bar.value == j * 10)
		j = j + 1
	end
end

local IntIntMap = containers.KVMap(int, int)

terra test_kv_map(i: int, ctx: &opaque)
	var map = IntIntMap.talloc(ctx)
	check.assert(map:count() == 0)
	map:insert(1, 10)
	check.assert(map:count() == 1)
	map:insert(2, 20)
	check.assert(map:count() == 2)
	var j = 1
	for k, v in map do
		check.assert(k == j)
		check.assert(v == j * 10)
		j = j + 1
	end
	var v, ok = map:search(5)
	check.assert(ok == false)
	v, ok = map:search(2)
	check.assert(v == 20)
	map:remove(2)
	check.assert(map:count() == 1)
end

local umap_tests = {}
local umap_flavours = {"hashdyn", "hashlin"}

for x, flavour in ipairs(umap_flavours) do
	local BarUMap = containers.UnorderedMap(Bar, map_hook,
			containers.hasher(Bar, "key"),
			containers.eq(Bar, "key"),
			{
				flavour = flavour,
			})

	local terra test_unordered_map(i: int, ctx: &opaque)
		var map = BarUMap.talloc(ctx)
		check.assert(map:count() == 0)
		var bar1 = Bar.talloc(map)
		bar1.key = 1
		bar1.value = 10
		map:insert(bar1)
		check.assert(map:count() == 1)
		var bar2 = Bar.talloc(map)
		bar2.key = 2
		bar2.value = 20
		map:insert(bar2)
		check.assert(map:count() == 2)
	end

	local IntIntUMap = containers.KVUnorderedMap(int, int, nil, nil, {
		flavour = flavour,
	})

	local terra test_unordered_pair_map(i: int, ctx: &opaque)
		var map = IntIntUMap.talloc(ctx)
		check.assert(map:count() == 0)
		map:insert(1, 10)
		check.assert(map:count() == 1)
		map:insert(2, 20)
		check.assert(map:count() == 2)
		var v, ok = map:search(5)
		check.assert(ok == false)
		v, ok = map:search(2)
		check.assert(v == 20)
		map:remove(2)
		check.assert(map:count() == 1)
	end

	table.insert(umap_tests, {
		flavour = flavour,
		test_map = test_unordered_map,
		test_pair_map = test_unordered_pair_map,
	})
end

local BarTrieMap = containers.TrieMap(Bar, map_hook,
		containers.field_key(Bar, "key"))

terra test_trie_map(i: int, ctx: &opaque)
	var map = BarTrieMap.talloc(ctx)
	check.assert(map:count() == 0)
	var bar1 = Bar.talloc(map)
	bar1.key = 1
	bar1.value = 10
	map:insert(bar1)
	check.assert(map:count() == 1)
	var bar2 = Bar.talloc(map)
	bar2.key = 2
	bar2.value = 20
	map:insert(bar2)
	check.assert(map:count() == 2)
end

local IntIntTrieMap = containers.KVTrieMap(int)

terra test_trie_pair_map(i: int, ctx: &opaque)
	var map = IntIntTrieMap.talloc(ctx)
	check.assert(map:count() == 0)
	map:insert(1, 10)
	check.assert(map:count() == 1)
	map:insert(2, 20)
	check.assert(map:count() == 2)
	var v, ok = map:search(5)
	check.assert(ok == false)
	v, ok = map:search(2)
	check.assert(v == 20)
	map:remove(2)
	check.assert(map:count() == 1)
end

local twrap = macro(function(test_fn)
	return terra(i: int)
		var ctx = talloc.new(nil)
		test_fn(i, ctx)
		ctx:free()
	end
end)

local function add_umap_tests(tc)
	local umap_test_stmts =  terralib.newlist()
	for _, test in ipairs(umap_tests) do
		umap_test_stmts:insert(`tc:add_test(twrap(test.test_map)))
		umap_test_stmts:insert(`tc:add_test(twrap(test.test_pair_map)))
	end
	return umap_test_stmts
end

return {
	tcase = terra()
		var tc = check.TCase.alloc("containers")
		tc:add_test(twrap(test_list))
		tc:add_test(twrap(test_array))
		tc:add_test(twrap(test_array_by_value))
		tc:add_test(twrap(test_stack))
		tc:add_test(twrap(test_map))
		tc:add_test(twrap(test_kv_map))
		[add_umap_tests(tc)]
		tc:add_test(twrap(test_trie_map))
		tc:add_test(twrap(test_trie_pair_map))
		return tc
	end
}
