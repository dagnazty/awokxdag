// Tests the real tool_memory.h with host heap/critical-section substitutes.
#include "tool_memory.h"
#include <atomic>
#include <cassert>
#include <string>
#include <thread>

struct Entry {
  inline static int live = 0;
  std::string text;
  int initial = -127;
  Entry() { ++live; }
  ~Entry() { --live; }
};

struct Hit {
  uint32_t generation;
  unsigned value;
};

static void testResults() {
  ToolBuffer<Entry, 8> table;
  assert(!table && HeapMock::live.empty());
  assert(table.allocate());
  assert(HeapMock::lastCaps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  assert(Entry::live == 8 && table[7].initial == -127);
  table[0].text.assign(1024, 'x');
  const unsigned calls = HeapMock::calls;
  assert(table.allocate() && calls == HeapMock::calls);
  table.release();
  table.release();
  assert(!table && Entry::live == 0 && HeapMock::live.empty());

  HeapMock::failExternal = true;
  assert(table.allocate());
  assert(table[0].text.empty());
  assert(HeapMock::lastCaps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  table.release();
  HeapMock::failInternal = true;
  assert(!table.allocate() && Entry::live == 0 && HeapMock::live.empty());
  HeapMock::failInternal = HeapMock::failExternal = false;

  // Partial multi-buffer startup must unwind previously constructed entries.
  assert(table.allocate());
  ToolQueue<Hit, 4> queue;
  HeapMock::failInternal = true;
  assert(!queue.begin());
  queue.release();
  table.release();
  assert(Entry::live == 0 && HeapMock::live.empty());
  HeapMock::failInternal = false;
  {
    ToolBuffer<Entry, 8> scoped;
    assert(scoped.allocate());
  }
  assert(Entry::live == 0 && HeapMock::live.empty());
}

static void testQueue() {
  ToolQueue<Hit, 4> queue;
  Hit hit = {};
  assert(queue.generation() == 0 && !queue.pop(hit));
  assert(!queue.push(hit, 1));
  assert(queue.begin());
  assert(HeapMock::lastCaps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  const uint32_t first = queue.generation();
  const unsigned calls = HeapMock::calls;
  assert(first != 0);
  for (unsigned i = 0; i < 3; ++i) assert(queue.push({first, i}, first));
  assert(!queue.push({first, 99}, first)); // full: no overwrite
  assert(queue.pop(hit) && hit.value == 0);
  assert(queue.push({first, 3}, first)); // wrap
  queue.pause();
  assert(queue.generation() == 0 && !queue.push({first, 4}, first));
  for (unsigned i = 1; i < 4; ++i) assert(queue.pop(hit) && hit.value == i);
  assert(!queue.pop(hit) && calls == HeapMock::calls); // no callback allocation
  queue.release();
  assert(!queue.push({first, 5}, first) && !queue.pop(hit));
  assert(queue.begin());
  const uint32_t second = queue.generation();
  assert(second != first && !queue.push({first, 6}, first));
  assert(!queue.push(hit, 0));
  assert(queue.push({second, 7}, second));
  assert(queue.pop(hit) && hit.value == 7);
  queue.release();
  queue.release();
  HeapMock::failInternal = true;
  assert(!queue.begin() && queue.generation() == 0 && !queue.pop(hit));
  HeapMock::failInternal = false;
  assert(HeapMock::live.empty());
}

static void testLateCallback() {
  ToolQueue<Hit, 8> queue;
  assert(queue.begin());
  std::atomic<int> stage{0};
  std::thread callback([&] {
    const uint32_t old = queue.generation();
    stage.store(1);
    while (stage.load() != 2) std::this_thread::yield();
    assert(!queue.push({old, 42}, old));
  });
  while (stage.load() != 1) std::this_thread::yield();
  queue.release();
  assert(queue.begin());
  stage.store(2);
  callback.join();
  Hit hit;
  assert(!queue.pop(hit));
  queue.release();
  assert(HeapMock::live.empty());
}

static void testConcurrentStopRestart() {
  ToolQueue<Hit, 32> queue;
  std::atomic<bool> done{false};
  auto produce = [&] {
    unsigned n = 0;
    while (!done.load()) {
      const uint32_t generation = queue.generation();
      if (generation) queue.push({generation, ++n}, generation);
      else std::this_thread::yield();
    }
  };
  std::thread wifi(produce), espNow(produce);
  for (int session = 0; session < 200; ++session) {
    assert(queue.begin());
    const uint32_t generation = queue.generation();
    Hit hit;
    for (int i = 0; i < 100; ++i) {
      if (queue.pop(hit)) assert(hit.generation == generation);
      std::this_thread::yield();
    }
    queue.pause();
    while (queue.pop(hit)) assert(hit.generation == generation);
    queue.release();
  }
  done.store(true);
  wifi.join();
  espNow.join();
  assert(HeapMock::live.empty());
}

int main() {
  testResults();
  testQueue();
  testLateCallback();
  testConcurrentStopRestart();
}
