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
local talloc = require("talloc")
local errors = require("errors")
local inspect = require("inspect")
local primitive = require("primitive")
local golike = require("golike")

local IError = errors.IError
local CString = primitive.CString

local M = {}

local EFromLuaMapper = errors.new("FromLuaMapper")
local ECallFromLua = errors.new("CallFromLua")

local function classifytype(type)
    if "primitive" == type.kind then
        if type.type == "float" or type.type == "integer" and type.bytes < 8 then
            return "number"
        elseif type.type == "logical" then
            return "boolean"
        else -- number does not fit into Lua, it has to be userdata
            return "userdata"
        end
    else
        return "userdata"
    end
end

local LUA_TNIL           = 0
local LUA_TBOOLEAN       = 1
local LUA_TLIGHTUSERDATA = 2
local LUA_TNUMBER        = 3
local LUA_TSTRING        = 4
local LUA_TTABLE         = 5
local LUA_TFUNCTION      = 6
local LUA_TUSERDATA      = 7
local LUA_TTHREAD        = 8

M.TNIL           = LUA_TNIL
M.TBOOLEAN       = LUA_TBOOLEAN
M.TLIGHTUSERDATA = LUA_TLIGHTUSERDATA
M.TNUMBER        = LUA_TNUMBER
M.TSTRING        = LUA_TSTRING
M.TTABLE         = LUA_TTABLE
M.TFUNCTION      = LUA_TFUNCTION
M.TUSERDATA      = LUA_TUSERDATA
M.TTHREAD        = LUA_TTHREAD

local size_t = uint64

local struct lua_State

local LUA_MULTRET = -1

local lua_CFunction = {&lua_State} -> {int}

local struct luaL_Reg {
	name: rawstring
	func: lua_CFunction
}

local LUA_IDSIZE = 60

local struct lua_Debug {
	event: int
	name: rawstring
	namewhat: rawstring
	what: rawstring
	source: rawstring
	currentline: int
	nups: int
	linedefined: int
	lastlinedefined: int
	short_src: int8[LUA_IDSIZE]
	i_ci : int
}

local ef = terralib.externfunction

local luaL_checknumber = ef("luaL_checknumber", {&lua_State, int} -> double)
local lua_pushboolean = ef("lua_pushboolean", {&lua_State, bool} -> {})
local lua_pushvalue = ef("lua_pushvalue", {&lua_State, int} -> {})
local lua_pushnumber = ef("lua_pushnumber", {&lua_State, double} -> {})
local lua_toboolean = ef("lua_toboolean", {&lua_State, int} -> int)
local lua_newuserdata = ef("lua_newuserdata", {&lua_State, int} -> &opaque)
local lua_touserdata = ef("lua_touserdata", {&lua_State, int} -> &opaque)
local lua_getmetatable = ef("lua_getmetatable", {&lua_State, int} -> int)
local lua_setmetatable = ef("lua_setmetatable", {&lua_State, int} -> {})
local lua_rawequal = ef("lua_rawequal", {&lua_State, int, int} -> int)
local luaL_typerror = ef("luaL_typerror", {&lua_State, int, rawstring} -> {})
local lua_settop = ef("lua_settop", {&lua_State, int} -> {})
local lua_checkstack = ef("lua_checkstack", {&lua_State, int} -> {int})
local lua_pushcclosure = ef("lua_pushcclosure",
		{&lua_State, lua_CFunction, int} -> {})
local lua_setfield = ef("lua_setfield", {&lua_State, int, rawstring} -> {})
local lua_getstack = ef("lua_getstack", {&lua_State, int, &lua_Debug} -> {int})
local lua_getinfo = ef("lua_getinfo",
		{&lua_State, rawstring, &lua_Debug} -> {int})
local lua_pushlstring = ef("lua_pushlstring",
		{&lua_State, rawstring, size_t} -> {})
local lua_pushstring = ef("lua_pushstring", {&lua_State, rawstring} -> {})
local lua_concat = ef("lua_concat", {&lua_State, int} -> {})
local lua_error = ef("lua_error", {&lua_State} -> {int})
local luaL_checkstack = ef("luaL_checkstack",
		{&lua_State, int, rawstring} -> {})
