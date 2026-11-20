#include <cassert>
#include <cstdint>
#include <string>

#include "src/syzygy/core/client_feedback.h"

static void be16(std::string& out, std::uint16_t value) {
  out.push_back(static_cast<char>(value >> 8));
  out.push_back(static_cast<char>(value));
}

static void be32(std::string& out, std::uint32_t value) {
  out.push_back(static_cast<char>(value >> 24));
  out.push_back(static_cast<char>(value >> 16));
  out.push_back(static_cast<char>(value >> 8));
  out.push_back(static_cast<char>(value));
}

static void le32(std::string& out, std::uint32_t value) {
  out.push_back(static_cast<char>(value));
  out.push_back(static_cast<char>(value >> 8));
  out.push_back(static_cast<char>(value >> 16));
  out.push_back(static_cast<char>(value >> 24));
}

int main() {
  using syzygy::core::parse_client_telemetry_v1;
  using syzygy::core::parse_client_telemetry_v2;
  using syzygy::core::parse_fec_frame_status;
  using syzygy::core::parse_legacy_loss_stats;

  std::string fec;
  be32(fec, 7);      // frame index
  be16(fec, 12);     // highest received sequence
  be16(fec, 10);     // next contiguous sequence
  be16(fec, 2);      // missing before highest
  be16(fec, 10);     // total data
  be16(fec, 2);      // total parity
  be16(fec, 8);      // received data
  be16(fec, 2);      // received parity
  fec.push_back(20); // FEC percentage
  fec.push_back(0);  // block index
  fec.push_back(1);  // block count
  auto fec_report = parse_fec_frame_status(fec);
  assert(fec_report);
  assert(fec_report->frame_index == 7);
  assert(fec_report->lost_data_packets == 2);
  assert(fec_report->loss_per_mille == 200.0);
  assert(!parse_fec_frame_status(std::string_view(fec).substr(0, 20)));

  auto invalid_fec = fec;
  invalid_fec[14] = 0;
  invalid_fec[15] = 11; // received data > total data
  assert(!parse_fec_frame_status(invalid_fec));

  invalid_fec = fec;
  invalid_fec[18] = 101; // protocol FEC percentage is bounded to 0..100
  assert(!parse_fec_frame_status(invalid_fec));

  std::string legacy;
  le32(legacy, 3);
  le32(legacy, 50);
  legacy.resize(32, '\0');
  auto legacy_report = parse_legacy_loss_stats(legacy);
  assert(legacy_report);
  assert(legacy_report->loss_count == 3);
  assert(legacy_report->interval_ms == 50);
  assert(!parse_legacy_loss_stats(std::string_view(legacy).substr(0, 31)));

  std::string client = "SZY1";
  client.push_back(1); // version
  client.push_back(0); // flags
  be16(client, 28);    // payload size
  be32(client, 42);    // last frame index
  be16(client, 30);    // sample count
  be16(client, 500);   // window ms
  be32(client, 3000);  // decode average us
  be32(client, 4000);  // presentation queue average us
  be32(client, 2000);  // render average us
  auto client_report = parse_client_telemetry_v1(client);
  assert(client_report);
  assert(client_report->last_frame_index == 42);
  assert(client_report->decode_ms == 3.0);
  assert(client_report->presentation_queue_ms == 4.0);
  assert(client_report->render_ms == 2.0);
  assert(client_report->presentation_ms == 6.0);

  auto invalid_client = client;
  invalid_client[0] = 'X';
  assert(!parse_client_telemetry_v1(invalid_client));

  invalid_client = client;
  invalid_client[16] = static_cast<char>(0x01);
  invalid_client[17] = static_cast<char>(0x31); // decode > 5 seconds
  assert(!parse_client_telemetry_v1(invalid_client));

  std::string client_v2 = "SZY1";
  client_v2.push_back(2); // version
  client_v2.push_back(0); // flags
  be16(client_v2, 32);    // payload size
  be32(client_v2, 84);    // last frame index
  be16(client_v2, 30);    // sample count
  be16(client_v2, 500);   // window ms
  be32(client_v2, 2500);  // decode average us
  be32(client_v2, 1500);  // presentation queue average us
  be32(client_v2, 1000);  // render average us
  be32(client_v2, 4200);  // RFC 3550 media inter-arrival jitter us
  auto client_v2_report = parse_client_telemetry_v2(client_v2);
  assert(client_v2_report);
  assert(client_v2_report->version == 2);
  assert(client_v2_report->media_interarrival_jitter_ms);
  assert(*client_v2_report->media_interarrival_jitter_ms == 4.2);
  assert(!parse_client_telemetry_v1(client_v2));

  auto invalid_jitter = client_v2;
  invalid_jitter[28] = 0;
  invalid_jitter[29] = 0x4c;
  invalid_jitter[30] = 0x4b;
  invalid_jitter[31] = 0x41; // 5,000,001 us
  assert(!parse_client_telemetry_v2(invalid_jitter));
  return 0;
}
