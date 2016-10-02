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

local C = terralib.includecstring[[

#include <stdlib.h>

]]

local M = {}

local struct ByteSlice {
	ptr: &int8,
	size: C.size_t,
}

M.ByteSlice = ByteSlice

ByteSlice.metamethods.__cast = function(from,to,exp)
	if to:ispointer() and to.type == int8 then
		return `exp.ptr
	end
	error(("invalid cast from %s to %s"):format(from, to))
end

local struct CString {
	ptr: &int8,
}

M.CString = CString

CString.metamethods.__cast = function(from,to,exp)
	if to:ispointer() and to.type == int8 then
		return `exp.ptr
	elseif from:ispointer() and from.type == int8 then
		return `CString { exp }
	elseif from == niltype then
		return `CString { nil }
	elseif to == M.ByteSlice then 
		return `M.ByteSlice { exp.ptr, C.strlen(exp.ptr) }
	end
	error(("invalid cast from %s to %s"):format(from, to))
end

CString.metamethods.__eq = macro(function(a, b)
	local cstr
	local with
	if a:gettype() == CString then
		cstr = a
		with = b
	else
		cstr = b
		with = a
	end
	if with:gettype() == niltype then
		return `cstr.ptr == nil
	end
	error(("invalid comparison between %s and %s"):format(
			a:gettype(), b:gettype()))
end)
CString.metamethods.__ne = macro(function(a, b)
	return `not (a == b)
end)

return M
