#include <cassert>
#include <chrono>
#include <string>

#include "src/syzygy/core/bounded_session_video_queue.h"

int main() {
  using queue = syzygy::core::bounded_session_video_queue<std::string>;
  queue q({2, 16});

  assert(q.try_push("idr-1", 5, true) == queue::push_result::accepted);
  assert(q.try_push("p-2", 4, false) == queue::push_result::accepted);
  assert(q.try_push("p-3", 4, false) == queue::push_result::overflow_requires_keyframe);

  auto state = q.inspect();
  assert(state.frames == 0);
  assert(state.bytes == 0);
  assert(state.overflow_events == 1);
  assert(state.awaiting_keyframe);

  assert(q.try_push("p-4", 4, false) == queue::push_result::awaiting_keyframe);
  assert(q.try_push("idr-5", 5, true) == queue::push_result::accepted_after_resync);
  assert(q.try_push("p-6", 4, false) == queue::push_result::accepted);

  auto first = q.pop_for(std::chrono::milliseconds(1));
  auto second = q.pop_for(std::chrono::milliseconds(1));
  assert(first && *first == "idr-5");
  assert(second && *second == "p-6");
  assert(!q.pop_for(std::chrono::milliseconds(1)));

  assert(q.try_push("too-large", 17, false) == queue::push_result::item_too_large);
  assert(q.inspect().awaiting_keyframe);
  q.close();
  assert(q.try_push("late", 1, true) == queue::push_result::stopped);
  assert(!q.pop());
  return 0;
}
