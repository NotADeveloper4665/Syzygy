#pragma once
#include <cstdint>
namespace syzygy {
  constexpr std::uint32_t portal_monitor_source = 1;
  constexpr std::uint32_t portal_virtual_source = 4;
  constexpr bool portal_supports_virtual(std::uint32_t available) {
    return (available & portal_virtual_source) != 0;
  }
  constexpr bool portal_is_virtual(std::uint32_t source) {
    return source == portal_virtual_source;
  }
}
