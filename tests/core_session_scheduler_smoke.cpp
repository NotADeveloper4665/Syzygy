#include <cassert>
#include <chrono>
#include <cstdint>
#include <string>
#include <optional>
#include <thread>
#include <vector>

#include <src/syzygy/core/session_scheduler.h>

int main() {
  using scheduler_t = syzygy::core::session_scheduler<std::string>;
  using key_t = scheduler_t::session_key;

  scheduler_t::limits limits;
  limits.global_frames = 5;
  limits.global_bytes = 1000;
  limits.per_session_frames = 4;
  limits.per_session_bytes = 1000;
  limits.recovery_reserve_frames = 1;
  limits.recovery_reserve_bytes = 250;
  limits.per_session_recovery_reserve_frames = 1;
  limits.per_session_recovery_reserve_bytes = 250;
  limits.quantum_bytes = 100;

  scheduler_t scheduler {limits};
  const key_t a {1, 1};
  const key_t b {2, 1};
  assert(scheduler.register_session(a));
  assert(scheduler.register_session(b));

  assert(scheduler.enqueue(a, "a1", 200, false) == scheduler_t::admission::accepted);
  assert(scheduler.enqueue(a, "a2", 200, false) == scheduler_t::admission::accepted);
  assert(scheduler.enqueue(a, "a3", 200, false) == scheduler_t::admission::accepted);
  assert(scheduler.enqueue(a, "a4", 1, false) == scheduler_t::admission::capacity_exceeded);
  assert(scheduler.enqueue(a, "a5", 1, false) == scheduler_t::admission::recovery_required);

  const auto now = scheduler_t::clock::now();
  assert(scheduler.enqueue(b, "b1", 100, false, now - std::chrono::milliseconds(10)) == scheduler_t::admission::accepted);
  assert(scheduler.enqueue(a, "idr", 200, true) == scheduler_t::admission::accepted);
  const auto b_stats = scheduler.stats(b, now);
  assert(b_stats.frames == 1 && b_stats.bytes == 100 && b_stats.oldest_age_ms >= 10.0);

  assert(scheduler.global_stats().bytes == 900);

  // A's 200-byte head frame initially exceeds one 100-byte quantum. B's
  // smaller frame must still make progress, and A must retain FIFO order.
  auto first = scheduler.try_pop();
  assert(first && first->session.id == b.id && first->value == "b1");

  std::vector<std::string> a_order;
  for (int i = 0; i < 4; ++i) {
    auto item = scheduler.try_pop();
    assert(item && item->session.id == a.id);
    a_order.push_back(item->value);
  }
  assert((a_order == std::vector<std::string> {"a1", "a2", "a3", "idr"}));

  // A removed generation cannot publish after reconnect.
  assert(scheduler.unregister_session(b));
  const key_t b_reconnected {2, 2};
  assert(scheduler.register_session(b_reconnected));
  assert(scheduler.enqueue(b, "stale", 100, false) == scheduler_t::admission::stale_generation);

  // Unregister releases any queued ownership and restores global accounting.
  const key_t c_key {3, 1};
  assert(scheduler.register_session(c_key));
  assert(scheduler.enqueue(c_key, "queued-owner", 50, true) == scheduler_t::admission::accepted);
  assert(scheduler.global_stats().bytes == 50);
  assert(scheduler.unregister_session(c_key));
  assert(scheduler.global_stats().bytes == 0);

  scheduler.stop();
  assert(scheduler.enqueue(a, "after-stop", 10, true) == scheduler_t::admission::stopped);

  // Sender workers can sleep until a frame is admitted instead of polling.
  scheduler_t wake_scheduler;
  const key_t wake_key {9, 1};
  assert(wake_scheduler.register_session(wake_key));
  std::optional<scheduler_t::scheduled_item> awakened;
  std::thread sender_waiter([&] {
    awakened = wake_scheduler.wait_pop_for(std::chrono::seconds(1));
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  assert(wake_scheduler.enqueue(wake_key, "wake-frame", 1024, true) == scheduler_t::admission::accepted);
  sender_waiter.join();
  assert(awakened && awakened->value == "wake-frame");
  wake_scheduler.stop();

  scheduler_t stopped_scheduler;
  assert(stopped_scheduler.register_session({10, 1}));
  std::optional<scheduler_t::scheduled_item> stopped_result;
  std::thread stopped_waiter([&] {
    stopped_result = stopped_scheduler.wait_pop_for(std::chrono::seconds(1));
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  stopped_scheduler.stop();
  stopped_waiter.join();
  assert(!stopped_result);
  return 0;
}
