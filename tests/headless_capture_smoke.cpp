// Real, displayless integration test: private KWin -> native screencast -> PipeWire frame.
#include "platform/linux/kwin_headless.h"
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <array>
#include <iostream>

struct state_t { pw_stream *stream = nullptr; bool frame = false; bool error = false; };
static void process(void *data) {
  auto &state = *static_cast<state_t *>(data);
  auto *buffer = pw_stream_dequeue_buffer(state.stream);
  if (!buffer) return;
  for (uint32_t i = 0; i < buffer->buffer->n_datas; ++i) {
    const auto &plane = buffer->buffer->datas[i];
    if (plane.chunk && plane.chunk->size > 0) state.frame = true;
  }
  pw_stream_queue_buffer(state.stream, buffer);
}
static void changed(void *data, pw_stream_state, pw_stream_state current, const char *) {
  if (current == PW_STREAM_STATE_ERROR) static_cast<state_t *>(data)->error = true;
}
int main() {
  auto &session = syzygy::headless::kwin_session::instance();
  if (!session.start() || session.screen_width() != 640 || session.screen_height() != 480) {
    std::cerr << "FAIL: native KWin capture/input or headless output geometry\n";
    return 1;
  }
  pw_init(nullptr, nullptr);
  auto *loop = pw_main_loop_new(nullptr);
  state_t state;
  pw_stream_events events {};
  events.version = PW_VERSION_STREAM_EVENTS;
  events.state_changed = changed;
  events.process = process;
  state.stream = pw_stream_new_simple(pw_main_loop_get_loop(loop), "headless-smoke",
    pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture", nullptr), &events, &state);
  std::array<uint8_t, 2048> storage {};
  spa_pod_builder builder = SPA_POD_BUILDER_INIT(storage.data(), storage.size());
  spa_video_info_raw info {};
  info.format = SPA_VIDEO_FORMAT_BGRx;
  info.size = SPA_RECTANGLE(640, 480);
  // KWin advertises variable delivery with a separate maximum rate.
  info.framerate = SPA_FRACTION(0, 1);
  info.max_framerate = SPA_FRACTION(60, 1);
  const spa_pod *params[] {spa_format_video_raw_build(&builder, SPA_PARAM_EnumFormat, &info)};
  auto *pwloop = pw_main_loop_get_loop(loop);
  pw_loop_enter(pwloop);
  int result = pw_stream_connect(state.stream, PW_DIRECTION_INPUT, session.node_id(),
    static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params, 1);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (result >= 0 && !state.frame && !state.error && std::chrono::steady_clock::now() < deadline) {
    if (pw_loop_iterate(pwloop, 100) < 0) break;
  }
  pw_stream_destroy(state.stream);
  pw_loop_leave(pwloop);
  pw_main_loop_destroy(loop);
  if (!state.frame || state.error) { std::cerr << "FAIL: no real headless frame\n"; return 1; }
  session.send([](auto *input) { org_kde_kwin_fake_input_pointer_motion(input, wl_fixed_from_int(1), 0); });
  std::cout << "PASS: real headless KWin capture, virtual geometry, PipeWire frame and native input binding\n";
  return 0;
}
