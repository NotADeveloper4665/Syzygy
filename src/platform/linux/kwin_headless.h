#pragma once

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <wayland-client.h>
#include <zkde-screencast-unstable-v1.h>
#include <fake-input.h>

// This connection belongs to the private compositor started by Syzygy's launcher.
// It never connects to a physical desktop through a fallback display name.
namespace syzygy::headless {
  inline bool active() {
    const char *mode = std::getenv("SYZYGY_HEADLESS_SESSION");
    return mode && std::strcmp(mode, "1") == 0;
  }

  class kwin_session {
  public:
    static kwin_session &instance() { static kwin_session session; return session; }
    bool start() {
      std::scoped_lock lock(mutex);
      if (display) return node != 0 && !failed;
      if (!active()) return false;
      display = wl_display_connect(nullptr);
      if (!display) return false;
      registry = wl_display_get_registry(display);
      wl_registry_add_listener(registry, &registry_events, this);
      if (wl_display_roundtrip(display) < 0 || !manager || !output || !input) {
        close(); return false;
      }
      if (wl_display_roundtrip(display) < 0) { close(); return false; }
      org_kde_kwin_fake_input_authenticate(input, "Syzygy", "Remote control of the private headless desktop");
      stream = zkde_screencast_unstable_v1_stream_output(manager, output,
          ZKDE_SCREENCAST_UNSTABLE_V1_POINTER_EMBEDDED);
      zkde_screencast_stream_unstable_v1_add_listener(stream, &stream_events, this);
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
      while (!node && !failed && std::chrono::steady_clock::now() < deadline) {
        if (wl_display_dispatch_pending(display) < 0) break;
        if (node || failed) break;
        if (wl_display_flush(display) < 0) break;
        pollfd fd {wl_display_get_fd(display), POLLIN, 0};
        if (poll(&fd, 1, 100) > 0 && wl_display_dispatch(display) < 0) break;
      }
      if (!node || failed || width <= 0 || height <= 0) { close(); return false; }
      return true;
    }
    int node_id() const { return node; }
    int screen_width() const { return width; }
    int screen_height() const { return height; }
    template<class F> void send(F operation) {
      if (!start()) return;
      std::scoped_lock lock(mutex);
      operation(input);
      wl_display_flush(display);
    }
    ~kwin_session() { close(); }
  private:
    std::mutex mutex;
    wl_display *display = nullptr;
    wl_registry *registry = nullptr;
    wl_output *output = nullptr;
    zkde_screencast_unstable_v1 *manager = nullptr;
    zkde_screencast_stream_unstable_v1 *stream = nullptr;
    org_kde_kwin_fake_input *input = nullptr;
    int node = 0, width = 0, height = 0;
    bool failed = false;
    void close() {
      if (stream) zkde_screencast_stream_unstable_v1_close(stream);
      if (input) org_kde_kwin_fake_input_destroy(input);
      if (manager) zkde_screencast_unstable_v1_destroy(manager);
      if (output) wl_output_destroy(output);
      if (registry) wl_registry_destroy(registry);
      if (display) { wl_display_flush(display); wl_display_disconnect(display); }
      display = nullptr; registry = nullptr; output = nullptr;
      manager = nullptr; stream = nullptr; input = nullptr; node = 0; failed = false;
    }
    static void global(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
      auto &s = *static_cast<kwin_session *>(data);
      if (std::strcmp(interface, "zkde_screencast_unstable_v1") == 0) {
        s.manager = static_cast<zkde_screencast_unstable_v1 *>(wl_registry_bind(registry, name,
            &zkde_screencast_unstable_v1_interface, std::min(version, 3u)));
      } else if (std::strcmp(interface, "org_kde_kwin_fake_input") == 0 && version >= 5) {
        s.input = static_cast<org_kde_kwin_fake_input *>(wl_registry_bind(registry, name,
            &org_kde_kwin_fake_input_interface, std::min(version, 6u)));
      } else if (std::strcmp(interface, "wl_output") == 0 && !s.output) {
        s.output = static_cast<wl_output *>(wl_registry_bind(registry, name, &wl_output_interface, 1));
        wl_output_add_listener(s.output, &output_events, &s);
      }
    }
    static void removed(void *, wl_registry *, uint32_t) {}
    static void geometry(void *, wl_output *, int32_t, int32_t, int32_t, int32_t, int32_t,
                         const char *, const char *, int32_t) {}
    static void mode(void *data, wl_output *, uint32_t flags, int32_t width, int32_t height, int32_t) {
      if (flags & WL_OUTPUT_MODE_CURRENT) {
        auto &s = *static_cast<kwin_session *>(data); s.width = width; s.height = height;
      }
    }
    static void closed(void *data, zkde_screencast_stream_unstable_v1 *) { static_cast<kwin_session *>(data)->failed = true; }
    static void created(void *data, zkde_screencast_stream_unstable_v1 *, uint32_t node) { static_cast<kwin_session *>(data)->node = node; }
    static void failure(void *data, zkde_screencast_stream_unstable_v1 *, const char *) { static_cast<kwin_session *>(data)->failed = true; }
    static void serial(void *, zkde_screencast_stream_unstable_v1 *, uint32_t, uint32_t) {}
    inline static constexpr wl_registry_listener registry_events {global, removed};
    inline static constexpr wl_output_listener output_events {geometry, mode, nullptr, nullptr, nullptr, nullptr};
    inline static constexpr zkde_screencast_stream_unstable_v1_listener stream_events {closed, created, failure, serial};
  };
}
