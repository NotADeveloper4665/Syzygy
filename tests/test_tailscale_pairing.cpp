#include "syzygy/tailscale_pairing.h"
#include <iostream>
#include <stdexcept>
using boost::property_tree::ptree;
ptree json(const char *value) { ptree tree; std::istringstream input(value); read_json(input, tree); return tree; }
void check(bool value) { if (!value) throw std::runtime_error("Tailscale identity policy test failed"); }
int main() {
  auto status = json(R"({"BackendState":"Running","TUN":true,"TailscaleIPs":["100.64.0.1"],"CurrentTailnet":{"MagicDNSSuffix":"test.ts.net"},"Peer":{"nodekey:test":{"ID":"n1","DNSName":"client.test.ts.net.","TailscaleIPs":["100.64.0.2"],"InNetworkMap":true}}})");
  auto who = json(R"({"Node":{"StableID":"n1","Name":"client.test.ts.net.","MachineAuthorized":true}})");
  auto allowed = [&](const ptree &s, const ptree &w) { return syzygy::same_tailnet(s, w, "100.64.0.2", "100.64.0.1"); };
  check(allowed(status, who));
  check(!syzygy::same_tailnet(status, who, "100.64.0.3", "100.64.0.1"));
  check(!syzygy::same_tailnet(status, who, "100.64.0.2", "192.168.1.1"));
  for (const auto *field : {"ShareeNode", "Expired"}) {
    auto changed = status; changed.put(std::string("Peer.nodekey:test.") + field, true);
    check(!allowed(changed, who));
  }
  auto changed = status; changed.put("Peer.nodekey:test.InNetworkMap", false); check(!allowed(changed, who));
  changed = status; changed.put("Peer.nodekey:test.AltSharerUserID", 123); check(!allowed(changed, who));
  changed = status; changed.put("Peer.nodekey:test.DNSName", "client.foreign.ts.net."); check(!allowed(changed, who));
  changed = status; changed.put("TUN", false); check(!allowed(changed, who));
  auto identity = who; identity.put("Node.StableID", "forged"); check(!allowed(status, identity));
  identity = who; identity.put("Node.MachineAuthorized", false); check(!allowed(status, identity));
  check(!allowed(ptree{}, who));
  std::cout << "PASS: same-tailnet identity, local interface, sharing, expiry, daemon state and malformed-data rejection\n";
}
