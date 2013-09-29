#!/usr/bin/luajit-2.0.2
package.path = package.path .. ";./?/init.lua;../build/lualib_gen/?.lua"

local ffi = require("ffi")
local fbr = require("evfibers")
local ev = ffi.load("ev")
require("debugger")

local jd = require("jit.dump")
jd.on('H', 'evfibers.dump.html')

function main()
	local C = ffi.C
	fbr.init()
	print("Hello, world!")

	local id = fbr.create("test fiber", function()
		while true do
			fbr.sleep(0.01)
			print("Ping!")
		end
	end)
	print("Created id " .. tostring(id))
	--pause("Let's debug it")
	fbr.transfer(id)

	fbr.run()
end

main()
