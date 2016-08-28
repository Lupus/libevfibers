local inspect = require("inspect")

local x = macro(function(arg)
	print(inspect(arg))
	return `0
end)

terra run()
	return x(42)
end

print(run())
