-- Drive ordinary Lua stdio -> native libc -> Linux file syscalls.
-- Absolute /tmp path is identical on native Linux and the NaSeed guest.
local path = "/tmp/naseed-lua-io.txt"
local f = assert(io.open(path, "w"))
assert(f:write("lua\n", "real-file\n"))
assert(f:close())
local r = assert(io.open(path, "r"))
assert(r:read("*l") == "lua")
assert(r:read("*l") == "real-file")
assert(r:read("*l") == nil)
assert(r:close())
assert(os.remove(path))
print("lua-io-ok")
