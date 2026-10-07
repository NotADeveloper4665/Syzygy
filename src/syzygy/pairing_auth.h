#pragma once

#include <string>
#include <string_view>

namespace syzygy {
  // Canonical message signed by the client and authenticated with the host key.
  // The client signs the returned challenge message with its certificate key.
  std::string pairing_message(std::string_view nonce, std::string_view unique_id, std::string_view certificate);
  std::string pairing_proof(std::string_view host_key, std::string_view message);
  bool verify_pairing_proof(std::string_view host_key, std::string_view message, std::string_view proof);
  std::string pairing_confirmation(std::string_view host_key, std::string_view message);
  bool verify_pairing_confirmation(std::string_view host_key, std::string_view message, std::string_view proof);
}
