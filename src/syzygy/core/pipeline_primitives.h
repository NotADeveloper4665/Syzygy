#pragma once

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace syzygy::core {

// Conservative wire-cost estimate for admission/fairness. It includes RTP,
// optional encryption prefix, IP+UDP overhead and parity shards. It is an
// estimate for scheduling only; actual send accounting should replace it
// with the final shard count once FEC encoding has completed.
struct wire_cost_config {
  std::size_t payload_bytes {};
  std::size_t packet_payload_bytes {};
  std::size_t rtp_header_bytes {sizeof(std::uint32_t) * 3};
  std::size_t encryption_prefix_bytes {};
  std::size_t network_overhead_bytes {28}; // IPv4+UDP; use 48 for IPv6+UDP
  unsigned fec_percent {};
  unsigned minimum_parity_shards {};
};

struct wire_cost {
  std::size_t data_shards {};
  std::size_t parity_shards {};
  std::size_t total_bytes {};
};

inline std::optional<wire_cost> estimate_wire_cost(const wire_cost_config &cfg) noexcept {
  if (!cfg.packet_payload_bytes || cfg.fec_percent > 400 ||
      cfg.payload_bytes == 0) {
    return std::nullopt;
  }
  const auto data = cfg.payload_bytes / cfg.packet_payload_bytes +
                    (cfg.payload_bytes % cfg.packet_payload_bytes != 0);
  if (data > (static_cast<std::size_t>(-1) - 99) / std::max(1u, cfg.fec_percent)) {
    return std::nullopt;
  }
  const auto parity_by_percent = (data * cfg.fec_percent + 99) / 100;
  const auto parity = std::max<std::size_t>(cfg.minimum_parity_shards, parity_by_percent);
  const auto per_packet = cfg.packet_payload_bytes + cfg.rtp_header_bytes +
                          cfg.encryption_prefix_bytes + cfg.network_overhead_bytes;
  if (data + parity > (static_cast<std::size_t>(-1) / per_packet)) {
    return std::nullopt;
  }
  return wire_cost {data, parity, (data + parity) * per_packet};
}

enum class socket_pressure {
  none,
  local_backpressure,
  message_too_large,
  route_or_peer_failure,
  unknown_failure,
};

// Classify host-side send results separately from remote congestion signals.
// Callers must not lower bitrate or raise FEC based only on local_backpressure.
inline socket_pressure classify_socket_error(int error) noexcept {
  switch (error) {
    case 0: return socket_pressure::none;
    case EAGAIN:
#if EWOULDBLOCK != EAGAIN
    case EWOULDBLOCK:
#endif
    case ENOBUFS:
    case EINTR:
      return socket_pressure::local_backpressure;
    case EMSGSIZE:
      return socket_pressure::message_too_large;
    case ENETUNREACH:
    case EHOSTUNREACH:
    case ECONNREFUSED:
    case EPIPE:
      return socket_pressure::route_or_peer_failure;
    default:
      return socket_pressure::unknown_failure;
  }
}

// One reservation follows a frame through encoder-in-flight, packetizer-ready
// and sender-in-flight stages. Moving the token between stages does not release
// capacity; only destruction after final completion releases it.
class pipeline_budget {
  struct state {
    explicit state(std::size_t frame_cap, std::size_t byte_cap):
        max_frames {frame_cap}, max_bytes {byte_cap} {}
    std::mutex mutex;
    const std::size_t max_frames;
    const std::size_t max_bytes;
    std::size_t frames {};
    std::size_t bytes {};
  };

public:
  class reservation {
  public:
    reservation() = default;
    reservation(const reservation &) = delete;
    reservation &operator=(const reservation &) = delete;
    reservation(reservation &&) noexcept = default;
    reservation &operator=(reservation &&other) noexcept {
      release();
      state_ = std::move(other.state_);
      bytes_ = std::exchange(other.bytes_, 0);
      return *this;
    }
    ~reservation() { release(); }

    explicit operator bool() const noexcept { return static_cast<bool>(state_); }
    std::size_t bytes() const noexcept { return bytes_; }

    void release() noexcept {
      if (!state_) return;
      {
        std::lock_guard lock {state_->mutex};
        --state_->frames;
        state_->bytes -= bytes_;
      }
      state_.reset();
      bytes_ = 0;
    }

  private:
    friend class pipeline_budget;
    reservation(std::shared_ptr<state> shared, std::size_t bytes):
        state_ {std::move(shared)}, bytes_ {bytes} {}
    std::shared_ptr<state> state_;
    std::size_t bytes_ {};
  };

  pipeline_budget(std::size_t max_frames, std::size_t max_bytes):
      state_ {std::make_shared<state>(max_frames, max_bytes)} {}

  std::optional<reservation> try_reserve(std::size_t bytes) {
    if (!bytes) return std::nullopt;
    std::lock_guard lock {state_->mutex};
    if (state_->frames >= state_->max_frames ||
        bytes > state_->max_bytes - std::min(state_->bytes, state_->max_bytes)) {
      return std::nullopt;
    }
    ++state_->frames;
    state_->bytes += bytes;
    return reservation {state_, bytes};
  }

  struct snapshot { std::size_t frames; std::size_t bytes; };
  snapshot stats() const {
    std::lock_guard lock {state_->mutex};
    return {state_->frames, state_->bytes};
  }

private:
  std::shared_ptr<state> state_;
};

// A surface lease is not reusable merely because its encoded packet was
// dequeued. The backend must signal completion after the GPU/encoder releases
// the surface. This tracker keeps ownership alive until that explicit signal.
class completion_gate {
public:
  bool complete() noexcept {
    std::lock_guard lock {mutex_};
    if (completed_) return false;
    completed_ = true;
    return true;
  }
  bool is_complete() const noexcept {
    std::lock_guard lock {mutex_};
    return completed_;
  }
private:
  mutable std::mutex mutex_;
  bool completed_ {};
};

} // namespace syzygy::core
