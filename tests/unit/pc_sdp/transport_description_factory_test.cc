// The transport description factory (#1063, C6 part 2): libwebrtc M157's
// p2p/base/transport_description_factory_unittest.cc (compare/webrtc/reference/p2p/base/), on sm_test.h. Every case
// but the goog-sped-v1 ones (AddsGoogSpedV1OptionWhenEnabled and the three after it: the WebRTC-IceHandshakeDtls
// trial, off by default, is not ported) and AddsDtlsInStunIceOption (ported below as DtlsInStunIceOption).
#include <memory>
#include <string>
#include <vector>

#include "sm/p2p/p2p_constants.h"
#include "sm/pc/transport_description_factory.h"
#include "sm/tls/ssl_fingerprint.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::pc;

namespace {

struct Fixture {
  Fixture() : cert1(tls::SslIdentity::Create("User1")), cert2(tls::SslIdentity::Create("User2")) {
    f1.set_certificate(cert1);
    f2.set_certificate(cert2);
  }
  void SetInsecure() {
    f1.SetInsecureForTesting();
    f2.SetInsecureForTesting();
  }
  void SetInsecureNoDtls() {
    SetInsecure();
    f1.set_certificate(nullptr);
    f2.set_certificate(nullptr);
  }
  static std::string DigestAlg(const std::shared_ptr<const tls::SslIdentity>& cert) {
    std::string alg;
    cert->certificate().GetSignatureDigestAlgorithm(alg);
    return alg;
  }