local luaL_loadstring = ef("luaL_loadstring", {&lua_State, rawstring} -> {int})
local lua_call = ef("lua_call", {&lua_State, int, int} -> {int})
local lua_pcall = ef("lua_pcall", {&lua_State, int, int, int} -> {int})
local lua_type = ef("lua_type", {&lua_State, int} -> {int})
local lua_typename = ef("lua_typename", {&lua_State, int} -> {rawstring})
local lua_tonumber = ef("lua_tonumber", {&lua_State, int} -> {double})
local lua_strlen = ef("lua_strlen", {&lua_State, int} -> {size_t})
local lua_gettop = ef("lua_gettop", {&lua_State} -> {int})
local lua_settop = ef("lua_settop", {&lua_State, int} -> {})
local lua_pushvalue = ef("lua_pushvalue", {&lua_State, int} -> {})
local lua_remove = ef("lua_remove", {&lua_State, int} -> {})
local lua_insert = ef("lua_insert", {&lua_State, int} -> {})
local lua_replace = ef("lua_replace", {&lua_State, int} -> {})
local lua_tolstring = ef("lua_tolstring",
		{&lua_State, int, &size_t} -> {rawstring})
local luaL_where = ef("luaL_where", {&lua_State, int} -> {})
local lua_createtable = ef("lua_createtable", {&lua_State, int, int} -> {})
local lua_topointer = ef("lua_topointer", {&lua_State, int} -> {&opaque})
local lua_gettable = ef("lua_gettable", {&lua_State, int} -> {})
local lua_getfield = ef("lua_getfield", {&lua_State, int, rawstring} -> {})
local lua_rawget = ef("lua_rawget", {&lua_State, int} -> {})
local lua_rawgeti = ef("lua_rawgeti", {&lua_State, int, int} -> {})

terra lua_pop(L : &lua_State, n : int)
    lua_settop(L,-(n)-1)
end

