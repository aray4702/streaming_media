// libwebrtc's p2p/dtls/dtls_utils_unittest.cc (M157), #1006, plus the RFC 7983 classification.
#include <unordered_set>
#include <vector>

#include "sm/p2p/dtls_utils.h"
#include "sm_test.h"

using namespace sm::p2p;

namespace {
std::vector<uint8_t> ToVector(std::span<const uint8_t> s) { return {s.begin(), s.end()}; }

const std::vector<uint8_t> kPacket1 = {
    0x2f, 0x5b, 0x4c, 0x00, 0x23, 0x47, 0xab, 0xe7, 0x90, 0x96, 0xc0, 0xac, 0x2f, 0x25,
    0x40, 0x35, 0x35, 0xa3, 0x81, 0x50, 0x0c, 0x38, 0x0a, 0xf6, 0xd4, 0xd5, 0x7d, 0xbe,
    0x9a, 0xa3, 0xcb, 0xcb, 0x67, 0xb0, 0x77, 0x79, 0x8b, 0x48, 0x60, 0xf8,
};
const std::vector<uint8_t> kPacket2 = {
    0x16, 0xfe, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0e,
    0x00, 0x00, 0x00, 0x00, 0xac, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
}  // namespace

SM_TEST(DtlsUtils, ClassifiesByFirstByte) {
  std::vector<uint8_t> p(20, 0);
  for (int b = 0; b < 256; ++b) {
    p[0] = static_cast<uint8_t>(b);
    SM_EXPECT_EQ(IsDtlsPacket(p), b >= 20 && b <= 63);  // RFC 7983: DTLS is 20-63.
  }
  // Shorter than a record header is never DTLS.
  SM_EXPECT_FALSE(IsDtlsPacket(std::vector<uint8_t>(12, 22)));
  // A ClientHello: a handshake record (22) whose message type (byte 13) is 1. kPacket2 is a ServerHelloDone (14).
  SM_EXPECT_TRUE(IsDtlsHandshakePacket(kPacket2));
  SM_EXPECT_FALSE(IsDtlsClientHelloPacket(kPacket2));
  std::vector<uint8_t> client_hello = kPacket2;
  client_hello[13] = 1;
  SM_EXPECT_TRUE(IsDtlsClientHelloPacket(client_hello));
  client_hello.resize(17);  // Too short to be one.
  SM_EXPECT_FALSE(IsDtlsClientHelloPacket(client_hello));
  std::vector<uint8_t> server_hello = kPacket2;
  server_hello[13] = 2;
  SM_EXPECT_FALSE(IsDtlsClientHelloPacket(server_hello));
  SM_EXPECT_TRUE(IsDtlsHandshakePacket(server_hello));
  std::vector<uint8_t> ccs = kPacket2;
  ccs[0] = 20;  // ChangeCipherSpec counts as a handshake packet.
  SM_EXPECT_TRUE(IsDtlsHandshakePacket(ccs));
  std::vector<uint8_t> app = kPacket2;
  app[0] = 23;
  SM_EXPECT_FALSE(IsDtlsHandshakePacket(app));
  SM_EXPECT_TRUE(IsDtlsPacket(app));
}

SM_TEST(DtlsUtils, PacketStashAdd) {
  PacketStash stash;
  stash.Add(kPacket1);
  SM_EXPECT_EQ(stash.size(), 1);
  SM_EXPECT_TRUE(ToVector(stash.GetNext()) == kPacket1);
  stash.Add(kPacket1);
  SM_EXPECT_EQ(stash.size(), 2);
  SM_EXPECT_TRUE(ToVector(stash.GetNext()) == kPacket1);
  SM_EXPECT_TRUE(ToVector(stash.GetNext()) == kPacket1);
}

SM_TEST(DtlsUtils, PacketStashAddIfUnique) {
  PacketStash stash;
  stash.AddIfUnique(kPacket1);
  SM_EXPECT_EQ(stash.size(), 1);
  SM_EXPECT_TRUE(ToVector(stash.GetNext()) == kPacket1);
  stash.AddIfUnique(kPacket1);
  SM_EXPECT_EQ(stash.size(), 1);
  SM_EXPECT_TRUE(ToVector(stash.GetNext()) == kPacket1);
  stash.AddIfUnique(kPacket2);
  SM_EXPECT_EQ(stash.size(), 2);
  SM_EXPECT_TRUE(ToVector(stash.GetNext()) == kPacket1);
  SM_EXPECT_TRUE(ToVector(stash.GetNext()) == kPacket2);
  stash.AddIfUnique(kPacket2);
  SM_EXPECT_EQ(stash.size(), 2);
}

SM_TEST(DtlsUtils, PacketStashPrune) {
  PacketStash stash;
  stash.AddIfUnique(kPacket1);
  stash.AddIfUnique(kPacket2);
  SM_EXPECT_EQ(stash.size(), 2);
  SM_EXPECT_TRUE(ToVector(stash.GetNext()) == kPacket1);
  SM_EXPECT_TRUE(ToVector(stash.GetNext()) == kPacket2);
  stash.Prune(std::unordered_set<uint32_t>{PacketStash::Hash(kPacket1)});
  SM_EXPECT_EQ(stash.size(), 1);
  SM_EXPECT_TRUE(ToVector(stash.GetNext()) == kPacket2);
}

SM_TEST(DtlsUtils, PacketStashPruneSize) {
  PacketStash stash;
  const std::vector<std::vector<uint8_t>> p = {kPacket1, kPacket2, {0x3}, {0x4}, {0x5}, {0x6}};
  for (const auto& x : p) stash.AddIfUnique(x);
  SM_EXPECT_EQ(stash.size(), 6);
  for (const auto& x : p) SM_EXPECT_TRUE(ToVector(stash.GetNext()) == x);
  stash.Prune(/*max_size=*/6u);  // A no-op.
  for (const auto& x : p) SM_EXPECT_TRUE(ToVector(stash.GetNext()) == x);
  SM_EXPECT_TRUE(ToVector(stash.GetNext()) == p[0]);  // Moves the cursor forward.
  stash.Prune(/*max_size=*/4u);
  SM_EXPECT_EQ(stash.size(), 4);
  for (size_t i = 2; i < 6; ++i) SM_EXPECT_TRUE(ToVector(stash.GetNext()) == p[i]);
}

SM_TEST(DtlsUtils, PacketStashPruneSome) {
  PacketStash stash;
  const std::vector<uint8_t> p1 = {1}, p2 = {2}, p3 = {3};
  stash.AddIfUnique(p1);
  stash.AddIfUnique(p2);
  stash.AddIfUnique(p3);
  SM_EXPECT_EQ(stash.size(), 3);
  SM_EXPECT_TRUE(ToVector(stash.GetNext()) == p1);
  SM_EXPECT_TRUE(ToVector(stash.GetNext()) == p2);
  SM_EXPECT_TRUE(ToVector(stash.GetNext()) == p3);
  SM_EXPECT_TRUE(ToVector(stash.GetNext()) == p1);
  const std::unordered_set<uint32_t> remove = {PacketStash::Hash(p1), PacketStash::Hash(p2)};
  SM_EXPECT_EQ(stash.Prune(remove), 2u);
  SM_EXPECT_EQ(stash.Prune(remove), 0u);
  SM_EXPECT_TRUE(ToVector(stash.GetNext()) == p3);
}
