local lunatest = require("lunatest")

require("tests.lcapi")

lunatest.suite("tests.lcapi")

lunatest.run({}, {"-v"})
