#pragma once

#include <filesystem>
#include <string>

namespace syzygy {
  // Returns a persistent 192-bit key. Never include its value in diagnostics.
  std::string load_or_create_key(const std::filesystem::path &private_directory);
}
