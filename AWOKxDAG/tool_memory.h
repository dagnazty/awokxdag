#pragma once

#include <cstddef>
#include <cstdint>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <new>
#include <type_traits>

// Owned by loopTask. Construct/destruct entries, including Arduino Strings.
// Result tables prefer PSRAM; radio callback storage must request internal RAM.
template <typename T, size_t Count>
class ToolBuffer {
 public:
  ToolBuffer() = default;
  ToolBuffer(const ToolBuffer&) = delete;
  ToolBuffer& operator=(const ToolBuffer&) = delete;
  ~ToolBuffer() { release(); }

  bool allocate(bool internalOnly = false) {
    if (entries_) return true;
    void* storage = nullptr;
    if (!internalOnly)
      storage = heap_caps_malloc(sizeof(T) * Count, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!storage)
      storage = heap_caps_malloc(sizeof(T) * Count, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!storage) return false;
    entries_ = static_cast<T*>(storage);
    for (size_t i = 0; i < Count; ++i) new (entries_ + i) T();
    return true;
  }

  void release() {
    if (!entries_) return;
    for (size_t i = 0; i < Count; ++i) entries_[i].~T();
    heap_caps_free(entries_);
    entries_ = nullptr;
  }

  explicit operator bool() const { return entries_ != nullptr; }
  T& operator[](size_t i) { return entries_[i]; }
  const T& operator[](size_t i) const { return entries_[i]; }

 private:
  T* entries_ = nullptr;
};

// Multiple callback producers, one loopTask consumer. Only bounded POD copies
// happen under the lock; allocation, parsing, SD and radio calls stay outside.
// begin/pause/release are loopTask-only. A paused queue can still be drained.
template <typename T, size_t Count>
class ToolQueue {
  static_assert(Count > 1, "A ring needs at least two slots");
  static_assert(std::is_trivially_copyable<T>::value, "Callback hits must be POD");
 public:
  bool begin() {
    if (!storage_.allocate(true)) return false;
    portENTER_CRITICAL(&mux_);
    head_ = tail_ = 0;
    if (++generation_ == 0) ++generation_;
    accepting_ = true;
    portEXIT_CRITICAL(&mux_);
    return true;
  }

  uint32_t generation() {
    portENTER_CRITICAL(&mux_);
    const uint32_t value = accepting_ ? generation_ : 0;
    portEXIT_CRITICAL(&mux_);
    return value;
  }

  bool push(const T& hit, uint32_t generation) {
    bool queued = false;
    portENTER_CRITICAL(&mux_);
    const size_t next = (head_ + 1) % Count;
    if (accepting_ && generation != 0 && generation == generation_ && next != tail_) {
      storage_[head_] = hit;
      head_ = next;
      queued = true;
    }
    portEXIT_CRITICAL(&mux_);
    return queued;
  }

  bool pop(T& hit) {
    bool found = false;
    portENTER_CRITICAL(&mux_);
    if (head_ != tail_) {
      hit = storage_[tail_];
      tail_ = (tail_ + 1) % Count;
      found = true;
    }
    portEXIT_CRITICAL(&mux_);
    return found;
  }

  void pause() {
    portENTER_CRITICAL(&mux_);
    accepting_ = false;
    portEXIT_CRITICAL(&mux_);
  }

  void release() {
    portENTER_CRITICAL(&mux_);
    accepting_ = false;
    head_ = tail_ = 0;
    portEXIT_CRITICAL(&mux_);
    // A callback that was parsing before pause cannot dereference storage:
    // push checks accepting/generation under the same lock before copying.
    storage_.release();
  }

 private:
  ToolBuffer<T, Count> storage_;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  size_t head_ = 0;
  size_t tail_ = 0;
  uint32_t generation_ = 0;
  bool accepting_ = false;
};