  IceCredentialsIterator ice_credentials{std::vector<IceParameters>()};
  TransportDescriptionFactory f1;
  TransportDescriptionFactory f2;
  std::shared_ptr<const tls::SslIdentity> cert1;
  std::shared_ptr<const tls::SslIdentity> cert2;
};

// CheckDesc: no option expected but trickle; random credentials when `ufrag` and `pwd` are empty.
bool CheckDesc(const TransportDescription* desc, const std::string& ufrag, const std::string& pwd,
               const std::string& dtls_alg) {
  if (!desc) return false;
  if (!desc->HasOption("trickle") || desc->HasOption("renomination")) return false;
  if (ufrag.empty() && pwd.empty()) {
    if (desc->ice_ufrag.size() != static_cast<size_t>(p2p::ICE_UFRAG_LENGTH)) return false;
    if (desc->ice_pwd.size() != static_cast<size_t>(p2p::ICE_PWD_LENGTH)) return false;
  } else if (desc->ice_ufrag != ufrag || desc->ice_pwd != pwd) {
    return false;
  }
  if (dtls_alg.empty()) return desc->identity_fingerprint == nullptr;
  return desc->identity_fingerprint && desc->identity_fingerprint->algorithm == dtls_alg &&
         !desc->identity_fingerprint->digest.empty();
}

void VerifyUfragAndPasswordChanged(bool dtls, const TransportDescription* org_desc,
                                   const TransportDescription* restart_desc) {
  SM_ASSERT_TRUE(org_desc != nullptr);
  SM_ASSERT_TRUE(restart_desc != nullptr);
  SM_EXPECT_NE(org_desc->ice_pwd, restart_desc->ice_pwd);
  SM_EXPECT_NE(org_desc->ice_ufrag, restart_desc->ice_ufrag);
  SM_EXPECT_EQ(static_cast<size_t>(p2p::ICE_UFRAG_LENGTH), restart_desc->ice_ufrag.size());
  SM_EXPECT_EQ(static_cast<size_t>(p2p::ICE_PWD_LENGTH), restart_desc->ice_pwd.size());
  if (dtls) {
    SM_EXPECT_FALSE(org_desc->identity_fingerprint->GetRfc4572Fingerprint().empty());
    SM_EXPECT_EQ(org_desc->identity_fingerprint->GetRfc4572Fingerprint(),
                 restart_desc->identity_fingerprint->GetRfc4572Fingerprint());
  }
}

void TestIceRestart(bool dtls) {
  Fixture t;
  if (!dtls) t.SetInsecure();
  TransportOptions options;
  auto offer = t.f1.CreateOffer(options, nullptr, &t.ice_credentials);
  auto answer = t.f2.CreateAnswer(offer.get(), options, true, nullptr, &t.ice_credentials);
  options.ice_restart = true;
  auto restart_offer = t.f1.CreateOffer(options, offer.get(), &t.ice_credentials);
  VerifyUfragAndPasswordChanged(dtls, offer.get(), restart_offer.get());
  auto restart_answer = t.f2.CreateAnswer(restart_offer.get(), options, true, answer.get(), &t.ice_credentials);
  SM_ASSERT_TRUE(restart_answer != nullptr);
  VerifyUfragAndPasswordChanged(dtls, answer.get(), restart_answer.get());
}

void TestIceRenomination(bool dtls) {
  Fixture t;
  if (!dtls) t.SetInsecureNoDtls();
  TransportOptions options;
  auto offer = t.f1.CreateOffer(options, nullptr, &t.ice_credentials);
  SM_ASSERT_TRUE(offer != nullptr);
  SM_EXPECT_FALSE(offer->HasOption("renomination"));
  auto answer = t.f2.CreateAnswer(offer.get(), options, true, nullptr, &t.ice_credentials);
  SM_ASSERT_TRUE(answer != nullptr);
  SM_EXPECT_FALSE(answer->HasOption("renomination"));
  options.enable_ice_renomination = true;
  auto renomination_offer = t.f1.CreateOffer(options, offer.get(), &t.ice_credentials);
  SM_ASSERT_TRUE(renomination_offer != nullptr);
  SM_EXPECT_TRUE(renomination_offer->HasOption("renomination"));
  auto renomination_answer =
      t.f2.CreateAnswer(renomination_offer.get(), options, true, answer.get(), &t.ice_credentials);
  SM_ASSERT_TRUE(renomination_answer != nullptr);
  SM_EXPECT_TRUE(renomination_answer->HasOption("renomination"));
}

SM_TEST(PcTransportDescriptionFactory, TestOfferDtls) {
  Fixture t;
  auto desc = t.f1.CreateOffer(TransportOptions(), nullptr, &t.ice_credentials);
  SM_EXPECT_TRUE(CheckDesc(desc.get(), "", "", Fixture::DigestAlg(t.cert1)));
  SM_EXPECT_TRUE(desc->connection_role == ConnectionRole::kActpass);
}

SM_TEST(PcTransportDescriptionFactory, TestOfferDtlsWithNoIdentity) {
  Fixture t;
  t.f1.set_certificate(nullptr);
  SM_EXPECT_TRUE(t.f1.CreateOffer(TransportOptions(), nullptr, &t.ice_credentials) == nullptr);
}

SM_TEST(PcTransportDescriptionFactory, TestOfferDtlsReofferDtls) {
  Fixture t;
  auto old_desc = t.f1.CreateOffer(TransportOptions(), nullptr, &t.ice_credentials);
  SM_ASSERT_TRUE(old_desc != nullptr);
  auto desc = t.f1.CreateOffer(TransportOptions(), old_desc.get(), &t.ice_credentials);
  SM_EXPECT_TRUE(CheckDesc(desc.get(), old_desc->ice_ufrag, old_desc->ice_pwd, Fixture::DigestAlg(t.cert1)));
}

SM_TEST(PcTransportDescriptionFactory, TestAnswerDefault) {
  Fixture t;
  auto offer = t.f1.CreateOffer(TransportOptions(), nullptr, &t.ice_credentials);
  SM_ASSERT_TRUE(offer != nullptr);
  auto desc = t.f2.CreateAnswer(offer.get(), TransportOptions(), true, nullptr, &t.ice_credentials);
  SM_EXPECT_TRUE(CheckDesc(desc.get(), "", "", Fixture::DigestAlg(t.cert1)));
  desc = t.f2.CreateAnswer(offer.get(), TransportOptions(), true, nullptr, &t.ice_credentials);
  SM_EXPECT_TRUE(CheckDesc(desc.get(), "", "", Fixture::DigestAlg(t.cert1)));
}

SM_TEST(PcTransportDescriptionFactory, TestReanswer) {
  Fixture t;
  auto offer = t.f1.CreateOffer(TransportOptions(), nullptr, &t.ice_credentials);
  SM_ASSERT_TRUE(offer != nullptr);
  auto old_desc = t.f2.CreateAnswer(offer.get(), TransportOptions(), true, nullptr, &t.ice_credentials);
  SM_ASSERT_TRUE(old_desc != nullptr);
  auto desc = t.f2.CreateAnswer(offer.get(), TransportOptions(), true, old_desc.get(), &t.ice_credentials);
  SM_ASSERT_TRUE(desc != nullptr);
  SM_EXPECT_TRUE(CheckDesc(desc.get(), old_desc->ice_ufrag, old_desc->ice_pwd, Fixture::DigestAlg(t.cert1)));
}

SM_TEST(PcTransportDescriptionFactory, TestAnswerDtlsToNoDtls) {
  Fixture t;
  t.f2.SetInsecureForTesting();
  t.f2.set_certificate(nullptr);
  auto offer = t.f1.CreateOffer(TransportOptions(), nullptr, &t.ice_credentials);
  SM_ASSERT_TRUE(offer != nullptr);
  auto desc = t.f2.CreateAnswer(offer.get(), TransportOptions(), true, nullptr, &t.ice_credentials);
  SM_EXPECT_TRUE(CheckDesc(desc.get(), "", "", ""));
}

SM_TEST(PcTransportDescriptionFactory, TestAnswerNoDtlsToDtls) {
  Fixture t;
  t.f1.SetInsecureForTesting();
  t.f1.set_certificate(nullptr);
  auto offer = t.f1.CreateOffer(TransportOptions(), nullptr, &t.ice_credentials);
  SM_ASSERT_TRUE(offer != nullptr);
  auto desc = t.f2.CreateAnswer(offer.get(), TransportOptions(), true, nullptr, &t.ice_credentials);
  SM_EXPECT_TRUE(desc == nullptr);
  t.f2.SetInsecureForTesting();
  desc = t.f2.CreateAnswer(offer.get(), TransportOptions(), true, nullptr, &t.ice_credentials);
  SM_EXPECT_TRUE(CheckDesc(desc.get(), "", "", ""));
}

SM_TEST(PcTransportDescriptionFactory, TestAnswerDtlsToDtls) {
  Fixture t;
  auto offer = t.f1.CreateOffer(TransportOptions(), nullptr, &t.ice_credentials);
  SM_ASSERT_TRUE(offer != nullptr);
  auto desc = t.f2.CreateAnswer(offer.get(), TransportOptions(), true, nullptr, &t.ice_credentials);
  SM_EXPECT_TRUE(CheckDesc(desc.get(), "", "", Fixture::DigestAlg(t.cert2)));
  t.f2.SetInsecureForTesting();
  desc = t.f2.CreateAnswer(offer.get(), TransportOptions(), true, nullptr, &t.ice_credentials);
  SM_EXPECT_TRUE(CheckDesc(desc.get(), "", "", Fixture::DigestAlg(t.cert2)));
}

SM_TEST(PcTransportDescriptionFactory, TestIceRestart) { TestIceRestart(false); }
SM_TEST(PcTransportDescriptionFactory, TestIceRestartWithDtls) { TestIceRestart(true); }
SM_TEST(PcTransportDescriptionFactory, TestIceRenomination) { TestIceRenomination(false); }
SM_TEST(PcTransportDescriptionFactory, TestIceRenominationWithDtls) { TestIceRenomination(true); }

SM_TEST(PcTransportDescriptionFactory, AddsTrickleIceOption) {
  Fixture t;
  TransportOptions options;
  auto offer = t.f1.CreateOffer(options, nullptr, &t.ice_credentials);
  SM_ASSERT_TRUE(offer != nullptr);
  SM_EXPECT_TRUE(offer->HasOption("trickle"));
  auto answer = t.f2.CreateAnswer(offer.get(), options, true, nullptr, &t.ice_credentials);
  SM_EXPECT_TRUE(answer->HasOption("trickle"));
}

// AddsDtlsInStunIceOption: "sped" in the offer when asked, and in the answer only if the offer has it.
SM_TEST(PcTransportDescriptionFactory, DtlsInStunIceOption) {
  Fixture t;
  TransportOptions options;
  options.dtls_handshake_in_stun = true;
  auto offer = t.f1.CreateOffer(options, nullptr, &t.ice_credentials);
  SM_ASSERT_TRUE(offer != nullptr);
  SM_EXPECT_TRUE(offer->HasOption("sped"));
  auto answer = t.f2.CreateAnswer(offer.get(), options, true, nullptr, &t.ice_credentials);
  SM_ASSERT_TRUE(answer != nullptr);
  SM_EXPECT_TRUE(answer->HasOption("sped"));
  auto plain_offer = t.f1.CreateOffer(TransportOptions(), nullptr, &t.ice_credentials);
  answer = t.f2.CreateAnswer(plain_offer.get(), options, true, nullptr, &t.ice_credentials);
  SM_ASSERT_TRUE(answer != nullptr);
  SM_EXPECT_FALSE(answer->HasOption("sped"));
}

SM_TEST(PcTransportDescriptionFactory, CreateOfferIceCredentialsIterator) {
  Fixture t;
  std::vector<IceParameters> credentials = {IceParameters("kalle", "anka", false)};
  IceCredentialsIterator iterator(credentials);
  auto offer = t.f1.CreateOffer(TransportOptions(), nullptr, &iterator);
  SM_EXPECT_EQ(offer->GetIceParameters().ufrag, credentials[0].ufrag);
  SM_EXPECT_EQ(offer->GetIceParameters().pwd, credentials[0].pwd);
}

SM_TEST(PcTransportDescriptionFactory, CreateAnswerIceCredentialsIterator) {
  Fixture t;
  auto offer = t.f1.CreateOffer(TransportOptions(), nullptr, &t.ice_credentials);
  std::vector<IceParameters> credentials = {IceParameters("kalle", "anka", false)};
  IceCredentialsIterator iterator(credentials);
  auto answer = t.f1.CreateAnswer(offer.get(), TransportOptions(), false, nullptr, &iterator);
  SM_EXPECT_EQ(answer->GetIceParameters().ufrag, credentials[0].ufrag);
  SM_EXPECT_EQ(answer->GetIceParameters().pwd, credentials[0].pwd);
}

SM_TEST(PcTransportDescriptionFactory, CreateAnswerToDtlsActpassOffer) {
  Fixture t;
  auto offer = t.f1.CreateOffer(TransportOptions(), nullptr, &t.ice_credentials);
  auto answer = t.f2.CreateAnswer(offer.get(), TransportOptions(), false, nullptr, &t.ice_credentials);
  SM_EXPECT_TRUE(answer->connection_role == ConnectionRole::kActive);
  TransportOptions passive;
  passive.prefer_passive_role = true;
  answer = t.f2.CreateAnswer(offer.get(), passive, false, nullptr, &t.ice_credentials);
  SM_EXPECT_TRUE(answer->connection_role == ConnectionRole::kPassive);
}

SM_TEST(PcTransportDescriptionFactory, CreateAnswerToDtlsActiveOffer) {
  Fixture t;
  auto offer = t.f1.CreateOffer(TransportOptions(), nullptr, &t.ice_credentials);
  offer->connection_role = ConnectionRole::kActive;
  auto answer = t.f2.CreateAnswer(offer.get(), TransportOptions(), false, nullptr, &t.ice_credentials);
  SM_EXPECT_TRUE(answer->connection_role == ConnectionRole::kPassive);
}

SM_TEST(PcTransportDescriptionFactory, CreateAnswerToDtlsPassiveOffer) {
  Fixture t;
  auto offer = t.f1.CreateOffer(TransportOptions(), nullptr, &t.ice_credentials);
  offer->connection_role = ConnectionRole::kPassive;
  auto answer = t.f2.CreateAnswer(offer.get(), TransportOptions(), false, nullptr, &t.ice_credentials);
  SM_EXPECT_TRUE(answer->connection_role == ConnectionRole::kActive);
}

}  // namespace
