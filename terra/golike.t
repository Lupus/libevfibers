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
	struct self.type {
		data : uint64
	}
	defined[self.type] = self
	self.type.metamethods.__cast = castmethod

	self.nextid = 0
	self.allocatedsize = 256
	self.implementedtypes = {} 
	
	self.methods = terralib.newlist()
	self.vtabletype = terralib.types.newstruct("vtable")
	-- We assume interfaced objects to have S.Object metatable
	methods["delete"] = {} -> {}
	for k,v in pairs(methods) do
		-- print(k," = ",v)
		assert(v:ispointer() and v.type:isfunction())
		local params,rets = terralib.newlist{&uint8}, v.type.returntype
		local syms = terralib.newlist()
		for i,p in ipairs(v.type.parameters) do
			params:insert(p)
			syms:insert(symbol(p))
		end
		local typ = params -> rets
		self.methods:insert({name = k, type = typ, syms = syms})
		self.vtabletype.entries:insert { field = k, type = &uint8 }
	end
	self.vtables = global(&self.vtabletype)
	self.vtablearray = terralib.new(self.vtabletype[self.allocatedsize])
	self.vtables:set(self.vtablearray)

	for _,m in ipairs(self.methods) do
		self.type.methods[m.name] = terra(interface : &self.type, [m.syms])
			var id = interface.data >> 48
			var mask = (1ULL << 48) - 1
			var obj = [&uint8](mask and interface.data)
			return m.type(self.vtables[id].[m.name])(obj,[m.syms])
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
		instance.id = self.nextid
		assert(instance.id < self.allocatedsize) --TODO: handle resize
		local vtableentry = self.vtablearray[self.nextid]
		self.nextid = self.nextid + 1
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
			vtableentry[m.name] = terralib.cast(&uint8,fn:getpointer()) 
		end
		self.implementedtypes[from] = instance
	end

	local id = self.implementedtypes[from].id
	return `self.type { uint64(exp) or (uint64(id) << 48) }
end

return M
