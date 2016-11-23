--[[

   Copyright (C) 2013 Stanford University.
   All rights reserved.
   
   Permission is hereby granted, free of charge, to any person obtaining a copy of
   this software and associated documentation files (the "Software"), to deal in
   the Software without restriction, including without limitation the rights to
   use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
   the Software, and to permit persons to whom the Software is furnished to do so,
   subject to the following conditions:
   
   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.
   
   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
   FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
   COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
   
   ]]

local S = require("std")

local M = {}

local struct This {}

M.This = This

local interface = {}
interface.__index = interface

local defined = {}

local function castmethod(from,to,exp)
	if to:isstruct() and from:ispointertostruct() then
		local self = defined[to]
		if not self then error("not a interface") end
		local cst = self:createcast(from.type,exp)
		return cst
	elseif to:isstruct() and from == niltype then
		return `to { nil, nil }
	elseif from:isstruct() and to == &opaque then
		return `exp.obj
	end
	error("invalid cast")
end

function M.Interface(methods)
	local self = setmetatable({}, interface)
	self.vtabletype = terralib.types.newstruct("vtable")
	struct self.type {
		vtable: &self.vtabletype
		obj: &opaque
	}
	defined[self.type] = self
	self.type.metamethods.__cast = castmethod

	self.implementedtypes = {} 
	
	self.methods = terralib.newlist()
	-- We assume interfaced objects to have talloc metatable
	methods["free"] = {} -> {}
	for k,v in pairs(methods) do
		-- print(k," = ",v)
		assert(v:ispointer() and v.type:isfunction())
		if v.type.returntype == This then
			v.type.returntype = self.type
		end
		local params,rets = terralib.newlist{&opaque}, v.type.returntype
		local syms = terralib.newlist()
		for i,p in ipairs(v.type.parameters) do
			params:insert(p)
			syms:insert(symbol(p))
		end
		local typ = params -> rets
		self.methods:insert({name = k, type = typ, syms = syms})
		self.vtabletype.entries:insert { field = k, type = &opaque }
	end

	for _,m in ipairs(self.methods) do
		self.type.methods[m.name] = terra(interface : &self.type, [m.syms])
			var fn = m.type(interface.vtable.[m.name])
			return fn(interface.obj,[m.syms])
		end
	end

	self.type.metamethods.__eq = macro(function(a, b)
		local iface
		local with
		if a:gettype() == self.type then
			iface = a
			with = b
		else
			iface = b
			with = a
		end
		if with:gettype() == niltype then
			return `iface.obj == nil
		end
		error(("invalid comparison between %s and %s"):format(
				a:gettype(), b:gettype()))
	end)
	self.type.metamethods.__ne = macro(function(a, b)
		return `not (a == b)
	end)
	return self.type
end

function interface:createcast(from,exp)
	if not self.implementedtypes[from] then
		local instance = {}
		local impl = terralib.newlist()
		for _,m in ipairs(self.methods) do
			local fn = from.methods[m.name]
			if not fn then
				local msg
				msg = "type %s lacks method %s = %s to satisfy the interface"
				error(msg:format(tostring(from), m.name,
						tostring(m.type)))
			end
			if not terralib.isfunction(fn) then
				-- assume it's a macro
				fn = terra([m.syms])
					return fn([m.sym])
				end
			end
			local iface_ret_type = m.type.type.returntype
			local fn_ret_type = fn:gettype().returntype
			if iface_ret_type ~= fn_ret_type then
				-- it is not possible to cast to interface type
				-- here, as vtable is not yet defined
				--
				-- yeah, not true static duck typing :(
				assert(iface_ret_type ~= self.type, "invalid \z
						return value type (must be \z
						strictly interface type) in \z
						the following function:\n" ..
						tostring(fn))
				-- this wrapping is necessary to ensure an
				-- explicit cast from actual function return
				-- type to interface method return type, which
				-- actually results in an error if types are not
				-- compatible, otherwise it will just crash in
				-- the runtime as return value would be some
				-- trash
				fn = terra(self: &opaque, [m.syms]) : iface_ret_type
					return fn([&from](self), [m.syms])
				end
			end
			impl:insert(fn)
		end
		instance.vtable = constant(`[self.vtabletype] { [impl] })
		self.implementedtypes[from] = instance
	end

	local vtable = self.implementedtypes[from].vtable
	return `self.type { &vtable, [&opaque](exp) }
end

return M
