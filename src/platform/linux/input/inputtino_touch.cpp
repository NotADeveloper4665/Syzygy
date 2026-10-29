/**
 * @file src/platform/linux/input/inputtino_touch.cpp
 * @brief Definitions for inputtino touch input handling.
 */
// lib includes
#include <boost/locale.hpp>
#include <inputtino/input.hpp>
#include <libevdev/libevdev.h>

// local includes
#include "inputtino_common.h"
#ifdef SUNSHINE_BUILD_WAYLAND
#include "../kwin_headless.h"
#endif
#include "inputtino_touch.h"
#include "src/config.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/utility.h"

using namespace std::literals;

namespace platf::touch {
  void update(client_input_raw_t *raw, const touch_port_t &touch_port, const touch_input_t &touch) {
#ifdef SUNSHINE_BUILD_WAYLAND
    // Headless sessions must never emit uinput touch devices: those are visible to
    // the host's physical desktop. Send touch directly to the owned private KWin
    // compositor instead, using client-scoped IDs to avoid cross-client collisions.
    if (syzygy::headless::active()) {
      auto &session = syzygy::headless::kwin_session::instance();
      if (touch_port.width <= 0 || touch_port.height <= 0) return;
      const auto x = wl_fixed_from_double(
        std::clamp(double(touch.x), 0.0, 1.0) * std::max(1, session.screen_width() - 1));
      const auto y = wl_fixed_from_double(
        std::clamp(double(touch.y), 0.0, 1.0) * std::max(1, session.screen_height() - 1));
      const uint32_t id = raw->headless_touch_id_base + (touch.pointerId & 0xffffu);
      session.send([&](auto *input) {
        if (wl_proxy_get_version(reinterpret_cast<wl_proxy *>(input)) < 2) {
          BOOST_LOG(warning) << "Headless touch requires KWin fake-input protocol version 2 or newer";
          return;
        }
        switch (touch.eventType) {
          case LI_TOUCH_EVENT_HOVER:
            org_kde_kwin_fake_input_pointer_motion_absolute(input, x, y);
            break;
          case LI_TOUCH_EVENT_DOWN:
            org_kde_kwin_fake_input_touch_down(input, id, x, y);
            break;
          case LI_TOUCH_EVENT_MOVE:
            org_kde_kwin_fake_input_touch_motion(input, id, x, y);
            break;
          case LI_TOUCH_EVENT_CANCEL:
          case LI_TOUCH_EVENT_UP:
            org_kde_kwin_fake_input_touch_up(input, id);
            break;
          case LI_TOUCH_EVENT_HOVER_LEAVE:
            break;
          default:
            break;
        }
        org_kde_kwin_fake_input_touch_frame(input);
      });
      return;
    }
#endif
    if (raw->touch) {
      switch (touch.eventType) {
        case LI_TOUCH_EVENT_HOVER:
        case LI_TOUCH_EVENT_DOWN:
        case LI_TOUCH_EVENT_MOVE:
          {
            // Convert our 0..360 range to -90..90 relative to Y axis
            int adjusted_angle = touch.rotation;

            if (adjusted_angle > 90 && adjusted_angle < 270) {
              // Lower hemisphere
              adjusted_angle = 180 - adjusted_angle;
            }

            // Wrap the value if it's out of range
            if (adjusted_angle > 90) {
              adjusted_angle -= 360;
            } else if (adjusted_angle < -90) {
              adjusted_angle += 360;
            }
            (*raw->touch).place_finger(touch.pointerId, touch.x, touch.y, touch.pressureOrDistance, adjusted_angle);
            break;
          }
        case LI_TOUCH_EVENT_CANCEL:
        case LI_TOUCH_EVENT_UP:
        case LI_TOUCH_EVENT_HOVER_LEAVE:
          {
            (*raw->touch).release_finger(touch.pointerId);
            break;
          }
          // TODO: LI_TOUCH_EVENT_CANCEL_ALL
      }
    }
  }
}  // namespace platf::touch
