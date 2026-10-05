#include "cli.h"

#include <stdexcept>
#include <string_view>

namespace syzygy {
  cli_options parse_cli(int argc, char *argv[]) {
    cli_options options;
    std::string encoder;
    bool h264 = false;
    for (int i = 0; i < argc; ++i) {
      const std::string_view arg(argv[i]);
      if (i == 0) {
        options.arguments.emplace_back(arg);
      } else if (arg == "--help") {
        return {{argv[0], "--help"}, false, false};
      } else if (arg == "-s") {
        options.start = true;
      } else if (arg == "-psk") {
        options.print_key = true;
      } else if (arg == "-h264") {
        h264 = true;
      } else if (arg == "-nvec" || arg == "-nvenc" || arg == "-vaapi" || arg == "-software") {
        const std::string selected = arg == "-nvec" || arg == "-nvenc" ? "nvenc" : std::string(arg.substr(1));
        if (!encoder.empty() && encoder != selected) {
          throw std::invalid_argument("Select only one encoder shortcut");
        }
        encoder = selected;
      } else if (arg.substr(0, 2) == "--") {
        // Apollo commands consume the rest of argv verbatim (including passwords).
        if (options.print_key || options.start || h264 || !encoder.empty()) {
          throw std::invalid_argument("Use Syzygy shortcuts separately from Apollo --commands");
        }
        for (; i < argc; ++i) {
          options.arguments.emplace_back(argv[i]);
        }
      } else {
        options.arguments.emplace_back(arg);
      }
    }
    if (!encoder.empty()) {
      options.arguments.emplace_back("encoder=" + encoder);
    }
    if (h264) {
      options.arguments.emplace_back("hevc_mode=1");
      options.arguments.emplace_back("av1_mode=1");
    }
    return options;
  }
}
