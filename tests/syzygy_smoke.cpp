#include "syzygy/cli.h"
#include "syzygy/virtual_display.h"
#include "syzygy/key_store.h"
#include "syzygy/pairing_auth.h"
#include "syzygy/phrase.h"
#include "syzygy/pairing_limiter.h"

#include <algorithm>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

void require(bool condition, const char *description) {
  if (!condition) throw std::runtime_error(description);
}

template<class F>
void rejects(F operation, const char *description) {
  bool rejected = false;
  try { operation(); } catch (const std::exception &) { rejected = true; }
  require(rejected, description);
}

syzygy::cli_options parse(std::vector<std::string> arguments) {
  std::vector<char *> values;
  for (auto &argument : arguments) values.push_back(argument.data());
  return syzygy::parse_cli(values.size(), values.data());
}

int main() {
  char root_template[] = "/tmp/syzygy-test-XXXXXX";
  const char *temporary = mkdtemp(root_template);
  if (!temporary) return 1;
  const fs::path root(temporary);
  try {
    auto cli = parse({"syzygy", "-s", "-nvec", "-h264", "-psk", "-p"});
    require(cli.start && cli.print_key, "start and key flags are recognized");
    require(cli.arguments == std::vector<std::string>({"syzygy", "-p", "encoder=nvenc", "hevc_mode=1", "av1_mode=1"}),
            "codec shortcuts preserve legacy UPnP and disable other codecs");
    require(parse({"syzygy", "-nvenc"}).arguments.back() == "encoder=nvenc", "NVENC spelling alias");
    require(parse({"syzygy", "-vaapi"}).arguments.back() == "encoder=vaapi", "VA-API encoder option");
    require(parse({"syzygy", "-software"}).arguments.back() == "encoder=software", "software encoder option");
    require(parse({"syzygy", "-s", "-quic"}).quic, "QUIC shortcut");
    require(parse({"syzygy", "-status"}).status, "Status shortcut");
    require(parse({"syzygy", "-stop"}).stop, "Stop shortcut");
    require(parse({"syzygy", "-s", "-foreground"}).foreground, "Foreground shortcut");
    const auto automatic = parse({"syzygy", "-s", "-auto", "encoder=vaapi", "capture=kwin"});
    require(automatic.start && automatic.automatic && automatic.encoder_override && automatic.capture_override && automatic.arguments == std::vector<std::string>({
      "syzygy", "encoder=vaapi", "capture=kwin"}),
      "automatic mode preserves explicit capture and encoder values");
    const auto automatic_vaapi = parse({"syzygy", "-s", "-auto", "-vaapi"});
    require(automatic_vaapi.start && automatic_vaapi.automatic && automatic_vaapi.encoder_override
      && automatic_vaapi.arguments == std::vector<std::string>({"syzygy", "encoder=vaapi"}),
      "automatic capture selection can be combined with a forced VA-API encoder");
    const auto headless_touch = parse({"syzygy", "-s", "-auto", "--headless-touch", "-psk"});
    require(headless_touch.headless && headless_touch.headless_touch && headless_touch.capture_override && headless_touch.start && headless_touch.print_key,
      "headless touch startup preserves the automatic encoder and key shortcuts");
    require(headless_touch.arguments == std::vector<std::string>({"syzygy", "capture=kwin", "portal_virtual_display=disabled"}),
      "headless touch uses the private native KWin backend");
    const auto headless = parse({"syzygy", "-s", "-auto", "-headless", "-psk"});
    require(headless.headless && headless.capture_override && headless.start && headless.print_key,
      "headless startup preserves the automatic encoder and key shortcuts");
    require(headless.arguments == std::vector<std::string>({"syzygy", "capture=kwin", "portal_virtual_display=disabled"}),
      "headless uses only the private native KWin backend");
    rejects([] { parse({"syzygy", "-headless", "-virtual"}); }, "headless cannot reuse a logged-in portal session");
    rejects([] { parse({"syzygy", "-headless", "capture=kms"}); }, "headless rejects physical capture overrides");
    rejects([] { parse({"syzygy", "-headless", "--creds", "name", "password"}); }, "headless rejects administrative commands");
    const auto virtual_host = parse({"syzygy", "-s", "-auto", "-virtual", "-psk"});
    require(virtual_host.virtual_display && virtual_host.capture_override && virtual_host.automatic,
      "virtual display retains portal selection in automatic mode");
    require(virtual_host.arguments == std::vector<std::string>({"syzygy", "capture=portal", "portal_virtual_display=enabled"}),
      "virtual display config is explicit and opt-in");
    require(!parse({"syzygy", "-s", "-auto"}).virtual_display &&
            !parse({"syzygy", "-auto"}).capture_override,
      "normal automatic startup does not request a virtual monitor");
    require(parse({"syzygy", "-auto", "capture=portal"}).capture_override &&
            parse({"syzygy", "capture=portal", "-auto"}).capture_override,
      "explicit portal capture survives auto in either argument order");
    rejects([] { parse({"syzygy", "-virtual", "capture=kms"}); }, "virtual display rejects KMS capture");
    rejects([] { parse({"syzygy", "capture=x11", "-virtual"}); }, "virtual display rejects X11 capture");
    require(!syzygy::portal_supports_virtual(0) && !syzygy::portal_supports_virtual(3) &&
            syzygy::portal_supports_virtual(4) && syzygy::portal_supports_virtual(7),
      "require advertised virtual-monitor capability");
    require(!syzygy::portal_is_virtual(1) && !syzygy::portal_is_virtual(2) && syzygy::portal_is_virtual(4),
      "virtual mode cannot silently capture a physical screen or window");
    rejects([] { parse({"syzygy", "-nvec", "-software"}); }, "conflicting encoder flags must fail");
    const std::vector<std::string> legacy {"syzygy", "--creds", "test-user", "-psk"};
    auto command = parse(legacy);
    require(command.arguments == legacy && !command.print_key, "command arguments must not become key flags");
    require(!parse({"syzygy", "-psk", "--help"}).print_key, "help must not print a key");
    require(parse({"syzygy", "-psk"}).arguments.size() == 1, "key flag must not toggle legacy UPnP");
    require(syzygy::phrase_key("abacus abacus abacus abacus abacus abacus") == "9099aa9169cd7480309dc3cb217b62371c0486755779f78e", "independent scrypt phrase derivation vector");
    const std::string host_key(48, 'a');
    const auto message = syzygy::pairing_message(std::string(32, 'n'), "desktop-1", "client-certificate");
    const auto proof = syzygy::pairing_proof(host_key, message);
    require(proof == "af4e3810806f0ca196fb292997b38d1a6d68fdbeeaba1be853ba1303725604a2", "independent client-compatible proof vector");
    require(proof.size() == 64 && syzygy::verify_pairing_proof(host_key, message, proof), "valid host-key proof");
    const std::string server_certificate("test server certificate");
    const auto confirmation = syzygy::pairing_confirmation(host_key, message, server_certificate);
    require(confirmation.size() == 64 && syzygy::verify_pairing_confirmation(
        host_key, message, server_certificate, confirmation),
      "valid host confirmation");
    require(!syzygy::verify_pairing_confirmation(
        std::string(48, 'b'), message, server_certificate, confirmation),
      "reject confirmation from another host key");
    require(!syzygy::verify_pairing_confirmation(
        host_key, message, "modified server certificate", confirmation),
      "reject confirmation for a substituted server certificate");
    require(!syzygy::verify_pairing_proof(std::string(48, 'b'), message, proof), "reject proof from another host key");
    require(!syzygy::verify_pairing_proof(host_key, message + "x", proof), "reject modified pairing transcript");
    require(syzygy::pairing_message(std::string(32, 'n'), "desktop-2", "client-certificate") != message,
      "bind proof to the client identity");

    require(parse({"syzygy", "-tails"}).tails, "Tailscale shortcut recognized");
    require(parse({"syzygy", "-tails"}).arguments.back() == "tails_pairing=enabled", "Tailscale shortcut enables option");
    syzygy::pairing_limiter limits;
    const auto now = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i) require(limits.allow("one", now), "initial attempts allowed");
    require(!limits.allow("one", now), "per-source rate limit");
    require(limits.allow("two", now), "independent source allowed");
    require(limits.allow("one", now + std::chrono::minutes(1)), "rate limit expires");
    const auto directory = root / "private";
    fs::create_directory(directory);
    chmod(directory.c_str(), 0700);
    std::ofstream(directory / "syzygy.psk") << std::string(48, 'a') << '\n';
    chmod((directory / "syzygy.psk").c_str(), 0600);
    const auto first = syzygy::load_or_create_key(directory);
    require(!fs::exists(directory / "syzygy.psk"), "legacy key retired after phrase creation");
    require(std::count(first.begin(), first.end(), ' ') == 5 && syzygy::known_phrase(first), "six independently generated words");
    require(syzygy::normalize_phrase("  ABACUS-abacus ABACUS abacus abacus abacus ") == "abacus abacus abacus abacus abacus abacus", "phrase separators and case");
    rejects([] { syzygy::normalize_phrase(std::string(48, 'a')); }, "old hex input must be rejected");
    require(first == syzygy::load_or_create_key(directory), "key must persist across restarts");
    struct stat state {};
    require(stat((directory / "syzygy.phrase").c_str(), &state) == 0 && (state.st_mode & 0777) == 0600,
            "key file must have owner-only read/write permissions");
    require(stat(directory.c_str(), &state) == 0 && (state.st_mode & 0777) == 0700,
            "key directory must be private");
    fs::remove(directory / "syzygy.phrase");
    std::vector<std::future<std::string>> writers;
    for (int i = 0; i < 12; ++i) {
      writers.push_back(std::async(std::launch::async, [&] { return syzygy::load_or_create_key(directory); }));
    }
    const auto concurrent = writers.front().get();
    require(concurrent != first, "separate generated keys must differ");
    for (size_t i = 1; i < writers.size(); ++i) {
      require(writers[i].get() == concurrent, "concurrent startup must converge on one complete key");
    }
    require(std::distance(fs::directory_iterator(directory), fs::directory_iterator()) == 1,
            "temporary key files must be cleaned up");
    chmod((directory / "syzygy.phrase").c_str(), 0644);
    rejects([&] { syzygy::load_or_create_key(directory); }, "insecure key permissions must fail");
    chmod((directory / "syzygy.phrase").c_str(), 0600);
    std::ofstream(directory / "syzygy.phrase") << "invalid-key\n";
    rejects([&] { syzygy::load_or_create_key(directory); }, "malformed key must not be replaced");
    fs::remove(directory / "syzygy.phrase");
    std::ofstream(root / "external") << "unchanged\n";
    fs::create_symlink(root / "external", directory / "syzygy.phrase");
    rejects([&] { syzygy::load_or_create_key(directory); }, "key symlinks must fail");
    fs::remove(directory / "syzygy.phrase");
    mkfifo((directory / "syzygy.phrase").c_str(), 0600);
    rejects([&] { syzygy::load_or_create_key(directory); }, "nonregular key files must fail without blocking");
    fs::create_directory_symlink(directory, root / "directory-link");
    rejects([&] { syzygy::load_or_create_key(root / "directory-link"); }, "directory symlinks must fail");
    chmod(directory.c_str(), 0755);
    rejects([&] { syzygy::load_or_create_key(directory); }, "insecure directory permissions must fail");
    fs::remove_all(root);
    std::cout << "PASS: CLI, key persistence and permissions, concurrent creation, unsafe-file rejection, and pairing proofs\n";
    return 0;
  } catch (const std::exception &error) {
    fs::remove_all(root);
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
