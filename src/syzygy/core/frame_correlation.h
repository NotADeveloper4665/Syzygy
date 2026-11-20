#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <utility>

namespace syzygy::core {

// Metadata submitted with an AVFrame and recovered only by matching the
// encoder output packet's PTS. frame_timestamp preserves the existing
// cadence-adjusted wire-header timestamp; core_frame_timestamp preserves the
// unadjusted source capture timestamp. Never substitute current-frame metadata
// for a delayed or reordered output packet.
struct frame_timing_record {
  std::int64_t pts {};
  std::chrono::steady_clock::time_point submitted_at {};
  std::optional<std::chrono::steady_clock::time_point> frame_timestamp;
  std::optional<std::chrono::steady_clock::time_point> core_frame_timestamp;
  std::optional<double> capture_ms;
  std::optional<double> capture_backend_ms;
  std::optional<double> conversion_ms;
};

class pts_timing_correlator {
public:
  explicit pts_timing_correlator(std::size_t max_pending = 256):
      max_pending_ {max_pending} {
  }

  pts_timing_correlator(const pts_timing_correlator&) = delete;
  pts_timing_correlator(pts_timing_correlator&&) noexcept = default;
  pts_timing_correlator& operator=(pts_timing_correlator&&) noexcept = default;
  pts_timing_correlator& operator=(const pts_timing_correlator&) = delete;

  bool remember(frame_timing_record record) {
    if (max_pending_ == 0 || record.submitted_at.time_since_epoch().count() == 0) {
      return false;
    }

    const auto pts = record.pts;
    if (pending_.find(pts) != pending_.end()) {
      // A reused PTS makes the association ambiguous. Remove the old record
      // rather than risk attaching stale metadata to a later output packet.
      pending_.erase(pts);
      const auto old_order = std::find(insertion_order_.begin(), insertion_order_.end(), pts);
      if (old_order != insertion_order_.end()) insertion_order_.erase(old_order);
      return false;
    }
    pending_.emplace(pts, std::move(record));
    insertion_order_.push_back(pts);
    while (pending_.size() > max_pending_) {
      const auto oldest = insertion_order_.front();
      insertion_order_.pop_front();
      if (pending_.erase(oldest) != 0) {
        ++evictions_;
      }
    }
    return true;
  }

  std::optional<frame_timing_record> take(std::int64_t pts) {
    const auto found = pending_.find(pts);
    if (found == pending_.end()) {
      return std::nullopt;
    }

    auto record = std::move(found->second);
    pending_.erase(found);
    const auto order_entry = std::find(insertion_order_.begin(), insertion_order_.end(), pts);
    if (order_entry != insertion_order_.end()) {
      insertion_order_.erase(order_entry);
    }
    return record;
  }

  std::size_t size() const noexcept {
    return pending_.size();
  }

  std::uint64_t evictions() const noexcept {
    return evictions_;
  }

  void clear() noexcept {
    pending_.clear();
    insertion_order_.clear();
  }

private:
  std::size_t max_pending_;
  std::map<std::int64_t, frame_timing_record> pending_;
  std::deque<std::int64_t> insertion_order_;
  std::uint64_t evictions_ {};
};

}  // namespace syzygy::core
