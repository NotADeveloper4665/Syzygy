#include "syzygy/cli.h"
#include "syzygy/key_store.h"
#include "syzygy/pairing_auth.h"

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
    const auto automatic = parse({"syzygy", "-s", "-auto", "encoder=vaapi", "capture=kwin"});
    require(automatic.start && automatic.automatic && !automatic.encoder_override && automatic.arguments == std::vector<std::string>({
      "syzygy", "encoder=vaapi", "capture=kwin"}),
      "automatic mode must be represented without invalid empty config arguments");
    const auto automatic_vaapi = parse({"syzygy", "-s", "-auto", "-vaapi"});
    require(automatic_vaapi.start && automatic_vaapi.automatic && automatic_vaapi.encoder_override
      && automatic_vaapi.arguments == std::vector<std::string>({"syzygy", "encoder=vaapi"}),
      "automatic capture selection can be combined with a forced VA-API encoder");
    rejects([] { parse({"syzygy", "-nvec", "-software"}); }, "conflicting encoder flags must fail");
    const std::vector<std::string> legacy {"syzygy", "--creds", "test-user", "-psk"};
    auto command = parse(legacy);
    require(command.arguments == legacy && !command.print_key, "command arguments must not become key flags");
    require(!parse({"syzygy", "-psk", "--help"}).print_key, "help must not print a key");
    require(parse({"syzygy", "-psk"}).arguments.size() == 1, "key flag must not toggle legacy UPnP");
    const std::string host_key(48, 'a');
    const auto message = syzygy::pairing_message(std::string(32, 'n'), "desktop-1", "client-certificate");
    const auto proof = syzygy::pairing_proof(host_key, message);
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

    const auto directory = root / "private";
    const auto first = syzygy::load_or_create_key(directory);
    require(first.size() == 48, "192-bit key length");
    require(first == syzygy::load_or_create_key(directory), "key must persist across restarts");
    struct stat state {};
    require(stat((directory / "syzygy.psk").c_str(), &state) == 0 && (state.st_mode & 0777) == 0600,
            "key file must have owner-only read/write permissions");
    require(stat(directory.c_str(), &state) == 0 && (state.st_mode & 0777) == 0700,
            "key directory must be private");
    fs::remove(directory / "syzygy.psk");
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
    chmod((directory / "syzygy.psk").c_str(), 0644);
    rejects([&] { syzygy::load_or_create_key(directory); }, "insecure key permissions must fail");
    chmod((directory / "syzygy.psk").c_str(), 0600);
    std::ofstream(directory / "syzygy.psk") << "invalid-key\n";
    rejects([&] { syzygy::load_or_create_key(directory); }, "malformed key must not be replaced");
    fs::remove(directory / "syzygy.psk");
    std::ofstream(root / "external") << "unchanged\n";
    fs::create_symlink(root / "external", directory / "syzygy.psk");
    rejects([&] { syzygy::load_or_create_key(directory); }, "key symlinks must fail");
    fs::remove(directory / "syzygy.psk");
    mkfifo((directory / "syzygy.psk").c_str(), 0600);
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
