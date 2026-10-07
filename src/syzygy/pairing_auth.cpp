#include "pairing_auth.h"

#include <array>
#include <stdexcept>
#include <openssl/crypto.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

namespace syzygy {
  namespace {
    constexpr std::string_view domain = "Syzygy pairing v1\0";

    void append_u32(std::string &out, uint32_t value) {
      for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<char>(value >> shift));
    }

    void append_field(std::string &out, std::string_view field) {
      if (field.size() > UINT32_MAX) throw std::length_error("Syzygy pairing field is too large");
      append_u32(out, static_cast<uint32_t>(field.size()));
      out.append(field);
    }
  }

  std::string pairing_message(std::string_view nonce, std::string_view unique_id, std::string_view certificate) {
    if (nonce.size() != 32 || unique_id.empty() || unique_id.size() > 256 || certificate.empty()) {
      throw std::invalid_argument("Invalid Syzygy pairing challenge fields");
    }
    std::array<unsigned char, SHA256_DIGEST_LENGTH> fingerprint {};
    SHA256(reinterpret_cast<const unsigned char *>(certificate.data()), certificate.size(), fingerprint.data());
    std::string message(domain);
    append_field(message, nonce);
    append_field(message, unique_id);
    append_field(message, std::string_view(reinterpret_cast<const char *>(fingerprint.data()), fingerprint.size()));
    return message;
  }

  std::string pairing_proof(std::string_view host_key, std::string_view message) {
    if (host_key.size() != 48) throw std::invalid_argument("Invalid Syzygy host key");
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest {};
    unsigned int size = 0;
    if (!HMAC(EVP_sha256(), host_key.data(), static_cast<int>(host_key.size()),
          reinterpret_cast<const unsigned char *>(message.data()), message.size(), digest.data(), &size) ||
        size != SHA256_DIGEST_LENGTH) {
      throw std::runtime_error("Syzygy pairing proof generation failed");
    }
    constexpr char hex[] = "0123456789abcdef";
    std::string output;
    output.reserve(size * 2);
    for (unsigned int i = 0; i < size; ++i) {
      output.push_back(hex[digest[i] >> 4]);
      output.push_back(hex[digest[i] & 15]);
    }
    OPENSSL_cleanse(digest.data(), digest.size());
    return output;
  }

  bool verify_pairing_proof(std::string_view host_key, std::string_view message, std::string_view proof) {
    if (proof.size() != 64) return false;
    std::string expected;
    try {
      expected = pairing_proof(host_key, message);
    } catch (...) {
      return false;
    }
    return CRYPTO_memcmp(expected.data(), proof.data(), expected.size()) == 0;
  }

  std::string pairing_confirmation(std::string_view host_key, std::string_view message) {
    std::string confirmation_message("Syzygy server confirmation v1");
    confirmation_message.append(message);
    return pairing_proof(host_key, confirmation_message);
  }

  bool verify_pairing_confirmation(std::string_view host_key, std::string_view message, std::string_view proof) {
    std::string expected;
    try {
      expected = pairing_confirmation(host_key, message);
    } catch (...) {
      return false;
    }
    return proof.size() == expected.size() && CRYPTO_memcmp(expected.data(), proof.data(), expected.size()) == 0;
  }
}
