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
local check = require("check")
local talloc = require("talloc")

local dtor_called = global(bool)

local struct User {
	uid: int
	username: &int8
	num_groups: int,
	groups: &&int8,
}

terra User:destructor()
	dtor_called = true
end

terra test_user(i: int)
	dtor_called = false
	talloc.set_log_stderr()
	var user = talloc.talloc(nil, User)
	talloc.set_destructor(user, User.destructor)
	user.uid = 42
	user.username = talloc.strdup(user, "Test user")
	user.num_groups = 100
	user.groups = talloc.array(user, [&int8], user.num_groups)
	for i = 0,user.num_groups do
		user.groups[i] = talloc.asprintf(user.groups, "Test group %d",
				i)
	end
	check.assert(not dtor_called)
	talloc.free(user)
	check.assert(dtor_called)
end

local struct User2(talloc.ObjectWithOptions({enable_salloc = true})) {
	uid: int
	username: &int8
	num_groups: int,
	groups: &&int8,
}

terra User2:__init(uid: int)
	self.uid = uid
end

terra User2:__destruct()
	dtor_called = true
end

terra user_salloc()
	var user2 = User2.salloc(42)
end

terra test_user2(i: int)
	dtor_called = false
	talloc.set_log_stderr()
	var user = User2.talloc(nil, 42)
	user.username = talloc.strdup(user, "Test user")
	user.num_groups = 200
	user.groups = talloc.array(user, [&int8], user.num_groups)
	for i = 0,user.num_groups do
		user.groups[i] = talloc.asprintf(user.groups, "Test group %d",
				i)
	end
	check.assert(not dtor_called)
	user:free()
	check.assert(dtor_called)

	dtor_called = false
	user_salloc()
	check.assert(dtor_called)
end

return {
	tcase = terra()
		var tc = check.TCase.alloc("talloc")
		tc:add_test(test_user)
		tc:add_test(test_user2)
		return tc
	end
}
