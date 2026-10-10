/**
 * @file src/main.cpp
 * @brief Definitions for the main entry point for Sunshine.
 */
// standard includes
#include <codecvt>
#include <csignal>
#include <cstdlib>
#include <string_view>
#include <fstream>
#include <filesystem>
#include <iostream>

// local includes
#include "confighttp.h"
#include "display_device.h"
#include "entry_handler.h"
#include "globals.h"
#include "httpcommon.h"
#include "logging.h"
#include "main.h"
#include "nvhttp.h"
#include "process.h"
#include "system_tray.h"
#include "upnp.h"
#include "uuid.h"
#include "video.h"
#include "syzygy/cli.h"
#include "syzygy/key_store.h"
#include "syzygy/phrase.h"

#ifndef _WIN32
  #include <unistd.h>
#endif
#ifdef __linux__
  #include <linux/capability.h>
  #include <sys/syscall.h>
  #include <sys/prctl.h>
#endif

#ifdef _WIN32
  #include "platform/windows/misc.h"
  #include "platform/windows/virtual_display.h"
#endif

#define PROBE_DISPLAY_UUID "38F72B96-B00C-4F21-8B6C-E1BFF1602B0E"

extern "C" {
#include "rswrapper.h"
}

using namespace std::literals;

std::map<int, std::function<void()>> signal_handlers;

void on_signal_forwarder(int sig) {
  signal_handlers.at(sig)();
}

template<class FN>
void on_signal(int sig, FN &&fn) {
  signal_handlers.emplace(sig, std::forward<FN>(fn));

  std::signal(sig, on_signal_forwarder);
}

std::map<std::string_view, std::function<int(const char *name, int argc, char **argv)>> cmd_to_func {
  {"creds"sv, [](const char *name, int argc, char **argv) {
     return args::creds(name, argc, argv);
   }},
  {"help"sv, [](const char *name, int argc, char **argv) {
     return args::help(name);
   }},
  {"version"sv, [](const char *name, int argc, char **argv) {
     return args::version();
   }},
#ifdef _WIN32
  {"restore-nvprefs-undo"sv, [](const char *name, int argc, char **argv) {
     return args::restore_nvprefs_undo();
   }},
#endif
};

#ifdef _WIN32
LRESULT CALLBACK SessionMonitorWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  switch (uMsg) {
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case WM_ENDSESSION:
      {
        // Terminate ourselves with a blocking exit call
        std::cout << "Received WM_ENDSESSION"sv << std::endl;
        lifetime::exit_sunshine(0, false);
        return 0;
      }
    default:
      return DefWindowProc(hwnd, uMsg, wParam, lParam);
  }
}

WINAPI BOOL ConsoleCtrlHandler(DWORD type) {
  if (type == CTRL_CLOSE_EVENT) {
    BOOST_LOG(info) << "Console closed handler called";
    lifetime::exit_sunshine(0, false);
  }
  return FALSE;
}
#endif

#if defined SUNSHINE_TRAY && SUNSHINE_TRAY >= 1
constexpr bool tray_is_enabled = true;
#else
constexpr bool tray_is_enabled = false;
#endif