local lua_pushliteral = macro(function(L, s)
	return `lua_pushlstring(L, ["" .. s], (sizeof(s)/sizeof(char))-1)
end)

terra lua_tostring(L : &lua_State, n : int)
	return lua_tolstring(L, n, nil)
end

terra lua_newtable(L : &lua_State)
	lua_createtable(L, 0, 0)
end

terra luaL_setfuncs(L: &lua_State, reg: &luaL_Reg, nup: int)
	var i: int

	luaL_checkstack(L, nup, "too many upvalues")
	while reg.name ~= nil do -- fill the table with given functions
		for i = 0,nup do -- copy upvalues to the top
			lua_pushvalue(L, -nup)
		end
		lua_pushcclosure(L, reg.func, nup) -- closure with those
		                                   -- upvalues
		lua_setfield(L, -(nup + 2), reg.name)
		reg = reg + 1
	end
	lua_pop(L, nup) -- remove upvalues
end

terra luaL_dostring(L: &lua_State, s: rawstring)
	var result = luaL_loadstring(L, s)
	if result ~= 0 then
		return result
	end
	return lua_pcall(L, 0, LUA_MULTRET, 0)
end

local LUA_REGISTRYINDEX = `(-10000)
local LUA_ENVIRONINDEX =  `(-10001)
local LUA_GLOBALSINDEX = `(-10002)

terra lua_upvalueindex(i : int)
	return LUA_GLOBALSINDEX - i
end

terra lua_setglobal(L: &lua_State, s: rawstring)
	lua_setfield(L, LUA_GLOBALSINDEX, s)
end

terra lua_getglobal(L: &lua_State, s: rawstring)
	lua_getfield(L, LUA_GLOBALSINDEX, s)
end

terra newterradata(L : &lua_State, sz : int, class : int) : &opaque
	var result = lua_newuserdata(L,sz)
	lua_pushvalue(L,class)
	lua_setmetatable(L,-2)
	return result
end

local to_lua_mappers = {}
local from_lua_mappers = {}

-- Low level API
local struct LuaState(talloc.Object) {
	L: &lua_State
}

M.LuaState = LuaState

terra LuaState:__init(L: &lua_State)
	self.L = L
end

terra LuaState:push_number(d: double)
	lua_pushnumber(self.L, d)
end

terra LuaState:push_string(s: rawstring)
	lua_pushstring(self.L, s)
end

terra LuaState:dostring(s: rawstring)
	luaL_dostring(self.L, s)
end

terra LuaState:get_type(index: int)
	return lua_type(self.L, index)
end

terra LuaState:gettop()
	return lua_gettop(self.L)
end

terra LuaState:settop(top: int)
	lua_settop(self.L, top)
end

terra LuaState:to_number(index: int)
	return lua_tonumber(self.L, index)
end

terra LuaState:to_string(index: int)
	return lua_tostring(self.L, index)
end

terra LuaState:get_strlen(index: int)
	return lua_strlen(self.L, index)
end

terra LuaState:to_boolean(index: int)
	return lua_toboolean(self.L, index) ~= 0
end

terra LuaState:get_typename(index: int)
	return lua_typename(self.L, index)
end

LuaState.methods.issue_stack_guard = macro(function(self)
	return quote
		var old_top = self:gettop()
		defer self:settop(old_top)
	end
end)

terra LuaState:dump_stack()
	var i: int
	var top = lua_gettop(self.L)
	S.printf("*** Lua stack ***\n")
	for i = 1,top+1 do
		var t = lua_type(self.L, i)
		if t == LUA_TSTRING then
			S.printf("%d: `%s'", i, lua_tolstring(self.L, i, nil))
		elseif t == LUA_TBOOLEAN then
			if lua_toboolean(self.L, i) ~= 0 then
				S.printf("%d: true", i)
			else
				S.printf("%d: false", i)
			end
		elseif t == LUA_TNUMBER then
			S.printf("%d: %g", i, lua_tonumber(self.L, i))
		else
			S.printf("%d: %s", i, lua_typename(self.L, t))
		end
		S.printf("\n")
	end
	S.printf("\n");
end

terra LuaState:raise_error()
	return lua_error(self.L)
end

terra LuaState:raise_error_with_location()
	luaL_where(self.L, 1)
	lua_pushvalue(self.L, -2)
	lua_concat(self.L, 2)
	return self:raise_error()
end

terra LuaState:new_table()
	lua_createtable(self.L, 0, 0)
end

terra LuaState:set_funcs(reg: &luaL_Reg, nup: int)
	luaL_setfuncs(self.L, reg, nup)
end

terra LuaState:get_global(name: rawstring)
	lua_getglobal(self.L, name)
end

terra LuaState:set_global(name: rawstring)
	lua_setglobal(self.L, name)
end

terra LuaState:get_table(index: int)
	lua_gettable(self.L, index)
end

terra LuaState:set_field(index: int, fname: rawstring)
	lua_setfield(self.L, index, fname)
end

terra LuaState:get_field(index: int, fname: rawstring)
	lua_getfield(self.L, index, fname)
end

talloc.complete_type(LuaState)

to_lua_mappers[int8] = LuaState.methods.push_number
to_lua_mappers[int16] = LuaState.methods.push_number
to_lua_mappers[int32] = LuaState.methods.push_number
to_lua_mappers[int64] = LuaState.methods.push_number
to_lua_mappers[uint8] = LuaState.methods.push_number
to_lua_mappers[uint16] = LuaState.methods.push_number
to_lua_mappers[uint32] = LuaState.methods.push_number
to_lua_mappers[uint64] = LuaState.methods.push_number
to_lua_mappers[rawstring] = LuaState.methods.push_string

local struct LuaValue {
	L: &LuaState
	index: int
}

M.LuaValue = LuaValue

terra LuaValue:is(t: int)
	return self.L:get_type(self.index) == t
end

terra LuaValue:is_nil()
	return self:is(LUA_TNIL)
end

terra LuaValue:is_boolean()
	return self:is(LUA_TBOOLEAN)
end

terra LuaValue:is_lightuserdata()
	return self:is(LUA_TLIGHTUSERDATA)
end

terra LuaValue:is_number()
	return self:is(LUA_TNUMBER)
end

terra LuaValue:is_string()
	return self:is(LUA_TSTRING)
end

terra LuaValue:is_table()
	return self:is(LUA_TTABLE)
end

terra LuaValue:is_function()
	return self:is(LUA_TFUNCTION)
end

terra LuaValue:is_userdata()
	return self:is(LUA_TUSERDATA)
end

terra LuaValue:is_thread()
	return self:is(LUA_TTHREAD)
end

terra LuaValue:to_number()
	return self.L:to_number(self.index)
end

terra LuaValue:to_string()
	return self.L:to_string(self.index)
end

terra LuaValue:dump()
	S.printf(">>> Lua value at %d: ", self.index)
	var t = self.L:get_type(self.index)
	if t == LUA_TSTRING then
		S.printf("`%s'", self.L:to_string(self.index))
	elseif t == LUA_TBOOLEAN then
		if self.L:to_boolean(self.index) then
			S.printf("true")
		else
			S.printf("false")
		end
	elseif t == LUA_TNUMBER then
		S.printf("%g", self.L:to_number(self.index))
	else
		S.printf("%s", self.L:get_typename(self.index))
	end
	S.printf("\n")
end

-- High level API
local struct Lua(talloc.Object) {
	L: &LuaState
}

M.Lua = Lua

terra Lua:__init(L: &LuaState)
	self.L = L
end

terra Lua:script(s: rawstring)
	self.L:dostring(s)
end

talloc.complete_type(Lua)

from_lua_mappers[rawstring] = terra(lvalue: LuaValue) : {rawstring, IError}
	if not lvalue:is_string() then
		return nil, EFromLuaMapper.talloc(nil,
				"string is expected")
	end
	return lvalue:to_string(), nil
end

function M.bind(fn)
	local stmts = terralib.newlist()
	local fn_type = fn:gettype()
	local lua_state = symbol(&LuaState)
	local lua = symbol(&Lua)
	local err = symbol(IError)
	local error_label = label()
	local args = terralib.newlist()
	args:insert(lua)
	for i, vt in ipairs(fn_type.parameters) do
		if i > 1 then
			local v = symbol(LuaValue)
			stmts:insert(quote
				var [v]
				v.L = lua_state
				v.index = i - 1
			end)
			assert(from_lua_mappers[vt],
					"no from_lua_mapper found for type " ..
					tostring(vt))
			args:insert(quote
				var result, e = [ from_lua_mappers[vt] ](v)
				if e ~= nil then
					[err] = e
					goto [error_label]
				end
			in
				result
			end)
		else
			assert(vt == &Lua, "first argument must be &Lua")
		end
	end
	local ret = symbol(fn_type.returntype)
	stmts:insert(quote var [ret] = fn([args]) end)
	local nret = 0
	if fn_type.returntype:isstruct() then
		for i, v in ipairs(fn_type.returntype.entries) do
			local field, type = v[1], v[2]
			assert(to_lua_mappers[type],
					"no to_lua_mapper found for type " ..
					tostring(type))
			stmts:insert(quote
				[ to_lua_mappers[type] ](lua_state, ret.[field])
			end)
			nret = nret + 1
		end
	else
		assert(to_lua_mappers[fn_type.returntype],
				"no to_lua_mapper found for type " ..
				tostring(fn_type.returntype))
		stmts:insert(quote
			[ to_lua_mappers[fn_type.returntype] ](lua_state, ret)
		end)
		nret = nret + 1
	end

	local terra wrapper(L : &lua_State) : int
		-- Caveat: Lua error thrown from this call will leak memory
		var ctx = talloc.new(nil)

		var [lua_state] = LuaState.talloc(ctx, L)
		var [lua] = Lua.talloc(ctx, lua_state)

		var [err]

		var nargs = lua_state:gettop()
		if nargs ~= [#fn_type.parameters - 1] then
			err = ECallFromLua.talloc(ctx,
					"invalid number of arguments: %d, \z
					expected exactly %d", nargs,
					[#fn_type.parameters - 1])
			goto [error_label]
		end

		[stmts]

		talloc.free(ctx)
		do return nret end

		::[error_label]::
		lua_state:push_string(err:string())
		err:free()
		talloc.free(ctx)
		return lua_state:raise_error()
	end
	--wrapper:printpretty()
	--wrapper:disas()
	return wrapper
end

local module = {}
local module_mt = { __index = module }

function module:add_function(name, fn)
	self.functions:insert({name = name, fn = fn})
end

function module:_compile_open_fn()
	local init = terralib.newlist()
	for _, fn_info in ipairs(self.functions) do
		local bound_fn = M.bind(fn_info.fn)
		init:insert(`luaL_Reg { fn_info.name, bound_fn })
	end
	return terra(L : &lua_State)
		var reg = arrayof(luaL_Reg, [init], luaL_Reg { nil, nil })
		lua_newtable(L)
		luaL_setfuncs(L, reg, 0)
		return 1
	end
end

function M.new_module(name)
	local mdl = {
		functions = terralib.newlist(),
		name = name
	}
	setmetatable(mdl, module_mt)
	return mdl
end

local bundle = {}
local bundle_mt = { __index = bundle }

function bundle:add_module(name, m)
	self.modules:insert({name = name, module = m})
end

function bundle:compile(name)
	local L = symbol(&lua_State)
	local stmts = terralib:newlist()
	for _, m in ipairs(self.modules) do
		local open_fn = m.module:_compile_open_fn()
		stmts:insert(quote
			lua_pushcclosure(L, open_fn, 0)
			lua_setfield(L, -2, m.name)
		end)
	end
	local terra luaopen([L])
		lua_getglobal(L, "package")
		lua_pushstring(L, "preload")
		lua_gettable(L, -2)

		[stmts]
	
		lua_newtable(L)
		return 1
	end
	
	terralib.saveobj("bundle.o", "object", {
		["luaopen_"..name] = luaopen
	}, {})
	
	os.execute("ld -shared bundle.o -o "..name..".so -llua5.1")
	os.execute("rm bundle.o")
end

function M.new_bundle()
	local bndl = {
		modules = terralib.newlist()
	}
	setmetatable(bndl, bundle_mt)
	return bndl
end

return M
