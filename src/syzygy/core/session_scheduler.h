#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace syzygy::core {

// A bounded, per-session encoded-frame scheduler. It deliberately owns queue
// admission and ordering only: packetization, FEC, encryption and socket I/O
// remain downstream and must preserve the order returned by try_pop().
template<class T>
class session_scheduler {
public:
  using clock = std::chrono::steady_clock;

  struct limits {
    std::size_t global_frames {64};
    std::size_t global_bytes {32 * 1024 * 1024};
    std::size_t per_session_frames {8};
    std::size_t per_session_bytes {8 * 1024 * 1024};
    std::size_t recovery_reserve_frames {2};
    std::size_t recovery_reserve_bytes {4 * 1024 * 1024};
    std::size_t quantum_bytes {64 * 1024};
  };

  enum class admission {
    accepted,
    unknown_session,
    stale_generation,
    stopped,
    capacity_exceeded,
    recovery_required,
  };

  struct session_key {
    std::uint64_t id {};
    std::uint64_t generation {};
  };

  struct scheduled_item {
    session_key session;
    T value;
    std::size_t bytes {};
    bool keyframe {};
    clock::time_point enqueued_at {};
  };

  struct queue_stats {
    std::size_t frames {};
    std::size_t bytes {};
    double oldest_age_ms {};
    bool recovery_required {};
  };

  explicit session_scheduler(limits configured = {}):
      limits_ {configured} {
    if (!valid_limits(limits_)) {
      limits_ = {};
    }
  }

  static bool valid_limits(const limits &value) noexcept {
    return value.global_frames > 0 &&
           value.global_bytes > 0 &&
           value.per_session_frames > 0 &&
           value.per_session_bytes > 0 &&
           value.per_session_frames <= value.global_frames &&
           value.per_session_bytes <= value.global_bytes &&
           value.recovery_reserve_frames < value.global_frames &&
           value.recovery_reserve_bytes < value.global_bytes &&
           value.quantum_bytes > 0;
  }

  bool register_session(session_key key, std::size_t weight = 1) {
    if (key.id == 0 || key.generation == 0 || weight == 0) {
      return false;
    }
    std::lock_guard lock {mutex_};
    if (!running_ || sessions_.contains(key.id)) {
      return false;
    }
    session_state state;
    state.generation = key.generation;
    state.weight = weight;
    sessions_.emplace(key.id, std::move(state));
    order_.push_back(key.id);
    return true;
  }

  // Removes all queued frames for this generation. A late producer holding the
  // old generation cannot enqueue into a reconnected session with the same id.
  bool unregister_session(session_key key) {
    std::lock_guard lock {mutex_};
    auto it = sessions_.find(key.id);
    if (it == sessions_.end() || it->second.generation != key.generation) {
      return false;
    }
    global_frames_ -= it->second.frames;
    global_bytes_ -= it->second.bytes;
    sessions_.erase(it);
    order_.erase(std::remove(order_.begin(), order_.end(), key.id), order_.end());
    if (cursor_ >= order_.size()) {
      cursor_ = 0;
    }
    return true;
  }

  admission enqueue(session_key key, T value, std::size_t bytes, bool keyframe,
                    clock::time_point now = clock::now()) {
    std::lock_guard lock {mutex_};
    if (!running_) {
      return admission::stopped;
    }
    auto it = sessions_.find(key.id);
    if (it == sessions_.end()) {
      return admission::unknown_session;
    }
    auto &session = it->second;
    if (session.generation != key.generation) {
      return admission::stale_generation;
    }
    if (session.recovery_required && !keyframe) {
      return admission::recovery_required;
    }
    if (bytes == 0 || bytes > limits_.per_session_bytes || bytes > limits_.global_bytes) {
      session.recovery_required = true;
      return admission::capacity_exceeded;
    }

    const auto session_frame_limit = limits_.per_session_frames;
    const auto session_byte_limit = limits_.per_session_bytes;
    const auto normal_global_frames = limits_.global_frames - limits_.recovery_reserve_frames;
    const auto normal_global_bytes = limits_.global_bytes - limits_.recovery_reserve_bytes;

    const bool session_full =
      session.frames >= session_frame_limit ||
      bytes > session_byte_limit - std::min(session.bytes, session_byte_limit);
    const bool global_full =
      global_frames_ >= (keyframe ? limits_.global_frames : normal_global_frames) ||
      bytes > (keyframe ? limits_.global_bytes : normal_global_bytes) -
                std::min(global_bytes_, keyframe ? limits_.global_bytes : normal_global_bytes);

    if (session_full || global_full) {
      session.recovery_required = true;
      return admission::capacity_exceeded;
    }

    session.frames++;
    session.bytes += bytes;
    session.queue.push_back(scheduled_item {key, std::move(value), bytes, keyframe, now});
    global_frames_++;
    global_bytes_++;
    if (keyframe) {
      session.recovery_required = false;
    }
    return admission::accepted;
  }

