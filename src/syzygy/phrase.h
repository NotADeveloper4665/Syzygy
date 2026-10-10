#pragma once
#include "phrase_words.h"
#include <algorithm>
#include <array>
#include <sstream>
#include <stdexcept>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace syzygy {
inline std::string normalize_phrase(std::string phrase) {
  if (phrase.size() > 128) throw std::invalid_argument("Invalid pairing phrase");
  for (char &c : phrase) {
    if (c == '-') c = ' ';
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && (c < 'a' || c > 'z'))
      throw std::invalid_argument("Invalid pairing phrase");
  }
  std::istringstream input(phrase);
  std::string word, out;
  size_t count = 0;
  while (input >> word) {
    if (word.size() > 9) throw std::invalid_argument("Invalid pairing phrase");
    if (count++) out += ' ';
    out += word;
  }
  if (count != 6) throw std::invalid_argument("A pairing phrase must contain six words");
  return out;
}
inline bool known_phrase(const std::string &phrase) {
  std::istringstream input(phrase);
  std::string word;
  while (input >> word) {
    if (std::find(phrase_words.begin(), phrase_words.end(), word) == phrase_words.end()) return false;
  }
  return true;
}
inline std::string random_phrase() {
  std::string out;
  for (int i = 0; i < 6; ++i) {
    std::array<unsigned char, 2> bytes;
    unsigned index;
    do {
      if (RAND_bytes(bytes.data(), bytes.size()) != 1) throw std::runtime_error("Secure phrase generation failed");
      index = (unsigned(bytes[0]) << 8) | bytes[1];
    } while (index >= 62176); // Rejection sampling: eight complete sets of 7772.
    if (i) out += ' ';
    out += phrase_words[index % phrase_words.size()];
  }
  return out;
}
inline std::string phrase_key(const std::string &phrase) {
  const auto normalized = normalize_phrase(phrase);
  constexpr std::string_view salt = "Syzygy pairing phrase v2";
  std::array<unsigned char, 24> key {};
  if (EVP_PBE_scrypt(normalized.data(), normalized.size(),
        reinterpret_cast<const unsigned char *>(salt.data()), salt.size(),
        32768, 8, 1, 64 * 1024 * 1024, key.data(), key.size()) != 1)
    throw std::runtime_error("Pairing phrase derivation failed");
  constexpr char hex[] = "0123456789abcdef";
  std::string out;
  for (auto b : key) { out += hex[b >> 4]; out += hex[b & 15]; }
  OPENSSL_cleanse(key.data(), key.size());
  return out;
}
}
