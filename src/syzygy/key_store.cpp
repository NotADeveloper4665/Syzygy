#include "key_store.h"

#include <array>
#include <cerrno>
#include <stdexcept>
#include <openssl/rand.h>

#ifndef _WIN32
  #include <fcntl.h>
  #include <sys/stat.h>
  #include <unistd.h>
#endif

namespace syzygy {
#ifndef _WIN32
  namespace {
    struct fd_guard {
      int fd;
      explicit fd_guard(int value): fd(value) {}
      ~fd_guard() { if (fd >= 0) close(fd); }
      fd_guard(const fd_guard &) = delete;
      fd_guard &operator=(const fd_guard &) = delete;
    };

    std::string random_hex() {
      std::array<unsigned char, 24> bytes {};
      if (RAND_bytes(bytes.data(), bytes.size()) != 1) {
        throw std::runtime_error("Secure key generation failed");
      }
      constexpr char digits[] = "0123456789abcdef";
      std::string value;
      value.reserve(bytes.size() * 2);
      for (auto byte : bytes) {
        value += digits[byte >> 4];
        value += digits[byte & 15];
      }
      return value;
    }

    std::string read_key(int directory) {
      fd_guard key(openat(directory, "syzygy.psk", O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
      if (key.fd < 0) {
        if (errno == ENOENT) return {};
        throw std::runtime_error("Cannot open Syzygy key file safely");
      }
      struct stat state {};
      if (fstat(key.fd, &state) != 0 || !S_ISREG(state.st_mode) || state.st_uid != geteuid() ||
          (state.st_mode & 0777) != 0600) {
        throw std::runtime_error("Syzygy key file must be a private, owner-owned regular file");
      }
      std::array<char, 50> buffer {};
      size_t size = 0;
      while (size < buffer.size()) {
        auto count = read(key.fd, buffer.data() + size, buffer.size() - size);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) throw std::runtime_error("Cannot read Syzygy key file");
        if (count == 0) break;
        size += count;
      }
      if (size == 49 && buffer[48] == '\n') --size;
      if (size != 48) throw std::runtime_error("Invalid Syzygy key file format");
      for (size_t i = 0; i < size; ++i) {
        if (!((buffer[i] >= '0' && buffer[i] <= '9') || (buffer[i] >= 'a' && buffer[i] <= 'f'))) {
          throw std::runtime_error("Invalid Syzygy key file format");
        }
      }
      return std::string(buffer.data(), size);
    }
  }
#endif

  std::string load_or_create_key(const std::filesystem::path &private_directory) {
#ifdef _WIN32
    throw std::runtime_error("Syzygy key management is not available on Windows until owner-only ACL support is implemented");
#else
    if (mkdir(private_directory.c_str(), 0700) != 0 && errno != EEXIST) {
      throw std::runtime_error("Cannot create Syzygy private key directory");
    }
    fd_guard directory(open(private_directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    struct stat state {};
    if (directory.fd < 0 || fstat(directory.fd, &state) != 0 || state.st_uid != geteuid() ||
        (state.st_mode & 077) != 0) {
      throw std::runtime_error("Syzygy key directory must be private and owned by the current user");
    }
    auto existing = read_key(directory.fd);
    if (!existing.empty()) return existing;

    const auto value = random_hex() + "\n";
    const auto temporary = ".syzygy.psk-" + random_hex();
    fd_guard key(openat(directory.fd, temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (key.fd < 0) throw std::runtime_error("Cannot create Syzygy private key file");
    try {
      size_t written = 0;
      while (written < value.size()) {
        const auto count = write(key.fd, value.data() + written, value.size() - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) throw std::runtime_error("Cannot write Syzygy key file");
        written += count;
      }
      if (fsync(key.fd) != 0) throw std::runtime_error("Cannot flush Syzygy key file");
      // Atomic publication without replacing a key another process already created.
      if (linkat(directory.fd, temporary.c_str(), directory.fd, "syzygy.psk", 0) != 0 && errno != EEXIST) {
        throw std::runtime_error("Cannot publish Syzygy key file");
      }
      if (unlinkat(directory.fd, temporary.c_str(), 0) != 0 || fsync(directory.fd) != 0) {
        throw std::runtime_error("Cannot finalize Syzygy key file");
      }
    } catch (...) {
      unlinkat(directory.fd, temporary.c_str(), 0);
      throw;
    }
    return read_key(directory.fd);
#endif
  }
}
