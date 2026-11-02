// libwebrtc's rtc_base/message_digest.cc (M157): the digest helpers and its RFC 2104 HMAC (#984). The digests
// themselves are the backend's (boringssl_digest.cc).
#include "sm/tls/message_digest.h"

#include <array>
#include <cstring>
#include <vector>

namespace sm::tls {
namespace {

constexpr size_t kBlockSize = 64;  // Valid for SHA-256 and below (message_digest.cc).

std::string HexEncode(std::span<const uint8_t> b) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string s;
  s.reserve(b.size() * 2);
  for (uint8_t v : b) {
    s.push_back(kHex[v >> 4]);
    s.push_back(kHex[v & 15]);
  }
  return s;
}

std::span<const uint8_t> Bytes(std::string_view s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }

}  // namespace

bool IsFips180DigestAlgorithm(std::string_view alg) {
  return alg == kDigestSha1 || alg == kDigestSha224 || alg == kDigestSha256 || alg == kDigestSha384 ||
         alg == kDigestSha512;
}

size_t ComputeDigest(std::string_view alg, std::span<const uint8_t> input, std::span<uint8_t> output) {
  std::unique_ptr<MessageDigest> d = CreateMessageDigest(alg);
  if (!d) return 0;
  d->Update(input.data(), input.size());
  return d->Finish(output.data(), output.size());
}

std::string ComputeDigest(std::string_view alg, std::string_view input) {
  std::array<uint8_t, MessageDigest::kMaxSize> out{};
  const size_t n = ComputeDigest(alg, Bytes(input), out);
  return n ? HexEncode(std::span<const uint8_t>(out.data(), n)) : std::string();
}

size_t ComputeHmac(std::string_view alg, std::span<const uint8_t> key, std::span<const uint8_t> input,
                   std::span<uint8_t> output) {
  std::unique_ptr<MessageDigest> digest = CreateMessageDigest(alg);
  // libwebrtc handles only algorithms with a 64-byte block.
  if (!digest || digest->Size() > 32) return 0;
  const size_t size = digest->Size();
  // The key padded to a block, or hashed first if it is longer than one.
  std::array<uint8_t, kBlockSize> k{};
  if (key.size() > kBlockSize) {
    digest->Update(key.data(), key.size());
    digest->Finish(k.data(), k.size());
  } else {
    std::memcpy(k.data(), key.data(), key.size());
  }
  std::array<uint8_t, kBlockSize> o_pad, i_pad;
  for (size_t i = 0; i < kBlockSize; ++i) {
    o_pad[i] = 0x5c ^ k[i];
    i_pad[i] = 0x36 ^ k[i];
  }
  std::array<uint8_t, MessageDigest::kMaxSize> inner{};
  digest->Update(i_pad.data(), kBlockSize);
  digest->Update(input.data(), input.size());
  digest->Finish(inner.data(), size);
  digest->Update(o_pad.data(), kBlockSize);
  digest->Update(inner.data(), size);
  return digest->Finish(output.data(), output.size());
}

std::string ComputeHmac(std::string_view alg, std::string_view key, std::string_view input) {
  std::array<uint8_t, MessageDigest::kMaxSize> out{};
  const size_t n = ComputeHmac(alg, Bytes(key), Bytes(input), out);
  return n ? HexEncode(std::span<const uint8_t>(out.data(), n)) : std::string();
}

}  // namespace sm::tls
