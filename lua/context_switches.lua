#!/usr/bin/luajit-2.0.2
local ffi = require("ffi")
local fbr = require("evfibers")
local ev = fbr.ev
local stats = require("stats")

local jd = require("jit.dump")
jd.on('H', 'contexts.dump.html')

function fiber2()
	while true do
		fbr.yield()
	end
end

function fiber1()
	local my_fiber2 = fbr.create("fiber2", fiber2)
	local num_iters = 200000
	local total_iters = num_iters * 50
	local total_count = 0
	local results = {}
	while total_count < total_iters do
		ev.now_update()
		local start = ev.now()
		local count = 0
		while count <= num_iters do
			fbr.transfer(my_fiber2)
			count = count + 1
		end
		ev.now_update()
		local end_ = ev.now()
		local diff = end_ - start
		local result = count * 2 / diff
		print(string.format("switches per second: %.2f", result))
		results[#results + 1] = result
		total_count = total_count + count
	end
	local avg = stats.mean(results)
	local max, min = stats.maxmin(results)
	print(string.format("avg: %.2f, min: %.2f, max: %.2f", avg, min, max))
	ffi.C.exit(0)
end

function main()
	local C = ffi.C
	fbr.init()

	local id = fbr.create("test fiber", fiber1)
	fbr.transfer(id)

	fbr.run()
end

main()
