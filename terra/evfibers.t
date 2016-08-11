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
local inspect = require("inspect")

terralib.includepath = terralib.includepath .. ";../include;../build/include"

local C = terralib.includecstring[[

#include <stdio.h>

#include <evfibers/fiber.h>

]]

local M = {}

local struct Context(S.Object) {
	fctx: C.fbr_context
}

M.Context = Context

do
	local old_alloc = Context.methods.alloc
	Context.methods.alloc = terralib.overloadedfunction("alloc")
	Context.methods.alloc:adddefinition(terra()
		var c = old_alloc()
		return c
	end)
	Context.methods.alloc:adddefinition(terra(fctx: C.fbr_context)
	end)
end

return M
