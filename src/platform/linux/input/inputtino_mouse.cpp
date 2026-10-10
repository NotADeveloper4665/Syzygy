/**
 * @file src/platform/linux/input/inputtino_mouse.cpp
 * @brief Definitions for inputtino mouse input handling.
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
#include "inputtino_mouse.h"
#include "src/config.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/utility.h"

using namespace std::literals;

namespace platf::mouse {

  void move(input_raw_t *raw, int deltaX, int deltaY) {
#ifdef SUNSHINE_BUILD_WAYLAND
    if (syzygy::headless::active()) {
      syzygy::headless::kwin_session::instance().send([&](auto *input) {
        org_kde_kwin_fake_input_pointer_motion(input, wl_fixed_from_int(deltaX), wl_fixed_from_int(deltaY));
      });
      return;
    }
#endif
    if (raw->mouse) {
      (*raw->mouse).move(deltaX, deltaY);
    }
  }

  void move_abs(input_raw_t *raw, const touch_port_t &touch_port, float x, float y) {
#ifdef SUNSHINE_BUILD_WAYLAND
    if (syzygy::headless::active()) {
      auto &session = syzygy::headless::kwin_session::instance();
      if (touch_port.width <= 0 || touch_port.height <= 0) return;
      session.send([&](auto *input) {
        org_kde_kwin_fake_input_pointer_motion_absolute(input,
          wl_fixed_from_double(std::clamp(double(x) / touch_port.width, 0.0, 1.0) * (session.screen_width() - 1)),
          wl_fixed_from_double(std::clamp(double(y) / touch_port.height, 0.0, 1.0) * (session.screen_height() - 1)));
      });
      return;
    }
#endif
    if (raw->mouse) {
      (*raw->mouse).move_abs(x, y, touch_port.width, touch_port.height);
    }
  }

  void button(input_raw_t *raw, int button, bool release) {
#ifdef SUNSHINE_BUILD_WAYLAND
    if (syzygy::headless::active()) {
      uint32_t code;
      switch (button) {
        case BUTTON_LEFT: code = BTN_LEFT; break;
        case BUTTON_RIGHT: code = BTN_RIGHT; break;
        case BUTTON_MIDDLE: code = BTN_MIDDLE; break;
        case BUTTON_X1: code = BTN_SIDE; break;
        case BUTTON_X2: code = BTN_EXTRA; break;
        default: return;
      }
      syzygy::headless::kwin_session::instance().send([&](auto *input) {
        org_kde_kwin_fake_input_button(input, code, release ? 0 : 1);
      });
      return;
    }
#endif
    if (raw->mouse) {
      inputtino::Mouse::MOUSE_BUTTON btn_type;
      switch (button) {
        case BUTTON_LEFT:
          btn_type = inputtino::Mouse::LEFT;
          break;
        case BUTTON_MIDDLE:
          btn_type = inputtino::Mouse::MIDDLE;
          break;
        case BUTTON_RIGHT:
          btn_type = inputtino::Mouse::RIGHT;
          break;
        case BUTTON_X1:
          btn_type = inputtino::Mouse::SIDE;
          break;
        case BUTTON_X2:
          btn_type = inputtino::Mouse::EXTRA;
          break;
        default:
          BOOST_LOG(warning) << "Unknown mouse button: " << button;
          return;
      }
      if (release) {
        (*raw->mouse).release(btn_type);
      } else {
        (*raw->mouse).press(btn_type);
      }
    }
  }

  void scroll(input_raw_t *raw, int high_res_distance) {
#ifdef SUNSHINE_BUILD_WAYLAND
    if (syzygy::headless::active()) {
      syzygy::headless::kwin_session::instance().send([&](auto *input) {
        org_kde_kwin_fake_input_axis(input, 0, wl_fixed_from_double(-1 * high_res_distance / 120.0 * 15.0));
      });
      return;
    }
#endif
    if (raw->mouse) {
      (*raw->mouse).vertical_scroll(high_res_distance);
    }
  }

  void hscroll(input_raw_t *raw, int high_res_distance) {
#ifdef SUNSHINE_BUILD_WAYLAND
    if (syzygy::headless::active()) {
      syzygy::headless::kwin_session::instance().send([&](auto *input) {
        org_kde_kwin_fake_input_axis(input, 1, wl_fixed_from_double(1 * high_res_distance / 120.0 * 15.0));
      });
      return;
    }
#endif
    if (raw->mouse) {
      (*raw->mouse).horizontal_scroll(high_res_distance);
    }
  }

  util::point_t get_location(input_raw_t *raw) {
    if (raw->mouse) {
      // TODO: decide what to do after https://github.com/games-on-whales/inputtino/issues/6 is resolved.
      // TODO: auto x = (*raw->mouse).get_absolute_x();
      // TODO: auto y = (*raw->mouse).get_absolute_y();
      return {0, 0};
    }
    return {0, 0};
  }
}  // namespace platf::mouse
