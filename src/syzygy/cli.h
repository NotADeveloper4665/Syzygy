#pragma once

#include <string>
#include <vector>

namespace syzygy {
  struct cli_options {
    std::vector<std::string> arguments;
    bool start = false;
    bool print_key = false;
    bool automatic = false;
    bool encoder_override = false;
  };

  // Preserve Apollo's argument grammar while translating Syzygy shortcuts.
  cli_options parse_cli(int argc, char *argv[]);
}
