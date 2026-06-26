-- Regression test for GC correctness across all collector modes.
-- See Copyright Notice in file lua.h
--
-- Motivation: this project hit two GC-layout/optimizer bugs that a routine
-- full collection would trip but that the existing tests did not reliably
-- surface:
--   * base-class tail-padding reuse corrupting an object's 'tt'/'marked'
--     header (fixed with gcHeaderReserved_), and
--   * a GCC-15 LTO miscompile of the marking path that crashed at
--     restartcollection -> markvalue(g, registry), i.e. the very first step of
--     *every* full collection.
-- Both manifest as a crash/corruption during a full collection while a
-- non-trivial, rooted object graph is live. This test forces many full
-- collections (and steps) in default, incremental, and generational modes
-- while holding such a graph, and verifies data integrity throughout. It is
-- meant to be cheap enough for all.lua but thorough enough to fail loudly if a
-- full collection ever corrupts live objects again.

print('testing GC correctness across collector modes')

-- Build a rooted object graph: a mix of tables, strings, closures, and nested
-- structure, deliberately interconnected so the marker must traverse it.
local function buildgraph (n)
  local root = {tag = "root", kids = {}, count = 0}
  local prev = root
  for i = 1, n do
    local node = {
      i = i,
      name = "node-" .. i,                  -- fresh string each time
      payload = {i, i * 2, i * 3, "s" .. i},
      get = function () return i end,        -- closure capturing 'i'
      back = prev,                           -- back-reference (cycle-ish)
    }
    prev.next = node
    root.kids[i] = node
    root.count = root.count + 1
    prev = node
  end
  return root
end

-- Verify the graph was not corrupted by collection.
local function checkgraph (root, n)
  assert(root.tag == "root")
  assert(root.count == n)
  local node = root.next
  for i = 1, n do
    assert(node.i == i)
    assert(node.name == "node-" .. i)
    assert(node.payload[1] == i and node.payload[4] == "s" .. i)
    assert(node.get() == i)                 -- closure upvalue intact
    assert(root.kids[i] == node)
    node = node.next
  end
end


-- Exercise one collector mode hard: switch into it, then repeatedly allocate
-- garbage and force full collections / steps while a live graph is rooted.
local function stress (mode, n, rounds)
  collectgarbage(mode)
  local root = buildgraph(n)
  for r = 1, rounds do
    -- create a heap of unreachable garbage between collections
    do
      local junk
      for j = 1, n do
        junk = {j, "g" .. j, {j, j}, function () return junk end}
      end
      junk = nil
    end
    -- a full collection: the operation that tripped both historical bugs
    collectgarbage("collect")
    checkgraph(root, n)
    -- incremental steps must also leave the graph intact
    collectgarbage("step")
    checkgraph(root, n)
  end
  -- one more full collection after dropping the root; must not crash
  root = nil
  collectgarbage("collect")
  return true
end


assert(collectgarbage("isrunning"))
collectgarbage()

-- Default (incremental) mode.
do
  collectgarbage("incremental")
  assert(stress("incremental", 200, 6))
end

-- Generational mode: minor/major collections, object aging.
do
  collectgarbage("generational")
  assert(stress("generational", 200, 6))
end

-- Rapidly alternate between modes while a graph is live: each switch can
-- trigger a full collection internally, so the graph must survive flips too.
do
  local root = buildgraph(150)
  for i = 1, 10 do
    collectgarbage("generational")
    collectgarbage("step")
    collectgarbage("incremental")
    collectgarbage("collect")
    checkgraph(root, 150)
  end
  root = nil
  collectgarbage()
end


-- Error paths during collection: a finalizer that raises must not abort the
-- collector nor corrupt unrelated live objects. The raised error surfaces as a
-- warning, so we use the test harness's warning-capture protocol ('@store').
-- This only runs under the internal 'T' test library (same gating as gc.lua).
if T then
  collectgarbage()
  local survivor = buildgraph(50)
  local finalized = false
  warn("@store")
  do
    setmetatable({}, {__gc = function ()
      finalized = true
      error("@expected boom from finalizer")
    end})
  end
  collectgarbage("collect")        -- runs the finalizer; its error is warned
  assert(finalized, "erroring finalizer did not run")
  assert(string.find(_WARN, "boom from finalizer"),
         "finalizer error was not reported as a warning")
  _WARN = false
  warn("@normal")
  checkgraph(survivor, 50)         -- unrelated graph untouched by the errored GC
end


-- Weak tables across a full collection: weak values must be cleared, strong
-- references must be kept, all without corrupting the collector.
do
  collectgarbage()
  local weak = setmetatable({}, {__mode = "v"})
  local kept = {}
  for i = 1, 100 do
    local t = {i}
    weak[i] = t
    if i % 5 == 0 then kept[i] = t end      -- keep every 5th alive
  end
  collectgarbage("collect")
  for i = 1, 100 do
    if i % 5 == 0 then
      assert(weak[i] ~= nil and weak[i][1] == i, "strong-kept weak entry lost")
    end
  end
  -- at least some non-kept entries should have been collected
  local cleared = 0
  for i = 1, 100 do
    if i % 5 ~= 0 and weak[i] == nil then cleared = cleared + 1 end
  end
  assert(cleared > 0, "no weak values were collected")
end


-- If the internal test library 'T' is present, assert the heap is genuinely
-- consistent after all of the above (catches corruption the asserts miss).
if T then
  T.checkmemory()
end

-- restore the default (incremental) mode for any test that runs after us
collectgarbage("incremental")
collectgarbage()

print('OK')
