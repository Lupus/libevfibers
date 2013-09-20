local ffi = require("ffi")
local bit = require("bit")
local band, bnot, rshift = bit.band, bit.bnot, bit.rshift
local so = ffi.load("/home/kolkhovskiy/git/libevfibers/build/libevfibers.so")
local evso = ffi.load("ev")
require("sysdeps_h")
require("ev_h")
require("fiber_h")

local fbr = {
	-- Constants
	FBR_SUCCESS = so.FBR_SUCCESS,
	FBR_EINVAL = so.FBR_EINVAL,
	FBR_ENOFIBER = so.FBR_ENOFIBER,
	FBR_ESYSTEM = so.FBR_ESYSTEM,
	FBR_EBUFFERMMAP = so.FBR_EBUFFERMMAP,
	FBR_ENOKEY = so.FBR_ENOKEY,
	FBR_LOG_ERROR = so.FBR_LOG_ERROR,
	FBR_LOG_WARNING = so.FBR_LOG_WARNING,
	FBR_LOG_NOTICE = so.FBR_LOG_NOTICE,
	FBR_LOG_INFO = so.FBR_LOG_INFO,
	FBR_LOG_DEBUG = so.FBR_LOG_DEBUG,
	FBR_EV_WATCHER = so.FBR_EV_WATCHER,
	FBR_EV_MUTEX = so.FBR_EV_MUTEX,
	FBR_EV_COND_VAR = so.FBR_EV_COND_VAR,
}

local fctx
local fibers = {}
local fibers_ids = {}
local current_fiber
local foreign_flags = ffi.new("enum fbr_foreign_flag[1]")
local one_event_arr = ffi.new("struct fbr_ev_base *[2]")

function fbr.log_e(format, ...)
	so.fbr_log_e(fctx, format, ...)
end

function fbr.log_w(format, ...)
	so.fbr_log_w(fctx, format, ...)
end

function fbr.log_n(format, ...)
	so.fbr_log_n(fctx, format, ...)
end

function fbr.log_i(format, ...)
	so.fbr_log_i(fctx, format, ...)
end

function fbr.log_d(format, ...)
	so.fbr_log_d(fctx, format, ...)
end

function fbr.is_foreign(id)
	return 0 == rshift(tonumber(id), 32)
end

function fbr.ev_wait(events)
	local retval
	retval = so.fbr_ev_wait_prepare(fctx, events)
	assert(0 == retval, "fbr_ev_wait_prepare failed")
	while 0 == so.fbr_has_pending_events(fctx, current_fiber.id) do
		fbr.yield()
	end
	retval = so.fbr_ev_wait_finish(fctx, events)
	assert(retval > 0, "fbr_ev_wait_finish failed")
	return retval
end

function fbr.ev_wait_one(one)
	local n_events
	local events = one_event_arr
	events[0] = one
	events[1] = nil
	n_events = fbr.ev_wait(events)
	assert(1 == n_events, "unexpected number of events returned")
end

local ev_timer_t = ffi.typeof("ev_timer")
local ev_timer_ptr_t = ffi.typeof("ev_timer *")
local fbr_ev_watcher_t = ffi.typeof("struct fbr_ev_watcher")
local fbr_ev_watcher_ptr_t = ffi.typeof("struct fbr_ev_watcher *")

function malloc(ctype, ptr_ctype)
	local size = ffi.cast("size_t", ffi.sizeof(ctype))
	local ptr = ffi.C.malloc(size)
	if ptr_ctype then
		return ffi.cast(ptr_ctype, ptr)
	else
		return ptr
	end
end

function free(ptr)
	return ffi.C.free(ptr)
end

function sleep_dtor(args)
	evso.ev_timer_stop(fbr.loop, args.timer)
	free(args.watcher)
	free(args.timer)
end

