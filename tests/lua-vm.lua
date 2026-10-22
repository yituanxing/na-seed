-- Unmodified upstream Lua interpreter, native libc, real table VM and GC.
local function fib(n)
  if n < 2 then return n end
  return fib(n - 1) + fib(n - 2)
end
assert(fib(12) == 144)
local t = {}
for i = 1, 1000 do t[i] = i * i end
assert(#t == 1000 and t[500] == 250000)
local sum = 0
for _, x in ipairs(t) do sum = sum + x end
assert(sum == 1000 * 1001 * 2001 / 6)
assert(string.format("%s-%03d", "vm", 7) == "vm-007")
local c = coroutine.create(function()
  coroutine.yield("pause", 17)
  return "resumed"
end)
local ok, state, n = coroutine.resume(c)
assert(ok and state == "pause" and n == 17)
local ok2, state2 = coroutine.resume(c)
assert(ok2 and state2 == "resumed")
collectgarbage("collect")
print("lua-vm-ok")
