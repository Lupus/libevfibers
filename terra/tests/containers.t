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

local FooArray = containers.Array(Foo)

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

local twrap = macro(function(test_fn)
	return terra(i: int)
		var ctx = talloc.new(nil)
		test_fn(i, ctx)
		ctx:free()
	end
end)

return {
	tcase = terra()
		var tc = check.TCase.alloc("containers")
		tc:add_test(twrap(test_list))
		tc:add_test(twrap(test_array))
		tc:add_test(twrap(test_array_by_value))
		return tc
	end
}