function fbr.sleep(seconds)
	local timer = malloc(ev_timer_t, ev_timer_ptr_t)
	local watcher = malloc(fbr_ev_watcher_t)
	local expected = evso.ev_now(fbr.loop) + seconds

	ffi.fill(timer, ffi.sizeof(timer))
	timer.at = seconds
	evso.ev_timer_start(fbr.loop, timer)
	local dtor = fbr.add_destructor(sleep_dtor, {
		watcher = watcher,
		timer = timer
	})
	so.fbr_ev_watcher_init(fctx, watcher, ffi.cast("ev_watcher *", timer))
	fbr.ev_wait_one(so.fbr_ev_watcher_base(watcher))
	fbr.remove_destructor(dtor, true)
	return math.max(0, expected - evso.ev_now(fbr.loop))
end

function fbr.create(name, func)
	local coro = coroutine.create(func)
	local id = so.fbr_create_foreign(fctx, name)
	id = tonumber(id)
	fibers[id] = {
		coro = coro,
		id = id,
		destructors = {},
	}
	return id
end

function fbr.add_destructor(func, args)
	local dtor = {
		func = func,
		args = args
	}
	current_fiber.destructors[dtor] = true
	return dtor
end

function fbr.remove_destructor(dtor, call)
	if call then
		dtor.func(dtor.args)
	end
	current_fiber.destructors[dtor] = nil
end

function fiber_resume(fiber)
	local retval
	current_fiber = fiber;
	retval = so.fbr_foreign_enter(fctx, fiber.id);
	assert(0 == retval, "fbr_foreign_enter failed")
	assert(coroutine.resume(fiber.coro))
end

function fiber_yield()
	local retval
	retval = so.fbr_foreign_leave(fctx, current_fiber.id);
	assert(0 == retval, "fbr_foreign_leave failed")
	coroutine.yield()
end

function fbr.transfer(id)
	if not fbr.is_foreign(id) then
		local retval
		retval = so.fbr_transfer(fctx, id)
		assert(0 == retval, "fbr_transfer failed")
	end
	fiber_resume(fibers[id])
end

function fbr.yield()
	fiber_yield()
end

function check_flag(x, flag)
	x = tonumber(x)
	flag = tonumber(flag)
	return 0 ~= band(x, flag)
end

function clear_flag(x, flag)
	x = tonumber(x)
	flag = tonumber(flag)
	return band(x, bnot(flag))
end

function transfer_pending()
	--local flags = foreign_flags
	--local retval
	local size = ffi.new("size_t[1]")
	local ids = so.fbr_foreign_get_transfer_pending(fctx, size)
	if 0 == size[0] then
		return
	end
	for i = 0, tonumber(size[0] - 1) do
		local id = tonumber(ids[i])
		local fiber = fibers[id]
		--[[
		retval = so.fbr_foreign_get_flags(fctx, fiber.id, flags)
		assert(0 == retval, "fbr_foreign_get_flags failed")
		if not check_flag(flags[0], so.FBR_FF_TRANSFER_PENDING) then
			return
		end
		flags[0] = clear_flag(flags[0], so.FBR_FF_TRANSFER_PENDING)
		retval = so.fbr_foreign_set_flags(fctx, fiber.id, flags[0])
		assert(0 == retval, "fbr_foreign_set_flags failed")
		]]
		fiber_resume(fiber)
	end
end

function fbr.init(ev_loop)
	if nil == ev_loop then
		ev_loop = evso.ev_default_loop(0)
	end
	fbr.loop = ev_loop
	fctx = ffi.new("struct fbr_context")
	so.fbr_init(fctx, ev_loop)
end

function fbr.run()
	while true do
		evso.ev_run(fbr.loop, evso.EVRUN_ONCE)
		transfer_pending()
	end
end

fbr.ev = {}

function fbr.ev.version()
	return evso.ev_version_major(), evso.ev_version_minor()
end

function fbr.ev.now()
	return evso.ev_now(fbr.loop)
end

function fbr.ev.now_update()
	return evso.ev_now_update(fbr.loop)
end

return fbr
