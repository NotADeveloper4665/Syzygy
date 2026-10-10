#pragma once

#include <filesystem>
#include <string>

namespace syzygy {
  // Returns a persistent six-word random pairing phrase. Never include its value in diagnostics.
  std::string load_or_create_key(const std::filesystem::path &private_directory);
}
