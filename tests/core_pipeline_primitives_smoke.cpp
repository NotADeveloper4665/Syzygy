#include <cassert>
#include <cerrno>
#include <cstddef>
#include <utility>

#include <src/syzygy/core/pipeline_primitives.h>

int main() {
  using namespace syzygy::core;

  wire_cost_config cost;
  cost.payload_bytes = 100000;
  cost.packet_payload_bytes = 1000;
  cost.fec_percent = 20;
  cost.encryption_prefix_bytes = 32;
  cost.network_overhead_bytes = 48;
  const auto estimate = estimate_wire_cost(cost);
  assert(estimate);
  assert(estimate->data_shards == 100);
  assert(estimate->parity_shards == 20);
  assert(estimate->total_bytes == 100 * 1092 + 20 * 1092);
  assert(!estimate_wire_cost(wire_cost_config{}));

  assert(classify_socket_error(0) == socket_pressure::none);
  assert(classify_socket_error(EAGAIN) == socket_pressure::local_backpressure);
  assert(classify_socket_error(ENOBUFS) == socket_pressure::local_backpressure);
  assert(classify_socket_error(EMSGSIZE) == socket_pressure::message_too_large);
  assert(classify_socket_error(ENETUNREACH) == socket_pressure::route_or_peer_failure);

  pipeline_budget budget {3, 1000};
  auto first = budget.try_reserve(400);
  assert(first);
  auto second = budget.try_reserve(500);
  assert(second);
  assert(!budget.try_reserve(200));
  auto snapshot = budget.stats();
  assert(snapshot.frames == 2 && snapshot.bytes == 900);

  // Moving a reservation through stages preserves the same aggregate budget.
  auto encoder_inflight = std::move(*first);
  auto packetizer_ready = std::move(encoder_inflight);
  auto sender_inflight = std::move(packetizer_ready);
  assert(budget.stats().frames == 2);
  sender_inflight.release();
  assert(budget.stats().frames == 1 && budget.stats().bytes == 500);

  completion_gate gpu_surface;
  assert(!gpu_surface.is_complete());
  assert(gpu_surface.complete());
  assert(!gpu_surface.complete());
  assert(gpu_surface.is_complete());
  return 0;
}
