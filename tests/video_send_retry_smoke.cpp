#include "src/platform/linux/video_send_retry.h"

#include <cassert>
#include <fcntl.h>
#include <unistd.h>

int main() {
  using namespace std::chrono;
  using platf::detail::send_video_datagrams;

  int pipe_fds[2];
  assert(pipe2(pipe_fds, O_NONBLOCK) == 0);
  const char datagram = 'v';
  auto submit = [&] {
    return write(pipe_fds[1], &datagram, sizeof(datagram));
  };
  assert(send_video_datagrams(pipe_fds[1], steady_clock::now() + 50ms, submit) == 1);
  while (submit() == 1) {}
  assert(errno == EAGAIN || errno == EWOULDBLOCK);

  const auto start = steady_clock::now();
  assert(send_video_datagrams(pipe_fds[1], start + 5ms, submit) == -1);
  assert(errno == ETIMEDOUT);
  assert(steady_clock::now() - start < 1s);

  char received;
  while (read(pipe_fds[0], &received, sizeof(received)) == 1) {
    assert(received == datagram);
  }
  assert(send_video_datagrams(pipe_fds[1], steady_clock::now() + 50ms, submit) == 1);

  int attempts = 0;
  auto interrupted = [&] {
    ++attempts;
    errno = EINTR;
    return -1;
  };
  assert(send_video_datagrams(pipe_fds[1], steady_clock::now() + 5ms, interrupted) == -1);
  assert(errno == ETIMEDOUT);
  assert(attempts > 0);

  close(pipe_fds[1]);
  close(pipe_fds[0]);
}
