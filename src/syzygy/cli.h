#pragma once

#include <string>
#include <vector>

namespace syzygy {
  struct cli_options {
    std::vector<std::string> arguments;
    bool start = false;
    bool print_key = false;
  };

  // Preserve Apollo's argument grammar while translating Syzygy shortcuts.
  cli_options parse_cli(int argc, char *argv[]);
}
