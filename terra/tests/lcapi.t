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

local lunatest = require("lunatest")
local inspect = require("inspect")
local lcapi = require("lcapi")

local suite = {}

--------------------------------------------------------------------------------
-- Tests initialization
--------------------------------------------------------------------------------

function suite.setup()
end

function suite.teardown()
end

--------------------------------------------------------------------------------
-- Tests
--------------------------------------------------------------------------------

local terra run_script(lua: &lcapi.Lua)
	return lua:script("return 'Hello, world!'")
end

local terra echo_func(lua: &lcapi.Lua, str: rawstring)
	return str
end

local m = lcapi.new_module()
m:add_function("run_script", run_script)
m:add_function("echo_func", echo_func)

local b = lcapi.new_bundle()
b:add_module("lcapi_test", m)
b:compile("foo")

require("foo")

local lcapi_test = require("lcapi_test")

function suite.test_run_script()
	lunatest.assert_function(lcapi_test.run_script)
	lcapi_test.run_script()
end

function suite.test_echo_func()
	lunatest.assert_function(lcapi_test.echo_func)
	local x = lcapi_test.echo_func("Hello, world!")
	lunatest.assert_equal("Hello, world!", x)
end

	--[[
	local struct Accumulator(lcapi.LuaObject) {
		state: int
	}
	terra Accumulator:__init(val: int)
		self.state = val
	end
	terra Accumulator:add(val: int)
		self.state = self.state + val
	end
	terra Accumulator:sub(val: int)
		self.state = self.state - val
	end
	terra Accumulator:get_value()
		return self.state
	end
	
	lcapi.complete_lua_object(Accumulator)
	]]

return suite