void mainThreadLoop(const std::shared_ptr<safe::event_t<bool>> &shutdown_event) {
  bool run_loop = false;

  // Conditions that would require the main thread event loop
#ifndef _WIN32
  run_loop = tray_is_enabled;  // On Windows, tray runs in separate thread, so no main loop needed for tray
#endif

  if (!run_loop) {
    BOOST_LOG(info) << "No main thread features enabled, skipping event loop"sv;
    return;
  }

  // Main thread event loop
  BOOST_LOG(info) << "Starting main loop"sv;
  while (true) {
    if (shutdown_event->peek()) {
      BOOST_LOG(info) << "Shutdown event detected, breaking main loop"sv;
      if (tray_is_enabled && config::sunshine.system_tray) {
        system_tray::end_tray();
      }
      break;
    }

    if (tray_is_enabled) {
      system_tray::process_tray_events();
    }

    // Sleep to avoid busy waiting
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

int main(int argc, char *argv[]) {
  lifetime::argv = argv;

  syzygy::cli_options cli;
  try {
    cli = syzygy::parse_cli(argc, argv);
  } catch (const std::exception &error) {
    std::cerr << "Syzygy: " << error.what() << std::endl;
    return 2;
  }
#ifdef __linux__
  if (cli.status || cli.stop || (cli.start && !cli.foreground && !std::getenv("SYZYGY_MANAGED_CHILD"))) {
    const auto manager = std::filesystem::path(SUNSHINE_ASSETS_DIR) / "scripts/syzygy-service.py";
    std::vector<std::string> args {"python3", manager.string(),
        cli.status ? "status" : cli.stop ? "stop" : "start", "--binary",
        std::filesystem::read_symlink("/proc/self/exe").string(), "--"};
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    std::vector<char *> exec_args;
    for (auto &arg : args) exec_args.push_back(arg.data());
    exec_args.push_back(nullptr);
    execvp("python3", exec_args.data());
    std::cerr << "Cannot start Syzygy background manager: Python 3 is required.\n";
    return 2;
  }
#else
  if (cli.status || cli.stop) {
    std::cerr << "Background management is currently supported only on Linux.\n";
    return 2;
  }
#endif
  if (cli.headless) {
#ifdef __linux__
    const auto launcher = std::filesystem::path(SUNSHINE_ASSETS_DIR) / "scripts/syzygy-headless.py";
    std::vector<std::string> args {"python3", launcher.string(), "--binary",
        std::filesystem::read_symlink("/proc/self/exe").string(), "--"};
    // Forward the original shortcuts so -auto, -s and -psk retain their meaning.
    for (int i = 1; i < argc; ++i) if (std::string(argv[i]) != "-headless") args.emplace_back(argv[i]);
    std::vector<char *> exec_args;
    for (auto &arg : args) exec_args.push_back(arg.data());
    exec_args.push_back(nullptr);
    execvp("python3", exec_args.data());
    std::cerr << "Cannot start headless launcher: install Python 3 and the headless runtime dependencies.\n";
#else
    std::cerr << "-headless is supported only on Linux.\n";
#endif
    return 2;
  }
#ifdef __linux__
  if (const char *mode = std::getenv("SYZYGY_HEADLESS_SESSION"); mode && std::string_view(mode) == "1") {
    // The RPM grants KMS capabilities. Native capture does not use them, and
    // retaining them prevents KWin from identifying /proc/<pid>/exe for its
    // executable permission check. Drop them before keys or sessions are loaded.
    __user_cap_header_struct header {_LINUX_CAPABILITY_VERSION_3, 0};
    __user_cap_data_struct empty[2] {};
    const long result = syscall(SYS_capset, &header, empty);
    if (result != 0 || prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) != 0) {
      std::cerr << "Cannot enter unprivileged headless capture mode.\n";
      return 2;
    }
  }
#endif
  // Keep explicit key output separate from config diagnostics and console logs.
  std::ostream key_stdout(std::cout.rdbuf());
  struct restore_stdout {
    std::streambuf *original;
    ~restore_stdout() { std::cout.rdbuf(original); }
  } stdout_guard {std::cout.rdbuf()};
  if (cli.print_key) std::cout.rdbuf(std::cerr.rdbuf());
  std::vector<char *> config_argv;
  for (auto &argument : cli.arguments) config_argv.push_back(argument.data());
  config_argv.push_back(nullptr);

  task_pool_util::TaskPool::task_id_t force_shutdown = nullptr;

#ifdef _WIN32
  // Avoid searching the PATH in case a user has configured their system insecurely
  // by placing a user-writable directory in the system-wide PATH variable.
  SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);

  setlocale(LC_ALL, "C");
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  // Use UTF-8 conversion for the default C++ locale (used by boost::log)
  std::locale utf8_locale(std::locale(), new std::codecvt_utf8<wchar_t>);
  std::locale::global(utf8_locale);
  boost::filesystem::path::imbue(utf8_locale);
#pragma GCC diagnostic pop

  mail::man = std::make_shared<safe::mail_raw_t>();

  // parse config file
  const auto config_result = config::parse(static_cast<int>(cli.arguments.size()), config_argv.data());
  if (config_result) {
    return config_result < 0 ? 2 : 0;
  }

#ifdef __linux__
  if (const char *mode = std::getenv("SYZYGY_HEADLESS_SESSION"); mode && std::string_view(mode) == "1") {
    // Saved physical audio devices do not exist in the private PipeWire session.
    // Clear the optional virtual override here: the CLI parser rejects empty values.
    config::audio.sink = "syzygy_headless";
    config::audio.virtual_sink.clear();
  }
#endif

  if (cli.automatic) {
    // Clear stored manual backend selections after parsing the saved configuration.
    // Empty CLI values are rejected by the legacy config parser, so apply this mode directly.
    if (!cli.encoder_override) {
      config::video.encoder.clear();
      config::modified_config_settings["encoder"] = "";
    }
    if (!cli.capture_override) {
      config::video.capture.clear();
      config::modified_config_settings["capture"] = "";
    }
  }

  if (config::video.portal_virtual_display) {
#if !defined(__linux__)
    std::cerr << "-virtual / portal_virtual_display is a Linux-only option.\n";
    return 2;
#endif
    if (cli.capture_override && config::video.capture != "portal") {
      std::cerr << "Virtual displays require capture=portal on Linux.\n";
      return 2;
    }
    config::video.capture = "portal";
    config::modified_config_settings["capture"] = "portal";
  }

  if (config::sunshine.cmd.name.empty() || cli.print_key) {
#ifdef _WIN32
    if (cli.print_key) {
      std::cerr << "Syzygy: host-key management is not available on Windows until owner-only ACL support is implemented.\n";
      return 2;
    }
#else
    try {
      const auto key = syzygy::load_or_create_key(platf::appdata() / "syzygy");
      if (!cli.print_key || cli.start) nvhttp::set_host_key(syzygy::phrase_key(key));
      if (cli.print_key) {
        std::cerr << "Sensitive pairing phrase: keep this output private. Share it only with trusted clients.\n";
        key_stdout << key << std::endl;
      } else if (isatty(STDOUT_FILENO)) {
        // Show the passkey to the interactive operator, never to file/journal logs.
        key_stdout << "Syzygy pairing phrase: " << key << "\n"
                   << "In Eclipse, select this host and choose Syzygy pairing phrase. "
                   << "Anyone with this passkey receives full host access.\n" << std::flush;
      }
    } catch (const std::exception &error) {
      std::cerr << "Syzygy: " << error.what() << std::endl;
      return 2;
    }
    if (cli.print_key && !cli.start) return 0;
#endif
  }

  auto log_deinit_guard = logging::init(config::sunshine.min_log_level, config::sunshine.log_file);
  if (!log_deinit_guard) {
    BOOST_LOG(error) << "Logging failed to initialize"sv;
  }

  // logging can begin at this point
  // if anything is logged prior to this point, it will appear in stdout, but not in the log viewer in the UI
  // the version should be printed to the log before anything else
  BOOST_LOG(info) << PROJECT_NAME << " version: " << PROJECT_VERSION << " commit: " << PROJECT_VERSION_COMMIT;

  // Log publisher metadata
  log_publisher_data();

  // Log modified_config_settings
  for (auto &[name, val] : config::modified_config_settings) {
    BOOST_LOG(info) << "config: '"sv << name << "' = "sv << val;
  }
  config::modified_config_settings.clear();

  if (!config::sunshine.cmd.name.empty()) {
    auto fn = cmd_to_func.find(config::sunshine.cmd.name);
    if (fn == std::end(cmd_to_func)) {
      BOOST_LOG(fatal) << "Unknown command: "sv << config::sunshine.cmd.name;

      BOOST_LOG(info) << "Possible commands:"sv;
      for (auto &[key, _] : cmd_to_func) {
        BOOST_LOG(info) << '\t' << key;
      }

      return 7;
    }

    return fn->second(argv[0], config::sunshine.cmd.argc, config::sunshine.cmd.argv);
  }

  // Adding guard here first as it also performs recovery after crash,
  // otherwise people could theoretically end up without display output.
  // It also should be destroyed before forced shutdown to expedite the cleanup.
  auto display_device_deinit_guard = display_device::init(platf::appdata() / "display_device.state", config::video);
  if (!display_device_deinit_guard) {
    BOOST_LOG(error) << "Display device session failed to initialize"sv;
  }

#ifdef _WIN32
  // Modify relevant NVIDIA control panel settings if the system has corresponding gpu
  if (nvprefs_instance.load()) {
    // Restore global settings to the undo file left by improper termination of sunshine.exe
    nvprefs_instance.restore_from_and_delete_undo_file_if_exists();
    // Modify application settings for sunshine.exe
    nvprefs_instance.modify_application_profile();
    // Modify global settings, undo file is produced in the process to restore after improper termination
    nvprefs_instance.modify_global_profile();
    // Unload dynamic library to survive driver re-installation
    nvprefs_instance.unload();
  }

  // Wait as long as possible to terminate Sunshine.exe during logoff/shutdown
  SetProcessShutdownParameters(0x100, SHUTDOWN_NORETRY);

  // We must create a hidden window to receive shutdown notifications since we load gdi32.dll
  std::promise<HWND> session_monitor_hwnd_promise;
  auto session_monitor_hwnd_future = session_monitor_hwnd_promise.get_future();
  std::promise<void> session_monitor_join_thread_promise;
  auto session_monitor_join_thread_future = session_monitor_join_thread_promise.get_future();

  std::thread session_monitor_thread([&]() {
    session_monitor_join_thread_promise.set_value_at_thread_exit();

    WNDCLASSA wnd_class {};
    wnd_class.lpszClassName = "Sunshine…19518 tokens truncated…_PORT);
    auto video_port = net::map_port(VIDEO_STREAM_PORT);
    auto audio_port = net::map_port(AUDIO_STREAM_PORT);

    if (ctx.control_server.bind(address_family, control_port)) {
      BOOST_LOG(error) << "Couldn't bind Control server to port ["sv << control_port << "], likely another process already bound to the port"sv;

      return -1;
    }

    boost::system::error_code ec;
    ctx.video_sock.open(protocol, ec);
    if (ec) {
      BOOST_LOG(fatal) << "Couldn't open socket for Video server: "sv << ec.message();

      return -1;
    }

    // Set video socket send buffer size (SO_SENDBUF) to 1MB
    try {
      ctx.video_sock.set_option(boost::asio::socket_base::send_buffer_size(1024 * 1024));
    } catch (...) {
      BOOST_LOG(error) << "Failed to set video socket send buffer size (SO_SENDBUF)";
    }

    ctx.video_sock.bind(udp::endpoint(protocol, video_port), ec);
    if (ec) {
      BOOST_LOG(fatal) << "Couldn't bind Video server to port ["sv << video_port << "]: "sv << ec.message();

      return -1;
    }

    ctx.audio_sock.open(protocol, ec);
    if (ec) {
      BOOST_LOG(fatal) << "Couldn't open socket for Audio server: "sv << ec.message();

      return -1;
    }

    ctx.audio_sock.bind(udp::endpoint(protocol, audio_port), ec);
    if (ec) {
      BOOST_LOG(fatal) << "Couldn't bind Audio server to port ["sv << audio_port << "]: "sv << ec.message();

      return -1;
    }

    ctx.message_queue_queue = std::make_shared<message_queue_queue_t::element_type>(30);

    ctx.video_thread = std::thread {videoBroadcastThread, std::ref(ctx.video_sock)};
    ctx.audio_thread = std::thread {audioBroadcastThread, std::ref(ctx.audio_sock)};
    ctx.control_thread = std::thread {controlBroadcastThread, &ctx.control_server};

    ctx.recv_thread = std::thread {recvThread, std::ref(ctx)};

    return 0;
  }

  void end_broadcast(broadcast_ctx_t &ctx) {
    auto broadcast_shutdown_event = mail::man->event<bool>(mail::broadcast_shutdown);

    broadcast_shutdown_event->raise(true);

    auto video_packets = mail::man->queue<video::packet_t>(mail::video_packets);
    auto audio_packets = mail::man->queue<audio::packet_t>(mail::audio_packets);

    // Minimize delay stopping video/audio threads
    video_packets->stop();
    audio_packets->stop();

    ctx.message_queue_queue->stop();
    ctx.io_context.stop();

    ctx.video_sock.close();
    ctx.audio_sock.close();

    video_packets.reset();
    audio_packets.reset();

    BOOST_LOG(debug) << "Waiting for main listening thread to end..."sv;
    ctx.recv_thread.join();
    BOOST_LOG(debug) << "Waiting for main video thread to end..."sv;
    ctx.video_thread.join();
    BOOST_LOG(debug) << "Waiting for main audio thread to end..."sv;
    ctx.audio_thread.join();
    BOOST_LOG(debug) << "Waiting for main control thread to end..."sv;
    ctx.control_thread.join();
    BOOST_LOG(debug) << "All broadcasting threads ended"sv;

    broadcast_shutdown_event->reset();
  }

  int recv_ping(session_t *session, decltype(broadcast)::ptr_t ref, socket_e type, std::string_view expected_payload, udp::endpoint &peer, std::chrono::milliseconds timeout) {
    auto messages = std::make_shared<message_queue_t::element_type>(30);
    av_session_id_t session_id = std::string {expected_payload};

    // Only allow matches on the peer address for legacy clients
    if (!(session->config.mlFeatureFlags & ML_FF_SESSION_ID_V1)) {
      ref->message_queue_queue->raise(type, peer.address(), messages);
    }
    ref->message_queue_queue->raise(type, session_id, messages);

    auto fg = util::fail_guard([&]() {
      messages->stop();

      // remove message queue from session
      if (!(session->config.mlFeatureFlags & ML_FF_SESSION_ID_V1)) {
        ref->message_queue_queue->raise(type, peer.address(), nullptr);
      }
      ref->message_queue_queue->raise(type, session_id, nullptr);
    });

    auto start_time = std::chrono::steady_clock::now();
    auto current_time = start_time;

    while (current_time - start_time < config::stream.ping_timeout) {
      auto delta_time = current_time - start_time;

      auto msg_opt = messages->pop(config::stream.ping_timeout - delta_time);
      if (!msg_opt) {
        break;
      }

      TUPLE_2D_REF(recv_peer, msg, *msg_opt);
      if (msg.find(expected_payload) != std::string::npos) {
        // Match the new PING payload format
        BOOST_LOG(debug) << "Received ping [v2] from "sv << recv_peer.address() << ':' << recv_peer.port() << " ["sv << util::hex_vec(msg) << ']';
      } else if (!(session->config.mlFeatureFlags & ML_FF_SESSION_ID_V1) && msg == "PING"sv) {
        // Match the legacy fixed PING payload only if the new type is not supported
        BOOST_LOG(debug) << "Received ping [v1] from "sv << recv_peer.address() << ':' << recv_peer.port() << " ["sv << util::hex_vec(msg) << ']';
      } else {
        BOOST_LOG(debug) << "Received non-ping from "sv << recv_peer.address() << ':' << recv_peer.port() << " ["sv << util::hex_vec(msg) << ']';
        current_time = std::chrono::steady_clock::now();
        continue;
      }

      // Update connection details.
      peer = recv_peer;
      return 0;
    }

    BOOST_LOG(error) << "Initial Ping Timeout"sv;
    return -1;
  }

  void videoThread(session_t *session) {
    auto fg = util::fail_guard([&]() {
      session::stop(*session);
    });

    while_starting_do_nothing(session->state);

    auto ref = broadcast.ref();
    auto error = recv_ping(session, ref, socket_e::video, session->video.ping_payload, session->video.peer, config::stream.ping_timeout);
    if (error < 0) {
      return;
    }

    // Enable local prioritization and QoS tagging on video traffic if requested by the client
    auto address = session->video.peer.address();
    session->video.qos = platf::enable_socket_qos(ref->video_sock.native_handle(), address, session->video.peer.port(), platf::qos_data_type_e::video, session->config.videoQosType != 0);

    BOOST_LOG(debug) << "Start capturing Video"sv;
    video::capture(session->mail, session->config.monitor, session);
  }

  void audioThread(session_t *session) {
    auto fg = util::fail_guard([&]() {
      session::stop(*session);
    });

    while_starting_do_nothing(session->state);

    auto ref = broadcast.ref();
    auto error = recv_ping(session, ref, socket_e::audio, session->audio.ping_payload, session->audio.peer, config::stream.ping_timeout);
    if (error < 0) {
      return;
    }

    // Enable local prioritization and QoS tagging on audio traffic if requested by the client
    auto address = session->audio.peer.address();
    session->audio.qos = platf::enable_socket_qos(ref->audio_sock.native_handle(), address, session->audio.peer.port(), platf::qos_data_type_e::audio, session->config.audioQosType != 0);

    BOOST_LOG(debug) << "Start capturing Audio"sv;
    audio::capture(session->mail, session->config.audio, session);
  }

  namespace session {
    std::atomic_uint running_sessions;

    state_e state(session_t &session) {
      return session.state.load(std::memory_order_relaxed);
    }

    inline bool send(session_t& session, const std::string_view &payload) {
      return session.broadcast_ref->control_server.send(payload, session.control.peer);
    }

    std::string uuid(const session_t& session) {
      return session.device_uuid;
    }

    bool uuid_match(const session_t &session, const std::string_view& uuid) {
      return session.device_uuid == uuid;
    }

    bool update_device_info(session_t& session, const std::string& name, const crypto::PERM& newPerm) {
      session.permission = newPerm;
      if (!(newPerm & crypto::PERM::_allow_view)) {
        BOOST_LOG(debug) << "Session: View permission revoked for [" << session.device_name << "], disconnecting...";
        graceful_stop(session);
        return true;
      }

      BOOST_LOG(debug) << "Session: Permission updated for [" << session.device_name << "]";

      if (session.device_name != name) {
        BOOST_LOG(debug) << "Session: Device name changed from [" << session.device_name << "] to [" << name << "]";
        session.device_name = name;
      }

      return false;
    }

    void stop(session_t &session) {
      while_starting_do_nothing(session.state);
      auto expected = state_e::RUNNING;
      auto already_stopping = !session.state.compare_exchange_strong(expected, state_e::STOPPING);
      if (already_stopping) {
        return;
      }

      BOOST_LOG(info) << "SYZYGY_SESSION_STOP " << reinterpret_cast<std::uintptr_t>(&session);
      session.shutdown_event->raise(true);
    }

    void graceful_stop(session_t& session) {
      while_starting_do_nothing(session.state);
      auto expected = state_e::RUNNING;
      auto already_stopping = !session.state.compare_exchange_strong(expected, state_e::STOPPING);
      if (already_stopping) {
        return;
      }

      BOOST_LOG(info) << "SYZYGY_SESSION_STOP " << reinterpret_cast<std::uintptr_t>(&session);

      // reason: graceful termination
      std::uint32_t reason = 0x80030023;

      control_terminate_t plaintext;
      plaintext.header.type = packetTypes[IDX_TERMINATION];
      plaintext.header.payloadLength = sizeof(plaintext.ec);
      plaintext.ec = util::endian::big<uint32_t>(reason);

      // We may not have gotten far enough to have an ENet connection yet
      if (session.control.peer) {
        std::array<std::uint8_t,
          sizeof(control_encrypted_t) + crypto::cipher::round_to_pkcs7_padded(sizeof(plaintext)) + crypto::cipher::tag_size>
          encrypted_payload;
        auto payload = stream::encode_control(&session, util::view(plaintext), encrypted_payload);

        if (send(session, payload)) {
          TUPLE_2D(port, addr, platf::from_sockaddr_ex((sockaddr *) &session.control.peer->address.address));
          BOOST_LOG(warning) << "Couldn't send termination code to ["sv << addr << ':' << port << ']';
        }
      }

      session.shutdown_event->raise(true);
      session.controlEnd.raise(true);
    }

    void join(session_t &session) {
      // Current Nvidia drivers have a bug where NVENC can deadlock the encoder thread with hardware-accelerated
      // GPU scheduling enabled. If this happens, we will terminate ourselves and the service can restart.
      // The alternative is that Sunshine can never start another session until it's manually restarted.
      auto task = []() {
        BOOST_LOG(fatal) << "Hang detected! Session failed to terminate in 10 seconds."sv;
        logging::log_flush();
        lifetime::debug_trap();
      };
      auto force_kill = task_pool.pushDelayed(task, 10s).task_id;
      auto fg = util::fail_guard([&force_kill]() {
        // Cancel the kill task if we manage to return from this function
        task_pool.cancel(force_kill);
      });

      BOOST_LOG(debug) << "Waiting for video to end..."sv;
      session.videoThread.join();
      BOOST_LOG(debug) << "Waiting for audio to end..."sv;
      session.audioThread.join();
      BOOST_LOG(debug) << "Waiting for control to end..."sv;
      session.controlEnd.view();
      // Reset input on session stop to avoid stuck repeated keys
      BOOST_LOG(debug) << "Resetting Input..."sv;
      input::reset(session.input);

      if (!session.undo_cmds.empty()) {
        auto exec_thread = std::thread([cmd_list = session.undo_cmds]{
          for (auto &cmd : cmd_list) {
            std::error_code ec;
            auto env = proc::proc.get_env();
            boost::filesystem::path working_dir = proc::find_working_directory(cmd.cmd, env);
            auto child = platf::run_command(cmd.elevated, true, cmd.cmd, working_dir, env, nullptr, ec, nullptr);
            BOOST_LOG(info) << "Spawning client undo command ["sv << cmd.cmd << "] in ["sv << working_dir << ']';
            if (ec) {
              BOOST_LOG(warning) << "Couldn't spawn ["sv << cmd.cmd << "]: System: "sv << ec.message();
            } else {
              child.detach();
            }
          }
        });

        exec_thread.detach();
      }

      // If this is the last session, invoke the platform callbacks
      if (--running_sessions == 0) {
        bool revert_display_config {config::video.dd.config_revert_on_disconnect};
        if (proc::proc.running()) {
          proc::proc.pause();
        } else {
          // We have no app running and also no clients anymore.
          revert_display_config = true;
        }

        if (revert_display_config) {
          display_device::revert_configuration();
        }

        platf::streaming_will_stop();
      }

      BOOST_LOG(debug) << "Session ended"sv;
    }

    int start(session_t &session, const std::string &addr_string) {
      session.input = input::alloc(session.mail);

      session.broadcast_ref = broadcast.ref();
      if (!session.broadcast_ref) {
        return -1;
      }

      session.control.expected_peer_address = addr_string;
      BOOST_LOG(debug) << "Expecting incoming session connections from "sv << addr_string;

      // Insert this session into the session list
      {
        auto lg = session.broadcast_ref->control_server._sessions.lock();
        session.broadcast_ref->control_server._sessions->push_back(&session);
      }

      auto addr = boost::asio::ip::make_address(addr_string);
      session.video.peer.address(addr);
      session.video.peer.port(0);

      session.audio.peer.address(addr);
      session.audio.peer.port(0);

      session.pingTimeout = std::chrono::steady_clock::now() + config::stream.ping_timeout;

      session.audioThread = std::thread {audioThread, &session};
      session.videoThread = std::thread {videoThread, &session};

      BOOST_LOG(info) << "SYZYGY_SESSION_START " << reinterpret_cast<std::uintptr_t>(&session)
                      << " " << (session.config.monitor.videoFormat == 0 ? "H.264" : session.config.monitor.videoFormat == 1 ? "HEVC" : "AV1");
      session.state.store(state_e::RUNNING, std::memory_order_relaxed);

      // If this is the first session, invoke the platform callbacks
      if (++running_sessions == 1) {
        platf::streaming_will_start();
        proc::proc.resume();
      }

      if (!session.do_cmds.empty()) {
        auto exec_thread = std::thread([cmd_list = session.do_cmds]{
          for (auto &cmd : cmd_list) {
            std::error_code ec;
            auto env = proc::proc.get_env();
            boost::filesystem::path working_dir = proc::find_working_directory(cmd.cmd, env);
            auto child = platf::run_command(cmd.elevated, true, cmd.cmd, working_dir, env, nullptr, ec, nullptr);
            BOOST_LOG(info) << "Spawning client do command ["sv << cmd.cmd << "] in ["sv << working_dir << ']';
            if (ec) {
              BOOST_LOG(warning) << "Couldn't spawn ["sv << cmd.cmd << "]: System: "sv << ec.message();
            } else {
              child.detach();
            }
          }
        });

        exec_thread.detach();
      }

      return 0;
    }

    std::shared_ptr<session_t> alloc(config_t &config, rtsp_stream::launch_session_t &launch_session) {
      auto session = std::make_shared<session_t>();

      auto mail = std::make_shared<safe::mail_raw_t>();

      session->shutdown_event = mail->event<bool>(mail::shutdown);
      session->launch_session_id = launch_session.id;
      session->device_name = launch_session.device_name;
      session->device_uuid = launch_session.unique_id;
      session->permission = launch_session.perm;

      session->do_cmds = std::move(launch_session.client_do_cmds);
      session->undo_cmds = std::move(launch_session.client_undo_cmds);

      session->config = config;

      session->control.connect_data = launch_session.control_connect_data;
      session->control.feedback_queue = mail->queue<platf::gamepad_feedback_msg_t>(mail::gamepad_feedback);
      session->control.hdr_queue = mail->event<video::hdr_info_t>(mail::hdr);
      session->control.legacy_input_enc_iv = launch_session.iv;
      session->control.cipher = crypto::cipher::gcm_t {
        launch_session.gcm_key,
        false
      };

      session->video.idr_events = mail->event<bool>(mail::idr);
      session->video.invalidate_ref_frames_events = mail->event<std::pair<int64_t, int64_t>>(mail::invalidate_ref_frames);
      session->video.lowseq = 0;
      session->video.ping_payload = launch_session.av_ping_payload;
      if (config.encryptionFlagsEnabled & SS_ENC_VIDEO) {
        BOOST_LOG(info) << "Video encryption enabled"sv;
        session->video.cipher = crypto::cipher::gcm_t {
          launch_session.gcm_key,
          false
        };
        session->video.gcm_iv_counter = 0;
      }

      constexpr auto max_block_size = crypto::cipher::round_to_pkcs7_padded(2048);

      util::buffer_t<char> shards {RTPA_TOTAL_SHARDS * max_block_size};
      util::buffer_t<uint8_t *> shards_p {RTPA_TOTAL_SHARDS};

      for (auto x = 0; x < RTPA_TOTAL_SHARDS; ++x) {
        shards_p[x] = (uint8_t *) &shards[x * max_block_size];
      }

      // Audio FEC spans multiple audio packets,
      // therefore its session specific
      session->audio.shards = std::move(shards);
      session->audio.shards_p = std::move(shards_p);

      session->audio.fec_packet.rtp.header = 0x80;
      session->audio.fec_packet.rtp.packetType = 127;
      session->audio.fec_packet.rtp.timestamp = 0;
      session->audio.fec_packet.rtp.ssrc = 0;

      session->audio.fec_packet.fecHeader.payloadType = 97;
      session->audio.fec_packet.fecHeader.ssrc = 0;

      session->audio.cipher = crypto::cipher::cbc_t {
        launch_session.gcm_key,
        true
      };

      session->audio.ping_payload = launch_session.av_ping_payload;
      session->audio.avRiKeyId = util::endian::big(*(std::uint32_t *) launch_session.iv.data());
      session->audio.sequenceNumber = 0;
      session->audio.timestamp = 0;

      session->control.peer = nullptr;
      session->state.store(state_e::STOPPED, std::memory_order_relaxed);

      session->mail = std::move(mail);

      return session;
    }
  }  // namespace session
}  // namespace stream
