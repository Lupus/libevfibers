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
		return `to { 0 }
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
	-- We assume interfaced objects to have S.Object metatable
	methods["delete"] = {} -> {}
	for k,v in pairs(methods) do
		-- print(k," = ",v)
		assert(v:ispointer() and v.type:isfunction())
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
			return quote
				var mask = (1ULL << 48) - 1
				var obj = [&uint8](mask and iface.data)
			in
				obj ~= nil
			end
		end
		error(("invalid comparison between %s and %s"):format(
				a:gettype(), b:gettype()))
	end)
	return self.type
end

function interface:createcast(from,exp)
	if not self.implementedtypes[from] then
		local instance = {}
		local impl = terralib.newlist()
		for _,m in ipairs(self.methods) do
			local fn
			if m.name == "delete" then
				-- workaround for ondemand delete method
				fn = terra(self: &from)
					self:delete()
				end
			else
				fn = from.methods[m.name]
			end
			assert(fn and terralib.isfunction(fn))
			impl:insert(fn)
		end
		instance.vtable = constant(`[self.vtabletype] { [impl] })
		self.implementedtypes[from] = instance
	end

	local vtable = self.implementedtypes[from].vtable
	return `self.type { &vtable, [&opaque](exp) }
end

return M
