#pragma once
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <curl/curl.h>
#include <sstream>
#include <string>
#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace syzygy {
namespace pt = boost::property_tree;
inline bool contains_ip(const pt::ptree &values, const std::string &ip) {
  for (const auto &value : values) if (value.second.get_value<std::string>() == ip) return true;
  return false;
}
// Only root-owned tailscaled data is trusted. Shared-in and shared-to nodes
// are deliberately excluded, even when Tailscale permits them to connect.
inline bool same_tailnet(const pt::ptree &status, const pt::ptree &who,
                        const std::string &remote, const std::string &local) {
  try {
    if (status.get<std::string>("BackendState") != "Running" || !status.get<bool>("TUN") ||
        !contains_ip(status.get_child("TailscaleIPs"), local)) return false;
    const auto suffix = "." + status.get<std::string>("CurrentTailnet.MagicDNSSuffix") + ".";
    if (suffix.size() < 4 || !who.get<bool>("Node.MachineAuthorized")) return false;
    for (const auto &entry : status.get_child("Peer")) {
      const auto &peer = entry.second;
      if (!contains_ip(peer.get_child("TailscaleIPs"), remote)) continue;
      const auto dns = peer.get<std::string>("DNSName");
      return peer.get<bool>("InNetworkMap") && !peer.get<bool>("Expired", false) &&
          !peer.get<bool>("ShareeNode", false) && peer.get<std::string>("AltSharerUserID", "0") == "0" &&
          dns.size() > suffix.size() && dns.compare(dns.size() - suffix.size(), suffix.size(), suffix) == 0 &&
          peer.get<std::string>("ID") == who.get<std::string>("Node.StableID") &&
          dns == who.get<std::string>("Node.Name");
    }
  } catch (...) { }
  return false;
}
inline pt::ptree tailscale_local_json(const std::string &path) {
#ifdef _WIN32
  throw std::runtime_error("Tailscale auto-pairing currently requires Linux");
#else
  constexpr auto directory = "/run/tailscale";
  constexpr auto socket = "/run/tailscale/tailscaled.sock";
  struct stat dir {}, sock {};
  if (lstat(directory, &dir) || !S_ISDIR(dir.st_mode) || dir.st_uid != 0 || (dir.st_mode & 0022) ||
      lstat(socket, &sock) || !S_ISSOCK(sock.st_mode) || sock.st_uid != 0)
    throw std::runtime_error("Trusted tailscaled socket unavailable");
  auto handle = curl_easy_init();
  if (!handle) throw std::runtime_error("Tailscale identity lookup unavailable");
  std::string body;
  const auto url = "http://local-tailscaled.sock/localapi/v0/" + path;
  curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
  curl_easy_setopt(handle, CURLOPT_UNIX_SOCKET_PATH, socket);
  curl_easy_setopt(handle, CURLOPT_PROXY, "");
  curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, 1500L);
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, +[](char *data, size_t size, size_t count, void *context) -> size_t {
    auto &out = *static_cast<std::string *>(context);
    if (size && count > 1024 * 1024 / size) return 0;
    const size_t n = size * count;
    if (n > 1024 * 1024 || out.size() > 1024 * 1024 - n) return 0;
    out.append(data, n);
    return n;
  });
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &body);
  auto result = curl_easy_perform(handle);
  long status = 0;
  curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
  curl_easy_cleanup(handle);
  if (result != CURLE_OK || status != 200) throw std::runtime_error("Tailscale identity lookup failed");
  pt::ptree tree;
  std::istringstream input(body);
  pt::read_json(input, tree);
  return tree;
#endif
}
inline bool tailscale_peer(const std::string &remote, const std::string &local) {
  try {
    // Never accept an HTTP header, hostname, or caller-supplied identity.
    // Callers provide normalized socket endpoint addresses only.
    if (remote.find_first_not_of("0123456789abcdefABCDEF:.") != std::string::npos) return false;
    auto status = tailscale_local_json("status");
    auto who = tailscale_local_json("whois?addr=" + remote);
    return same_tailnet(status, who, remote, local);
  } catch (...) { return false; }
}
}
