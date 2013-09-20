-- Small stats library                      --
----------------------------------------------
-- Version History --
-- 1.0 First written.

-- Tables supplied as arguments are not changed.


-- Table to hold statistical functions
stats={}

-- Get the mean value of a table
function stats.mean( t )
	local sum = 0
	local count= 0

	for k,v in pairs(t) do
		if type(v) == 'number' then
			sum = sum + v
			count = count + 1
		end
	end

	return (sum / count)
end

-- Get the mode of a table.  Returns a table of values.
-- Works on anything (not just numbers).
function stats.mode( t )
	local counts={}

	for k, v in pairs( t ) do
		if counts[v] == nil then
			counts[v] = 1
		else
			counts[v] = counts[v] + 1
		end
	end

	local biggestCount = 0

	for k, v  in pairs( counts ) do
		if v > biggestCount then
			biggestCount = v
		end
	end

	local temp={}

	for k,v in pairs( counts ) do
		if v == biggestCount then
			table.insert( temp, k )
		end
	end

	return temp
end

-- Get the median of a table.
function stats.median( t )
	local temp={}

	-- deep copy table so that when we sort it, the original is unchanged
	-- also weed out any non numbers
	for k,v in pairs(t) do
		if type(v) == 'number' then
			table.insert( temp, v )
		end
	end

	table.sort( temp )

	-- If we have an even number of table elements or odd.
	if math.fmod(#temp,2) == 0 then
		-- return mean value of middle two elements
		return ( temp[#temp/2] + temp[(#temp/2)+1] ) / 2
	else
		-- return middle element
		return temp[math.ceil(#temp/2)]
	end
end


-- Get the standard deviation of a table
function stats.standard_deviation( t )
	local m
	local vm
	local sum = 0
	local count = 0
	local result

	m = stats.mean( t )

	for k,v in pairs(t) do
		if type(v) == 'number' then
			vm = v - m
			sum = sum + (vm * vm)
			count = count + 1
		end
	end

	result = math.sqrt(sum / (count-1))

	return result
end

-- Get the max and min for a table
function stats.maxmin( t )
	local max = -math.huge
	local min = math.huge

	for k,v in pairs( t ) do
		if type(v) == 'number' then
			max = math.max( max, v )
			min = math.min( min, v )
		end
	end

	return max, min
end

return stats
