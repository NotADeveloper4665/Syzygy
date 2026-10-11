#pragma once
#ifdef __linux__
#include <spawn.h>
#include <sys/wait.h>
#include <poll.h>
#include <unistd.h>
#include <csignal>
#include <stdexcept>
#include <string>
#include <vector>
extern char **environ;
namespace syzygy {
class quic_process {
  pid_t pid = -1;
public:
  void start(const std::string &script, const std::string &cert,
             const std::string &key, const std::string &state, int base) {
    if (base < 6 || base > 65504) throw std::runtime_error("QUIC base port is out of range");
    std::vector<std::string> args {"python3", script, "server", "--cert", cert,
      "--key", key, "--state", state, "--base", std::to_string(base),
      "--port", std::to_string(base + 31)};
    std::vector<char*> argv;
    for (auto &arg: args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    int fd[2];
    if (pipe(fd)) throw std::runtime_error("QUIC readiness pipe failed");
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fd[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, fd[0]);
    posix_spawn_file_actions_addclose(&actions, fd[1]);
    const int error = posix_spawnp(&pid, "python3", &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fd[1]);
    pollfd ready {fd[0], POLLIN, 0};
    std::string output;
    if (!error && poll(&ready, 1, 5000) > 0) {
      char bytes[128];
      const auto n = read(fd[0], bytes, sizeof(bytes));
      if (n > 0) output.assign(bytes, n);
    }
    close(fd[0]);
    if (error || output.find("SYZYGY_QUIC_LISTENING") == std::string::npos) {
      if (error) pid = -1;
      throw std::runtime_error("QUIC listener failed: install python3-aioquic and check its port");
    }
  }
  ~quic_process() {
    if (pid > 0) {
      kill(pid, SIGTERM);
      for (int i = 0; i < 50; ++i) {
        if (waitpid(pid, nullptr, WNOHANG) != 0) return;
        usleep(100000);
      }
      kill(pid, SIGKILL);
      waitpid(pid, nullptr, 0);
    }
  }
};
}
#endif
