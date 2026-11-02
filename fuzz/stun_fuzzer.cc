// STUN, TURN and ICE messages (#1017, requirements C2 and C10): what
// libwebrtc's test/fuzzers/stun_parser_fuzzer.cc and stun_validator_fuzzer.cc
// feed (compare/webrtc/reference/test/fuzzers/), plus a write/read round trip
// and StunRequestManager::CheckResponse:
//
// - stun_parser_fuzzer.cc: IceMessage::Read on the input, then
//   ValidateMessageIntegrity(""). Here the input is read as each of the three
//   message classes (StunMessage, TurnMessage, IceMessage), whose attribute
//   tables differ, and integrity is checked with "" and with the RFC 5769
//   short-term password, so the corpus's signed vectors reach the HMAC
//   comparison and the kIntegrityOk path.
// - stun_validator_fuzzer.cc: ValidateFingerprint and
//   ValidateMessageIntegrityForTesting("") on the raw bytes (libwebrtc first
//   takes a length-prefixed string from the input with FuzzDataHelper; here
//   the whole input is the message, so the corpus is plain STUN packets), and
//   the GOOG-MESSAGE-INTEGRITY-32 check too.
// - A message that parsed is written and read back into a new message of the
//   same class: that must succeed with the same type and transaction id and
//   write the same bytes again (the same attributes, in order), and Clone()
//   must give the same, or the target aborts. Two kinds of message are left
//   out, because libwebrtc's api/transport/stun.cc does not round-trip them
//   either: one whose Write() fails (Read() accepts an attribute length that
//   is illegal for its type, which Write() refuses), and one with skipped
//   unknown attributes (Read() keeps the header's length, which Write()
//   writes back, so its own bytes don't parse again).
// - StunRequestManager::CheckResponse(span) (#997) with a request outstanding
//   whose transaction id is the input's, so the lookup, the reparse with the
//   request's class and the response checks run: an authenticated binding
//   request, an unauthenticated one and an unauthenticated TURN Allocate.
//
// The crypto hooks are the STUN tests' own HMAC-SHA1 and MD5
// (tests/unit/p2p/test_crypto.cc), with counter bytes for transaction ids, so
// the integrity code runs to its comparison as with libwebrtc's real HMAC, and
// the RFC 5769 vectors in the corpus check OK.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/byte_buffer.h"
#include "sm/p2p/stun.h"
#include "sm/p2p/stun_request.h"
#include "test_crypto.h"

namespace {

using sm::p2p::ByteBufferReader;
using sm::p2p::ByteBufferWriter;
using sm::p2p::StunMessage;

// RFC 5769 section 2.1's short-term credential (the corpus's signed vectors).
const std::string kRfc5769Password = "VOkJxbRl1RmTxUk/WvJxBt";

void InstallCrypto() {
  static const bool installed = [] {
    sm::p2p::testing::InstallTestCrypto();
    return true;
  }();
  (void)installed;
}

void Check(bool ok) {
  if (!ok) std::abort();
}

// Reads `in` as `proto`'s class; if it parses, checks its integrity and the
// write/read round trip.
void ParseAs(const StunMessage& proto, std::span<const uint8_t> in) {
  std::unique_ptr<StunMessage> msg(proto.CreateNew());
  ByteBufferReader buf(in);
  if (!msg->Read(&buf)) return;
  (void)msg->GetNonComprehendedAttributes();
  (void)msg->GetErrorCodeValue();
  (void)msg->IsLegacy();
  msg->ValidateMessageIntegrity("");
  msg->RevalidateMessageIntegrity(kRfc5769Password);

  // Write() refuses an attribute whose length is illegal for its type
  // (LengthValid), which Read() accepts, as in libwebrtc's stun.cc.
  ByteBufferWriter out;
  if (!msg->Write(&out)) return;
  // Read() skips unknown attributes but keeps the header's length, and
  // Write() writes that length (libwebrtc's stun.cc does the same), so only a
  // message whose attributes all parsed comes back from its own bytes.
  if (out.DataView().size() != sm::p2p::kStunHeaderSize + msg->length()) return;
  std::unique_ptr<StunMessage> again(proto.CreateNew());
  ByteBufferReader rbuf(out);
  Check(again->Read(&rbuf));
  Check(again->type() == msg->type());
  Check(again->transaction_id() == msg->transaction_id());
  // The same attributes, in order, with the same encoding: the message writes
  // the same bytes again. (EqualAttributes compares only the first attribute
  // of each type, as libwebrtc's does, so it is false for a message that
  // repeats a type even against itself.)
  ByteBufferWriter out2;
  Check(again->Write(&out2));
  Check(std::ranges::equal(out.DataView(), out2.DataView()));
  (void)again->EqualAttributes(msg.get(), [](int) { return true; });

  auto clone = msg->Clone();
  Check(clone != nullptr);
  ByteBufferWriter out3;
  Check(clone->Write(&out3));
  Check(std::ranges::equal(out.DataView(), out3.DataView()));
}

class Request : public sm::p2p::StunRequest {
 public:
  using StunRequest::StunRequest;
};

// A request of `msg`'s type with the input's transaction id outstanding, then
// the input as its response.
void CheckResponse(std::unique_ptr<StunMessage> msg, bool authenticate, std::span<const uint8_t> in) {
  const auto now = sm::Timestamp::Millis(1'000);
  sm::p2p::StunRequestManager manager([](sm::Timestamp, std::span<const uint8_t>, sm::p2p::StunRequest*) {});
  if (authenticate) msg->AddMessageIntegrity(kRfc5769Password);
  auto request = std::make_unique<Request>(manager, std::move(msg));
  request->SetAuthenticationRequired(authenticate);
  if (!manager.Send(now, std::move(request))) return;
  (void)manager.CheckResponse(now, in);
  manager.Process(now + sm::TimeDelta::Millis(100'000));
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  InstallCrypto();
  const std::span<const uint8_t> in(data, size);

  // stun_validator_fuzzer.cc.
  (void)StunMessage::ValidateFingerprint(in);
  (void)StunMessage::ValidateMessageIntegrityForTesting("", in);
  (void)StunMessage::ValidateMessageIntegrity32ForTesting("", in);
  (void)StunMessage::ValidateMessageIntegrityForTesting(kRfc5769Password, in);

  // stun_parser_fuzzer.cc, as each message class.
  ParseAs(StunMessage(), in);
  ParseAs(sm::p2p::TurnMessage(), in);
  ParseAs(sm::p2p::IceMessage(), in);

  // StunRequestManager::CheckResponse with the input's transaction id.
  if (size >= 20) {
    const std::string id(reinterpret_cast<const char*>(data) + 8, 12);
    CheckResponse(std::make_unique<sm::p2p::IceMessage>(sm::p2p::STUN_BINDING_REQUEST, id), true, in);
    CheckResponse(std::make_unique<sm::p2p::IceMessage>(sm::p2p::STUN_BINDING_REQUEST, id), false, in);
    CheckResponse(std::make_unique<sm::p2p::TurnMessage>(sm::p2p::STUN_ALLOCATE_REQUEST, id), false, in);
  }
  return 0;
}