  std::optional<scheduled_item> try_pop() {
    std::lock_guard lock {mutex_};
    if (!running_ || order_.empty()) {
      return std::nullopt;
    }

    // A large frame may need several DRR rounds to accumulate enough deficit.
    const auto rounds_per_session = limits_.global_bytes / limits_.quantum_bytes + 2;
    const auto max_visits = std::max<std::size_t>(1, rounds_per_session) * order_.size();
    for (std::size_t visit = 0; visit < max_visits && !order_.empty(); ++visit) {
      if (cursor_ >= order_.size()) {
        cursor_ = 0;
      }
      auto &id = order_[cursor_++];
      auto it = sessions_.find(id);
      if (it == sessions_.end() || it->second.queue.empty()) {
        continue;
      }

      auto &session = it->second;
      const auto quantum = limits_.quantum_bytes * session.weight;
      session.deficit = std::min(limits_.global_bytes, session.deficit + quantum);
      const auto item_bytes = session.queue.front().bytes;
      if (item_bytes > session.deficit) {
        continue;
      }

      session.deficit -= item_bytes;
      auto item = std::move(session.queue.front());
      session.queue.pop_front();
      --session.frames;
      session.bytes -= item.bytes;
      --global_frames_;
      global_bytes_ -= item.bytes;
      return item;
    }
    return std::nullopt;
  }

  queue_stats stats(session_key key, clock::time_point now = clock::now()) const {
    std::lock_guard lock {mutex_};
    auto it = sessions_.find(key.id);
    if (it == sessions_.end() || it->second.generation != key.generation) {
      return {};
    }
    const auto &session = it->second;
    double age = 0.0;
    if (!session.queue.empty()) {
      age = std::chrono::duration<double, std::milli>(now - session.queue.front().enqueued_at).count();
    }
    return {session.frames, session.bytes, std::max(0.0, age), session.recovery_required};
  }

  queue_stats global_stats(clock::time_point now = clock::now()) const {
    std::lock_guard lock {mutex_};
    double oldest = 0.0;
    for (const auto &[id, session] : sessions_) {
      (void) id;
      if (!session.queue.empty()) {
        oldest = std::max(oldest, std::chrono::duration<double, std::milli>(now - session.queue.front().enqueued_at).count());
      }
    }
    return {global_frames_, global_bytes_, std::max(0.0, oldest), false};
  }

  void stop() {
    std::lock_guard lock {mutex_};
    running_ = false;
    sessions_.clear();
    order_.clear();
    cursor_ = 0;
    global_frames_ = 0;
    global_bytes_ = 0;
  }

private:
  struct session_state {
    std::uint64_t generation {};
    std::size_t weight {1};
    std::size_t deficit {};
    std::size_t frames {};
    std::size_t bytes {};
    bool recovery_required {};
    std::deque<scheduled_item> queue;
  };

  limits limits_;
  mutable std::mutex mutex_;
  bool running_ {true};
  std::map<std::uint64_t, session_state> sessions_;
  std::vector<std::uint64_t> order_;
  std::size_t cursor_ {};
  std::size_t global_frames_ {};
  std::size_t global_bytes_ {};
};

}  // namespace syzygy::core
