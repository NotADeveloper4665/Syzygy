/**
 * @file tests/unit/test_stream.cpp
 * @brief Test src/stream.*
 */

#include <cstdint>
#include <memory>

#include <src/audio.h>
#include <src/thread_safe.h>
#include <src/syzygy/core/pipeline_primitives.h>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace stream {
  std::vector<uint8_t> concat_and_insert(uint64_t insert_size, uint64_t slice_size, const std::string_view &data1, const std::string_view &data2);
}

#include "../tests_common.h"

TEST(ConcatAndInsertTests, ConcatNoInsertionTest) {
  char b1[] = {'a', 'b'};
  char b2[] = {'c', 'd', 'e'};
  auto res = stream::concat_and_insert(0, 2, std::string_view {b1, sizeof(b1)}, std::string_view {b2, sizeof(b2)});
  auto expected = std::vector<uint8_t> {'a', 'b', 'c', 'd', 'e'};
  ASSERT_EQ(res, expected);
}

TEST(ConcatAndInsertTests, ConcatLargeStrideTest) {
  char b1[] = {'a', 'b'};
  char b2[] = {'c', 'd', 'e'};
  auto res = stream::concat_and_insert(1, sizeof(b1) + sizeof(b2) + 1, std::string_view {b1, sizeof(b1)}, std::string_view {b2, sizeof(b2)});
  auto expected = std::vector<uint8_t> {0, 'a', 'b', 'c', 'd', 'e'};
  ASSERT_EQ(res, expected);
}

TEST(ConcatAndInsertTests, ConcatSmallStrideTest) {
  char b1[] = {'a', 'b'};
  char b2[] = {'c', 'd', 'e'};
  auto res = stream::concat_and_insert(1, 1, std::string_view {b1, sizeof(b1)}, std::string_view {b2, sizeof(b2)});
  auto expected = std::vector<uint8_t> {0, 'a', 0, 'b', 0, 'c', 0, 'd', 0, 'e'};
  ASSERT_EQ(res, expected);
}


TEST(SessionPacketQueueTests, ClearingStoppedQueueReleasesAudioPacketLease) {
  using syzygy::core::session_handle;
  using syzygy::core::session_lifetime;

  auto owner = std::make_shared<int>(88);
  std::weak_ptr<int> weak_owner = owner;
  auto fence = std::make_shared<session_lifetime>(88);
  session_handle handle {owner, fence, 88};
  auto lease = handle.acquire();
  ASSERT_TRUE(lease.has_value());

  safe::queue_t<audio::packet_t> packets {4};
  packets.raise(audio::packet_t {handle, std::move(*lease), audio::buffer_t {16}});
  owner.reset();
  handle = {};

  fence->close();
  EXPECT_EQ(fence->outstanding(), 1u);
  packets.stop();
  packets.clear();
  EXPECT_EQ(fence->outstanding(), 0u);
  fence->close_and_wait();
  EXPECT_TRUE(weak_owner.expired());
}

TEST(SessionPacketQueueTests, PoppedAudioPacketRetainsLeaseUntilSendCompletes) {
  using syzygy::core::session_handle;
  using syzygy::core::session_lifetime;

  auto owner = std::make_shared<int>(42);
  std::weak_ptr<int> weak_owner = owner;
  auto fence = std::make_shared<session_lifetime>(89);
  session_handle handle {owner, fence, 89};
  auto lease = handle.acquire();
  ASSERT_TRUE(lease.has_value());

  safe::queue_t<audio::packet_t> packets {4};
  packets.raise(audio::packet_t {handle, std::move(*lease), audio::buffer_t {16}});
  owner.reset();
  handle = {};
  fence->close();

  auto packet = packets.pop();
  ASSERT_TRUE(packet.has_value());
  ASSERT_TRUE(packet->session);
  EXPECT_EQ(*packet->session.owner_as<int>(), 42);
  EXPECT_TRUE(static_cast<bool>(packet->session_lease));
  EXPECT_EQ(fence->outstanding(), 1u);

  packet.reset();
  EXPECT_EQ(fence->outstanding(), 0u);
  fence->close_and_wait();
  EXPECT_TRUE(weak_owner.expired());
}
