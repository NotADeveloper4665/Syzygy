#include "../tests_common.h"

#include <string>

#include <src/syzygy/core/session_scheduler.h>

using scheduler_t = syzygy::core::session_scheduler<std::string>;
using key_t = scheduler_t::session_key;

TEST(SessionScheduler, EnforcesGlobalAndPerSessionByteAndFrameLimits) {
  scheduler_t::limits limits;
  limits.global_frames = 4;
  limits.global_bytes = 1000;
  limits.per_session_frames = 2;
  limits.per_session_bytes = 700;
  limits.per_session_recovery_reserve_frames = 0;
  limits.per_session_recovery_reserve_bytes = 0;
  limits.recovery_reserve_frames = 1;
  limits.recovery_reserve_bytes = 200;
  limits.quantum_bytes = 100;
  scheduler_t scheduler {limits};
  const key_t a {1, 1};
  ASSERT_TRUE(scheduler.register_session(a));
  EXPECT_EQ(scheduler.enqueue(a, "one", 300, false), scheduler_t::admission::accepted);
  EXPECT_EQ(scheduler.enqueue(a, "two", 300, false), scheduler_t::admission::accepted);
  EXPECT_EQ(scheduler.enqueue(a, "three", 50, false), scheduler_t::admission::capacity_exceeded);
  const auto stats = scheduler.stats(a);
  EXPECT_EQ(stats.frames, 2u);
  EXPECT_EQ(stats.bytes, 600u);
  EXPECT_TRUE(stats.recovery_required);
}

TEST(SessionScheduler, ReservesAdmissionCapacityForRecoveryKeyframes) {
  scheduler_t::limits limits;
  limits.global_frames = 4;
  limits.global_bytes = 1000;
  limits.per_session_frames = 4;
  limits.per_session_bytes = 1000;
  limits.per_session_recovery_reserve_frames = 1;
  limits.per_session_recovery_reserve_bytes = 300;
  limits.recovery_reserve_frames = 1;
  limits.recovery_reserve_bytes = 300;
  limits.quantum_bytes = 100;
  scheduler_t scheduler {limits};
  const key_t a {1, 1};
  ASSERT_TRUE(scheduler.register_session(a));
  EXPECT_EQ(scheduler.enqueue(a, "p1", 200, false), scheduler_t::admission::accepted);
  EXPECT_EQ(scheduler.enqueue(a, "p2", 200, false), scheduler_t::admission::accepted);
  EXPECT_EQ(scheduler.enqueue(a, "p3", 200, false), scheduler_t::admission::accepted);
  EXPECT_EQ(scheduler.enqueue(a, "p4", 1, false), scheduler_t::admission::capacity_exceeded);
  EXPECT_EQ(scheduler.enqueue(a, "p5", 1, false), scheduler_t::admission::recovery_required);
  EXPECT_EQ(scheduler.enqueue(a, "idr", 250, true), scheduler_t::admission::accepted);
  EXPECT_FALSE(scheduler.stats(a).recovery_required);
  EXPECT_EQ(scheduler.global_stats().frames, 4u);
}

TEST(SessionScheduler, DeficitRoundRobinSharesServiceByBytes) {
  scheduler_t::limits limits;
  limits.global_frames = 16;
  limits.global_bytes = 10000;
  limits.per_session_frames = 8;
  limits.per_session_bytes = 8000;
  limits.per_session_recovery_reserve_bytes = 100;
  limits.recovery_reserve_frames = 1;
  limits.recovery_reserve_bytes = 100;
  limits.quantum_bytes = 100;
  scheduler_t scheduler {limits};
  const key_t a {1, 1};
  const key_t b {2, 1};
  ASSERT_TRUE(scheduler.register_session(a));
  ASSERT_TRUE(scheduler.register_session(b));
  for (int i = 0; i < 4; ++i) {
    ASSERT_EQ(scheduler.enqueue(a, "A", 300, false), scheduler_t::admission::accepted);
    ASSERT_EQ(scheduler.enqueue(b, "B", 100, false), scheduler_t::admission::accepted);
  }

  std::size_t a_bytes = 0;
  std::size_t b_bytes = 0;
  for (int i = 0; i < 8; ++i) {
    auto item = scheduler.try_pop();
    ASSERT_TRUE(item.has_value());
    if (item->session.id == a.id) a_bytes += item->bytes;
    if (item->session.id == b.id) b_bytes += item->bytes;
  }
  EXPECT_EQ(a_bytes, 1200u);
  EXPECT_EQ(b_bytes, 400u);
}

TEST(SessionScheduler, QueueAgeAndBytesAreObservable) {
  scheduler_t scheduler;
  const key_t a {1, 1};
  ASSERT_TRUE(scheduler.register_session(a));
  const auto now = scheduler_t::clock::now();
  ASSERT_EQ(scheduler.enqueue(a, "frame", 512, false, now - std::chrono::milliseconds(12)), scheduler_t::admission::accepted);
  const auto stats = scheduler.stats(a, now);
  EXPECT_EQ(stats.bytes, 512u);
  EXPECT_EQ(stats.frames, 1u);
  EXPECT_GE(stats.oldest_age_ms, 12.0);
}

TEST(SessionScheduler, ReconnectGenerationRejectsLatePacketsAndClearsOldQueue) {
  scheduler_t scheduler;
  const key_t old_session {42, 7};
  const key_t new_session {42, 8};
  ASSERT_TRUE(scheduler.register_session(old_session));
  ASSERT_EQ(scheduler.enqueue(old_session, "queued", 100, false), scheduler_t::admission::accepted);
  ASSERT_TRUE(scheduler.unregister_session(old_session));
  ASSERT_TRUE(scheduler.register_session(new_session));
  EXPECT_EQ(scheduler.enqueue(old_session, "late", 100, false), scheduler_t::admission::stale_generation);
  EXPECT_EQ(scheduler.global_stats().frames, 0u);
  EXPECT_EQ(scheduler.enqueue(new_session, "new", 100, false), scheduler_t::admission::accepted);
}

TEST(SessionScheduler, PreservesFifoWithinEachSessionAndStopsCleanly) {
  scheduler_t scheduler;
  const key_t a {1, 1};
  ASSERT_TRUE(scheduler.register_session(a));
  ASSERT_EQ(scheduler.enqueue(a, "first", 10, false), scheduler_t::admission::accepted);
  ASSERT_EQ(scheduler.enqueue(a, "second", 10, false), scheduler_t::admission::accepted);
  auto first = scheduler.try_pop();
  auto second = scheduler.try_pop();
  ASSERT_TRUE(first && second);
  EXPECT_EQ(first->value, "first");
  EXPECT_EQ(second->value, "second");
  scheduler.stop();
  EXPECT_EQ(scheduler.enqueue(a, "after-stop", 10, true), scheduler_t::admission::stopped);
  EXPECT_FALSE(scheduler.try_pop());
}
