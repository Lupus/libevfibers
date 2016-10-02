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
local errors = require("errors")

local EOne = errors.new("One")
local ETwo = errors.new("Two")

terra returns_two()
	return ETwo.talloc(nil, "magic number is %d", 42)
end

terra returns_one()
	var err = returns_two()
	return EOne.talloc(err, "outer error message: %s", "foo"):link(err)
end

terra main()
	var err = returns_one()
	S.printf(">>>> :string()\n")
	S.printf("%s\n", err:string())
	S.printf(">>>> :verbose()\n")
	S.printf("%s\n", err:verbose())
	S.printf(">>>> format_chain()\n")
	S.printf("%s\n", errors.format_chain(err))
	err:free()

	return 0
end

main()

terralib.saveobj("errors_standalone", "executable", {
	main = main
}, {
	"-lm", "-lrt",
	"-ltalloc",
	"-lunwind",
	"-pthread"
})
