#pragma once
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
namespace syzygy {
class pairing_limiter {
  using clock = std::chrono::steady_clock;
  struct bucket { clock::time_point since; unsigned count; };
  std::mutex mutex;
  std::unordered_map<std::string, bucket> sources;
  bucket total {clock::now(), 0};
public:
  bool allow(const std::string &source, clock::time_point now = clock::now()) {
    std::lock_guard lock(mutex);
    const auto window = std::chrono::minutes(1);
    for (auto it = sources.begin(); it != sources.end();) {
      if (now - it->second.since >= window) it = sources.erase(it);
      else ++it;
    }
    if (now - total.since >= window) total = {now, 0};
    if (total.count >= 200 || (!sources.count(source) && sources.size() >= 1024)) return false;
    auto entry = sources.emplace(source, bucket {now, 0}).first;
    if (entry->second.count >= 20) return false;
    ++entry->second.count;
    ++total.count;
    return true;
  }
};
}
