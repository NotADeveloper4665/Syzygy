#pragma once

#include <cerrno>
#include <chrono>
#include <poll.h>

namespace platf::detail {
  // Each batch shares a single deadline, including partial sends and GSO fallback.
  template <class Send>
  auto send_video_datagrams(int socket, std::chrono::steady_clock::time_point deadline, Send send) {
    using namespace std::chrono;
    while (steady_clock::now() < deadline) {
      const auto sent = send();
      if (sent >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
        return sent;
      }
      if (errno == EINTR) {
        continue;
      }

      const auto remaining = ceil<milliseconds>(deadline - steady_clock::now());
      if (remaining.count() <= 0) {
        break;
      }
      pollfd descriptor {socket, POLLOUT, 0};
      const int ready = poll(&descriptor, 1, static_cast<int>(remaining.count()));
      if (ready < 0 && errno != EINTR) {
        return decltype(sent)(-1);
      }
      if (ready > 0 && !(descriptor.revents & POLLOUT)) {
        errno = EIO;
        return decltype(sent)(-1);
      }
    }
    errno = ETIMEDOUT;
    return decltype(send())(-1);
  }
}
