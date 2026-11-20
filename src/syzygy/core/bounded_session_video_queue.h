#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace syzygy::core {

// A per-session FIFO for complete encoded frames. Overflow is a decoder
// continuity event: queued dependent frames are discarded only for this
// session and the queue refuses P-frames until a keyframe is admitted.
template<class T>
class bounded_session_video_queue {
public:
  enum class push_result {
    accepted,
    accepted_after_resync,
    overflow_requires_keyframe,
    awaiting_keyframe,
    item_too_large,
    stopped,
  };

  struct limits {
    std::size_t max_frames {3};
    std::size_t max_bytes {8 * 1024 * 1024};
  };

  struct snapshot {
    std::size_t frames {};
    std::size_t bytes {};
    std::uint64_t overflow_events {};
    bool awaiting_keyframe {};
    bool closed {};
  };

  explicit bounded_session_video_queue(limits bounds = {}):
      bounds_ {bounds} {
    if (bounds_.max_frames == 0 || bounds_.max_bytes == 0) {
      throw std::invalid_argument("video queue limits must be non-zero");
    }
  }

  bounded_session_video_queue(const bounded_session_video_queue&) = delete;
  bounded_session_video_queue& operator=(const bounded_session_video_queue&) = delete;

  push_result try_push(T item, std::size_t byte_size, bool keyframe) {
    std::lock_guard lock {mutex_};
    if (closed_) return push_result::stopped;
    if (byte_size > bounds_.max_bytes) {
      ++overflow_events_;
      awaiting_keyframe_ = true;
      clear_locked();
      return push_result::item_too_large;
    }
    if (awaiting_keyframe_ && !keyframe) return push_result::awaiting_keyframe;

    const bool needs_resync = awaiting_keyframe_;
    if (frames_.size() >= bounds_.max_frames || byte_size > bounds_.max_bytes - bytes_) {
      ++overflow_events_;
      clear_locked();
      if (!keyframe) {
        awaiting_keyframe_ = true;
        return push_result::overflow_requires_keyframe;
      }
      awaiting_keyframe_ = false;
    } else if (needs_resync) {
      clear_locked();
      awaiting_keyframe_ = false;
    }

    bytes_ += byte_size;
    frames_.push_back(entry {std::move(item), byte_size});
    ready_.notify_one();
    return needs_resync ? push_result::accepted_after_resync : push_result::accepted;
  }

  std::optional<T> pop() {
    std::unique_lock lock {mutex_};
    ready_.wait(lock, [&] { return closed_ || !frames_.empty(); });
    if (frames_.empty()) return std::nullopt;
    auto item = std::move(frames_.front());
    frames_.pop_front();
    bytes_ -= item.bytes;
    return std::move(item.value);
  }

  template<class Rep, class Period>
  std::optional<T> pop_for(const std::chrono::duration<Rep, Period>& timeout) {
    std::unique_lock lock {mutex_};
    if (!ready_.wait_for(lock, timeout, [&] { return closed_ || !frames_.empty(); })) {
      return std::nullopt;
    }
    if (frames_.empty()) return std::nullopt;
    auto item = std::move(frames_.front());
    frames_.pop_front();
    bytes_ -= item.bytes;
    return std::move(item.value);
  }

  void close() {
    std::lock_guard lock {mutex_};
    closed_ = true;
    ready_.notify_all();
  }

  snapshot inspect() const {
    std::lock_guard lock {mutex_};
    return {frames_.size(), bytes_, overflow_events_, awaiting_keyframe_, closed_};
  }

private:
  struct entry {
    T value;
    std::size_t bytes;
  };

  void clear_locked() {
    frames_.clear();
    bytes_ = 0;
  }

  const limits bounds_;
  mutable std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<entry> frames_;
  std::size_t bytes_ {};
  std::uint64_t overflow_events_ {};
  bool awaiting_keyframe_ {};
  bool closed_ {};
};

}  // namespace syzygy::core
