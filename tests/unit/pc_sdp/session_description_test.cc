// The session description model and JSEP objects (#1062, C6 part 1): libwebrtc M157's
// pc/session_description_unittest.cc and pc/jsep_session_description_unittest.cc (compare/webrtc/reference/pc/),
// every case, on sm_test.h (AddHostnameCandidate and SerializeDeserializeWithHostnameCandidate since #1152).
#include <memory>
#include <string>
#include <vector>

#include "sm/p2p/p2p_constants.h"
#include "sm/pc/jsep.h"
#include "sm/pc/session_description.h"
#include "sm/pc/webrtc_sdp.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::pc;
using p2p::IceCandidateType;
using p2p::SocketAddress;

namespace {

SocketAddress Addr(const char* ip, uint16_t port) { return SocketAddress(*p2p::IpAddress::FromString(ip), port); }

using Level = MediaContentDescription::AttributeLevel;

SM_TEST(PcMediaContentDescription, ExtmapAllowMixedDefaultValue) {
  VideoContentDescription video_desc;
  SM_EXPECT_TRUE(video_desc.extmap_allow_mixed_level() == Level::kMedia);
}

SM_TEST(PcMediaContentDescription, SetExtmapAllowMixed) {
  VideoContentDescription video_desc;
  video_desc.set_extmap_allow_mixed_level(Level::kNone);
  SM_EXPECT_TRUE(video_desc.extmap_allow_mixed_level() == Level::kNone);
  video_desc.set_extmap_allow_mixed_level(Level::kMedia);
  SM_EXPECT_TRUE(video_desc.extmap_allow_mixed_level() == Level::kMedia);
  video_desc.set_extmap_allow_mixed_level(Level::kSession);
  SM_EXPECT_TRUE(video_desc.extmap_allow_mixed_level() == Level::kSession);
  // Not allowed to downgrade from kSession to kMedia.
  video_desc.set_extmap_allow_mixed_level(Level::kMedia);
  SM_EXPECT_TRUE(video_desc.extmap_allow_mixed_level() == Level::kSession);
  // Always okay to set not supported.
  video_desc.set_extmap_allow_mixed_level(Level::kNone);
  SM_EXPECT_TRUE(video_desc.extmap_allow_mixed_level() == Level::kNone);
  video_desc.set_extmap_allow_mixed_level(Level::kMedia);
  SM_EXPECT_TRUE(video_desc.extmap_allow_mixed_level() == Level::kMedia);
  video_desc.set_extmap_allow_mixed_level(Level::kNone);
  SM_EXPECT_TRUE(video_desc.extmap_allow_mixed_level() == Level::kNone);
}

SM_TEST(PcMediaContentDescription, MixedOneTwoByteHeaderSupported) {
  VideoContentDescription video_desc;
  video_desc.set_extmap_allow_mixed_level(Level::kNone);
  SM_EXPECT_FALSE(video_desc.extmap_allow_mixed());
  video_desc.set_extmap_allow_mixed_level(Level::kMedia);
  SM_EXPECT_TRUE(video_desc.extmap_allow_mixed());
  video_desc.set_extmap_allow_mixed_level(Level::kSession);
  SM_EXPECT_TRUE(video_desc.extmap_allow_mixed());
}

SM_TEST(PcSessionDescription, SetExtmapAllowMixed) {
  SessionDescription session_desc;
  session_desc.set_extmap_allow_mixed(true);
  SM_EXPECT_TRUE(session_desc.extmap_allow_mixed());
  session_desc.set_extmap_allow_mixed(false);
  SM_EXPECT_FALSE(session_desc.extmap_allow_mixed());
}

SM_TEST(PcSessionDescription, SetExtmapAllowMixedPropagatesToMediaLevel) {
  SessionDescription session_desc;
  session_desc.AddContent("video", MediaProtocolType::kRtp, std::make_unique<VideoContentDescription>());
  MediaContentDescription* video_desc = session_desc.GetContentDescriptionByName("video");
  // Setting true on session level propagates to media level.
  session_desc.set_extmap_allow_mixed(true);
  SM_EXPECT_TRUE(video_desc->extmap_allow_mixed_level() == Level::kSession);
  // Don't downgrade from session level to media level
  video_desc->set_extmap_allow_mixed_level(Level::kMedia);
  SM_EXPECT_TRUE(video_desc->extmap_allow_mixed_level() == Level::kSession);
  // Setting false on session level propagates to media level if the current state is kSession.
  session_desc.set_extmap_allow_mixed(false);
  SM_EXPECT_TRUE(video_desc->extmap_allow_mixed_level() == Level::kNone);
  // Now possible to set at media level.
  video_desc->set_extmap_allow_mixed_level(Level::kMedia);
  SM_EXPECT_TRUE(video_desc->extmap_allow_mixed_level() == Level::kMedia);
  // Setting false on session level does not override on media level if current state is kMedia.
  session_desc.set_extmap_allow_mixed(false);
  SM_EXPECT_TRUE(video_desc->extmap_allow_mixed_level() == Level::kMedia);
  // Setting true on session level overrides setting on media level.
  session_desc.set_extmap_allow_mixed(true);
  SM_EXPECT_TRUE(video_desc->extmap_allow_mixed_level() == Level::kSession);
}

SM_TEST(PcSessionDescription, AddContentTransfersExtmapAllowMixedSetting) {
  SessionDescription session_desc;
  session_desc.set_extmap_allow_mixed(false);
  std::unique_ptr<MediaContentDescription> audio_desc = std::make_unique<AudioContentDescription>();
  audio_desc->set_extmap_allow_mixed_level(Level::kMedia);
  // If session setting is false, media level setting is preserved when new content is added.
  session_desc.AddContent("audio", MediaProtocolType::kRtp, std::move(audio_desc));
  SM_EXPECT_TRUE(session_desc.GetContentDescriptionByName("audio")->extmap_allow_mixed_level() == Level::kMedia);
  // If session setting is true, it's transferred to media level when new content is added.
  session_desc.set_extmap_allow_mixed(true);
  session_desc.AddContent("video", MediaProtocolType::kRtp, std::make_unique<VideoContentDescription>());
  SM_EXPECT_TRUE(session_desc.GetContentDescriptionByName("video")->extmap_allow_mixed_level() == Level::kSession);
}

SM_TEST(PcSessionDescription, GroupsAndCloneAreDeep) {
  SessionDescription desc;
  desc.AddContent("a", MediaProtocolType::kRtp, std::make_unique<AudioContentDescription>());
  ContentGroup bundle(GROUP_TYPE_BUNDLE);
  bundle.AddContentName("a");
  bundle.AddContentName("a");  // Ignored: already in the group.
  desc.AddGroup(bundle);
  SM_EXPECT_EQ(desc.GetGroupByName(GROUP_TYPE_BUNDLE)->content_names().size(), 1u);
  SM_EXPECT_EQ(desc.GetGroupByName(GROUP_TYPE_BUNDLE)->ToString(), std::string("BUNDLE(a)"));
  std::unique_ptr<SessionDescription> copy = desc.Clone();
  copy->GetContentDescriptionByName("a")->set_rtcp_mux(true);
  SM_EXPECT_FALSE(desc.GetContentDescriptionByName("a")->rtcp_mux());
  SM_EXPECT_TRUE(copy->HasGroup(GROUP_TYPE_BUNDLE));
  desc.RemoveGroupByName(GROUP_TYPE_BUNDLE);
  SM_EXPECT_FALSE(desc.HasGroup(GROUP_TYPE_BUNDLE));
}

// jsep_session_description_unittest.cc.
constexpr char kAudioMid[] = "audio";
constexpr char kVideoMid[] = "video";
constexpr char kCandidateUfrag[] = "ufrag";
constexpr char kCandidatePwd[] = "pwd";
constexpr char kCandidateUfragVoice[] = "ufrag_voice";
constexpr char kCandidatePwdVoice[] = "pwd_voice";
constexpr char kCandidateUfragVideo[] = "ufrag_video";
constexpr char kCandidatePwdVideo[] = "pwd_video";
constexpr char kCandidateFoundation[] = "a0+B/1";
constexpr uint32_t kCandidatePriority = 2130706432U;  // pref = 1.0
constexpr uint32_t kCandidateGeneration = 2;

// This creates a session description with both audio and video media contents. In SDP this is described by two m
// lines, one audio and one video.
std::unique_ptr<SessionDescription> CreateCricketSessionDescription() {
  auto desc = std::make_unique<SessionDescription>();
  auto audio = std::make_unique<AudioContentDescription>();
  auto video = std::make_unique<VideoContentDescription>();
  audio->AddCodec(CreateAudioCodec(103, "ISAC", 16000, 0));
  desc->AddContent(kAudioMid, MediaProtocolType::kRtp, std::move(audio));
  video->AddCodec(CreateVideoCodec(120, "VP8"));
  desc->AddContent(kVideoMid, MediaProtocolType::kRtp, std::move(video));
  desc->AddTransportInfo(TransportInfo(
      kAudioMid, TransportDescription({}, kCandidateUfragVoice, kCandidatePwdVoice, p2p::ICEMODE_FULL,
                                      p2p::ConnectionRole::kNone, nullptr)));
  desc->AddTransportInfo(TransportInfo(
      kVideoMid, TransportDescription({}, kCandidateUfragVideo, kCandidatePwdVideo, p2p::ICEMODE_FULL,
                                      p2p::ConnectionRole::kNone, nullptr)));
  return desc;
}

struct JsepFixture {
  JsepFixture()
      : candidate_(p2p::ICE_CANDIDATE_COMPONENT_RTP, "udp", Addr("127.0.0.1", 1234), 1, "", "",
                   IceCandidateType::kHost, 0, "1"),
        jsep_desc_(CreateSessionDescription(SdpType::kOffer, "4611731400430051336", "12345",
                                            CreateCricketSessionDescription())) {}

  static std::string Serialize(const SessionDescriptionInterface* desc) {
    std::string sdp = desc->ToString();
    SM_EXPECT_FALSE(sdp.empty());
    return sdp;
  }
  static std::unique_ptr<SessionDescriptionInterface> DeSerialize(const std::string& sdp) {
    std::unique_ptr<SessionDescriptionInterface> jsep_desc = SdpDeserialize(SdpType::kOffer, sdp);
    SM_EXPECT_TRUE(jsep_desc != nullptr);
    return jsep_desc;
  }
  Candidate candidate_;
  std::unique_ptr<SessionDescriptionInterface> jsep_desc_;
};

Candidate MakeCandidate(const char* proto, const char* ip, uint16_t port, IceCandidateType type) {
  return Candidate(p2p::ICE_CANDIDATE_COMPONENT_RTP, proto, Addr(ip, port), kCandidatePriority, "", "", type,
                   kCandidateGeneration, kCandidateFoundation);
}

SM_TEST(PcJsepSessionDescription, CloneDefault) {
  JsepFixture f;
  auto new_desc = f.jsep_desc_->Clone();
  SM_EXPECT_TRUE(f.jsep_desc_->GetType() == new_desc->GetType());
  std::string old_desc_string;
  std::string new_desc_string;
  SM_EXPECT_TRUE(f.jsep_desc_->ToString(&old_desc_string));
  SM_EXPECT_TRUE(new_desc->ToString(&new_desc_string));
  SM_EXPECT_EQ(old_desc_string, new_desc_string);
  SM_EXPECT_EQ(f.jsep_desc_->session_id(), new_desc->session_id());
  SM_EXPECT_EQ(f.jsep_desc_->session_version(), new_desc->session_version());
}

SM_TEST(PcJsepSessionDescription, CloneRollback) {
  auto jsep_desc = CreateRollbackSessionDescription("123", "456");
  SM_EXPECT_TRUE(jsep_desc->GetType() == SdpType::kRollback);
  auto new_desc = jsep_desc->Clone();
  SM_EXPECT_TRUE(jsep_desc->GetType() == new_desc->GetType());
  SM_EXPECT_EQ(jsep_desc->session_id(), new_desc->session_id());
  SM_EXPECT_EQ(jsep_desc->session_version(), new_desc->session_version());
  SM_EXPECT_EQ(jsep_desc->ToString(), std::string());
}

SM_TEST(PcJsepSessionDescription, CloneWithCandidates) {
  JsepFixture f;
  Candidate candidate_v4 = MakeCandidate("udp", "192.168.1.5", 1234, IceCandidateType::kSrflx);
  Candidate candidate_v6 = MakeCandidate("udp", "::1", 1234, IceCandidateType::kHost);
  IceCandidate jice_v4("audio", 0, candidate_v4);
  IceCandidate jice_v6("audio", 0, candidate_v6);
  IceCandidate jice_v4_video("video", 0, candidate_v4);
  IceCandidate jice_v6_video("video", 0, candidate_v6);
  SM_ASSERT_TRUE(f.jsep_desc_->AddCandidate(&jice_v4));
  SM_ASSERT_TRUE(f.jsep_desc_->AddCandidate(&jice_v6));
  SM_ASSERT_TRUE(f.jsep_desc_->AddCandidate(&jice_v4_video));
  SM_ASSERT_TRUE(f.jsep_desc_->AddCandidate(&jice_v6_video));
  auto new_desc = f.jsep_desc_->Clone();
  SM_ASSERT_EQ(f.jsep_desc_->number_of_mediasections(), new_desc->number_of_mediasections());
  for (size_t i = 0; i < f.jsep_desc_->number_of_mediasections(); ++i) {
    const IceCandidateCollection* old_collection = f.jsep_desc_->candidates(i);
    const IceCandidateCollection* new_collection = new_desc->candidates(i);
    SM_ASSERT_EQ(old_collection->count(), new_collection->count());
    for (size_t j = 0; j < old_collection->count(); ++j) {
      SM_EXPECT_EQ(old_collection->at(j)->ToString(), new_collection->at(j)->ToString());
    }
  }
  SM_EXPECT_EQ(f.jsep_desc_->ToString(), new_desc->ToString());
}

SM_TEST(PcJsepSessionDescription, CheckSessionDescription) {
  JsepFixture f;
  SM_EXPECT_EQ(2u, f.jsep_desc_->number_of_mediasections());
}

SM_TEST(PcJsepSessionDescription, IsValidMLineIndex) {
  JsepFixture f;
  IceCandidate ice_candidate("", -1, f.candidate_);
  SM_EXPECT_FALSE(f.jsep_desc_->AddCandidate(&ice_candidate));
  IceCandidate ice_candidate2("", static_cast<int>(f.jsep_desc_->number_of_mediasections()), f.candidate_);
  SM_EXPECT_FALSE(f.jsep_desc_->AddCandidate(&ice_candidate2));
}

SM_TEST(PcJsepSessionDescription, AddCandidateWithoutMid) {
  JsepFixture f;
  IceCandidate jsep_candidate("", 0, f.candidate_);
  SM_EXPECT_TRUE(f.jsep_desc_->AddCandidate(&jsep_candidate));
  const IceCandidateCollection* ice_candidates = f.jsep_desc_->candidates(0);
  SM_ASSERT_TRUE(ice_candidates != nullptr);
  SM_EXPECT_EQ(1u, ice_candidates->count());
  const IceCandidate* ice_candidate = ice_candidates->at(0);
  f.candidate_.set_username(kCandidateUfragVoice);
  f.candidate_.set_password(kCandidatePwdVoice);
  SM_EXPECT_TRUE(ice_candidate->candidate().IsEquivalent(f.candidate_));
  SM_EXPECT_EQ(0, ice_candidate->sdp_mline_index());
  SM_EXPECT_EQ(std::string("audio"), ice_candidate->sdp_mid());
  SM_EXPECT_EQ(0u, f.jsep_desc_->candidates(1)->count());
}

SM_TEST(PcJsepSessionDescription, AddAndRemoveCandidatesWithMid) {
  JsepFixture f;
  IceCandidate jsep_candidate("video", 0, f.candidate_);
  SM_EXPECT_TRUE(f.jsep_desc_->AddCandidate(&jsep_candidate));
  SM_EXPECT_EQ(0u, f.jsep_desc_->candidates(0)->count());
  const IceCandidateCollection* ice_candidates = f.jsep_desc_->candidates(1);
  SM_ASSERT_TRUE(ice_candidates != nullptr);
  SM_EXPECT_EQ(1u, ice_candidates->count());
  const IceCandidate* ice_candidate = ice_candidates->at(0);
  f.candidate_.set_username(kCandidateUfragVideo);
  f.candidate_.set_password(kCandidatePwdVideo);
  SM_EXPECT_TRUE(ice_candidate->candidate().IsEquivalent(f.candidate_));
  // The mline index should have been updated according to mid.
  SM_EXPECT_EQ(1, ice_candidate->sdp_mline_index());
  SM_EXPECT_TRUE(f.jsep_desc_->RemoveCandidate(ice_candidate));
  SM_EXPECT_EQ(0u, f.jsep_desc_->candidates(0)->count());
  SM_EXPECT_EQ(0u, f.jsep_desc_->candidates(1)->count());
}

SM_TEST(PcJsepSessionDescription, AddCandidateAlreadyHasUfrag) {
  JsepFixture f;
  f.candidate_.set_username(kCandidateUfrag);
  f.candidate_.set_password(kCandidatePwd);
  IceCandidate jsep_candidate("audio", 0, f.candidate_);
  SM_EXPECT_TRUE(f.jsep_desc_->AddCandidate(&jsep_candidate));
  const IceCandidateCollection* ice_candidates = f.jsep_desc_->candidates(0);
  SM_ASSERT_TRUE(ice_candidates != nullptr);
  SM_EXPECT_EQ(1u, ice_candidates->count());
  SM_EXPECT_TRUE(ice_candidates->at(0)->candidate().IsEquivalent(f.candidate_));
  SM_EXPECT_EQ(0u, f.jsep_desc_->candidates(1)->count());
}

SM_TEST(PcJsepSessionDescription, AddBadCandidate) {
  JsepFixture f;
  IceCandidate bad_candidate1("", 55, f.candidate_);
  SM_EXPECT_FALSE(f.jsep_desc_->AddCandidate(&bad_candidate1));
  IceCandidate bad_candidate2("some weird mid", 0, f.candidate_);
  SM_EXPECT_FALSE(f.jsep_desc_->AddCandidate(&bad_candidate2));
}

SM_TEST(PcJsepSessionDescription, AddCandidateDuplicates) {
  JsepFixture f;
  IceCandidate jsep_candidate("", 0, f.candidate_);
  SM_EXPECT_TRUE(f.jsep_desc_->AddCandidate(&jsep_candidate));
  SM_EXPECT_EQ(1u, f.jsep_desc_->candidates(0)->count());
  // Add the same candidate again. It should be ignored.
  SM_EXPECT_TRUE(f.jsep_desc_->AddCandidate(&jsep_candidate));
  SM_EXPECT_EQ(1u, f.jsep_desc_->candidates(0)->count());
  // Create a new candidate, identical except that the ufrag and pwd are now populated.
  f.candidate_.set_username(kCandidateUfragVoice);
  f.candidate_.set_password(kCandidatePwdVoice);
  IceCandidate jsep_candidate_with_credentials("", 0, f.candidate_);
  // This should also be identified as redundant and ignored.
  SM_EXPECT_TRUE(f.jsep_desc_->AddCandidate(&jsep_candidate_with_credentials));
  SM_EXPECT_EQ(1u, f.jsep_desc_->candidates(0)->count());
}

SM_TEST(PcJsepSessionDescription, SerializeDeserialize) {
  JsepFixture f;
  std::string sdp = f.Serialize(f.jsep_desc_.get());
  auto parsed_jsep_desc = f.DeSerialize(sdp);
  SM_ASSERT_TRUE(parsed_jsep_desc != nullptr);
  SM_EXPECT_EQ(2u, parsed_jsep_desc->number_of_mediasections());
  SM_EXPECT_EQ(sdp, f.Serialize(parsed_jsep_desc.get()));
}

SM_TEST(PcJsepSessionDescription, SerializeDeserializeWithCandidates) {
  JsepFixture f;
  std::string sdp = f.Serialize(f.jsep_desc_.get());
  IceCandidate jsep_candidate("audio", 0, f.candidate_);
  SM_EXPECT_TRUE(f.jsep_desc_->AddCandidate(&jsep_candidate));
  std::string sdp_with_candidate = f.Serialize(f.jsep_desc_.get());
  SM_EXPECT_NE(sdp, sdp_with_candidate);
  auto parsed_jsep_desc = f.DeSerialize(sdp_with_candidate);
  SM_ASSERT_TRUE(parsed_jsep_desc != nullptr);
  SM_EXPECT_EQ(sdp_with_candidate, f.Serialize(parsed_jsep_desc.get()));
}

// AddHostnameCandidate: a hostname (mDNS) candidate as the default destination gives the dummy address 0.0.0.0:9.
SM_TEST(PcJsepSessionDescription, AddHostnameCandidate) {
  JsepFixture f;
  Candidate c;
  c.set_component(p2p::ICE_CANDIDATE_COMPONENT_RTP);
  c.set_protocol(p2p::UDP_PROTOCOL_NAME);
  c.set_address(SocketAddress("example.local", 1234));
  c.set_type(IceCandidateType::kHost);
  const size_t audio_index = 0;
  IceCandidate hostname_candidate("audio", audio_index, c);
  SM_EXPECT_TRUE(f.jsep_desc_->AddCandidate(&hostname_candidate));
  SM_ASSERT_TRUE(f.jsep_desc_->description() != nullptr);
  SM_ASSERT_EQ(2u, f.jsep_desc_->description()->contents().size());
  const auto& content = f.jsep_desc_->description()->contents()[audio_index];
  SM_EXPECT_EQ(std::string("0.0.0.0:9"), content.media_description()->connection_address().ToString());
  // The candidate keeps its name.
  SM_EXPECT_EQ(std::string("example.local:1234"), f.jsep_desc_->candidates(0)->at(0)->candidate().address().ToString());
}

// SerializeDeserializeWithHostnameCandidate: the hostname candidate survives the round trip, and the c= line is the
// dummy address 0.0.0.0:9.
SM_TEST(PcJsepSessionDescription, SerializeDeserializeWithHostnameCandidate) {
  JsepFixture f;
  Candidate c;
  c.set_component(p2p::ICE_CANDIDATE_COMPONENT_RTP);
  c.set_protocol(p2p::UDP_PROTOCOL_NAME);
  c.set_address(SocketAddress("example.local", 1234));
  c.set_type(IceCandidateType::kHost);
  const size_t audio_index = 0;
  const size_t video_index = 1;
  IceCandidate hostname_candidate_audio("audio", audio_index, c);
  IceCandidate hostname_candidate_video("video", video_index, c);
  SM_EXPECT_TRUE(f.jsep_desc_->AddCandidate(&hostname_candidate_audio));
  SM_EXPECT_TRUE(f.jsep_desc_->AddCandidate(&hostname_candidate_video));
  const std::string sdp = f.Serialize(f.jsep_desc_.get());
  auto parsed_jsep_desc = f.DeSerialize(sdp);
  SM_ASSERT_TRUE(parsed_jsep_desc != nullptr);
  SM_EXPECT_EQ(2u, parsed_jsep_desc->number_of_mediasections());
  SM_ASSERT_TRUE(parsed_jsep_desc->description() != nullptr);
  SM_ASSERT_EQ(2u, parsed_jsep_desc->description()->contents().size());
  const auto& audio_content = parsed_jsep_desc->description()->contents()[audio_index];
  const auto& video_content = parsed_jsep_desc->description()->contents()[video_index];
  SM_EXPECT_EQ(std::string("0.0.0.0:9"), audio_content.media_description()->connection_address().ToString());
  SM_EXPECT_EQ(std::string("0.0.0.0:9"), video_content.media_description()->connection_address().ToString());
  // Beyond libwebrtc's case: both candidates are back with their name and no IP.
  for (size_t m = 0; m < 2; ++m) {
    SM_ASSERT_EQ(1u, parsed_jsep_desc->candidates(m)->count());
    const SocketAddress& a = parsed_jsep_desc->candidates(m)->at(0)->candidate().address();
    SM_EXPECT_TRUE(a.IsUnresolvedIP());
    SM_EXPECT_EQ(std::string("example.local:1234"), a.ToString());
  }
  SM_EXPECT_EQ(sdp, f.Serialize(parsed_jsep_desc.get()));
}

// The connection address in the c= line, from a set of candidates.
std::string MessageWith(JsepFixture& f, const Candidate& c1, const Candidate& c2) {
  IceCandidate jice1("audio", 0, c1);
  IceCandidate jice2("audio", 0, c2);
  IceCandidate jice3("video", 0, c1);
  IceCandidate jice4("video", 0, c2);
  SM_EXPECT_TRUE(f.jsep_desc_->AddCandidate(&jice1));
  SM_EXPECT_TRUE(f.jsep_desc_->AddCandidate(&jice2));
  SM_EXPECT_TRUE(f.jsep_desc_->AddCandidate(&jice3));
  SM_EXPECT_TRUE(f.jsep_desc_->AddCandidate(&jice4));
  return f.Serialize(f.jsep_desc_.get());
}

// Tests that the connection address is set to the IPv6 address if only IPv6 candidates are gathered.
SM_TEST(PcJsepSessionDescription, SerializeSessionDescriptionWithIPv6Only) {
  JsepFixture f;
  std::string message = MessageWith(f, MakeCandidate("udp", "::1", 1234, IceCandidateType::kSrflx),
                                    MakeCandidate("udp", "::2", 1235, IceCandidateType::kHost));
  SM_EXPECT_NE(message.find("c=IN IP6 ::1"), std::string::npos);
  SM_EXPECT_EQ(message.find("c=IN IP4"), std::string::npos);
}

// Tests that the connection address is set to the IPv4 address if both IPv4 and IPv6 candidates are gathered.
SM_TEST(PcJsepSessionDescription, SerializeSessionDescriptionWithBothIPFamilies) {
  JsepFixture f;
  std::string message = MessageWith(f, MakeCandidate("udp", "192.168.1.5", 1234, IceCandidateType::kSrflx),
                                    MakeCandidate("udp", "::1", 1234, IceCandidateType::kHost));
  SM_EXPECT_NE(message.find("c=IN IP4 192.168.1.5"), std::string::npos);
  SM_EXPECT_EQ(message.find("c=IN IP6"), std::string::npos);
}

// Tests that the connection address is set to the UDP address if both UDP and TCP candidates are gathered.
SM_TEST(PcJsepSessionDescription, SerializeSessionDescriptionWithBothProtocols) {
  JsepFixture f;
  std::string message = MessageWith(f, MakeCandidate("tcp", "::1", 1234, IceCandidateType::kSrflx),
                                    MakeCandidate("udp", "fe80::1234:5678:abcd:ef12", 1235, IceCandidateType::kHost));
  SM_EXPECT_NE(message.find("c=IN IP6 fe80::1234:5678:abcd:ef12"), std::string::npos);
  SM_EXPECT_EQ(message.find("c=IN IP4"), std::string::npos);
}

// Tests that the connection address is set to the default (0.0.0.0) if only TCP candidates are gathered.
SM_TEST(PcJsepSessionDescription, SerializeSessionDescriptionWithTCPOnly) {
  JsepFixture f;
  std::string message = MessageWith(f, MakeCandidate("tcp", "::1", 1234, IceCandidateType::kSrflx),
                                    MakeCandidate("tcp", "::2", 1235, IceCandidateType::kHost));
  SM_EXPECT_EQ(message.find("c=IN IP6 ::3"), std::string::npos);
  SM_EXPECT_NE(message.find("c=IN IP4 0.0.0.0"), std::string::npos);
}

// Tests that the connection address is updated when a candidate is removed.
SM_TEST(PcJsepSessionDescription, RemoveCandidateAndSetConnectionAddress) {
  JsepFixture f;
  IceCandidate jice1("audio", 0, MakeCandidate("udp", "::1", 1234, IceCandidateType::kHost));
  IceCandidate jice2("audio", 0, MakeCandidate("tcp", "::2", 1235, IceCandidateType::kHost));
  IceCandidate jice3("audio", 0, MakeCandidate("udp", "192.168.1.1", 1236, IceCandidateType::kHost));
  auto* media_desc = f.jsep_desc_->description()->contents()[0].media_description();
  SM_ASSERT_TRUE(f.jsep_desc_->AddCandidate(&jice1));
  SM_ASSERT_TRUE(f.jsep_desc_->AddCandidate(&jice2));
  SM_ASSERT_TRUE(f.jsep_desc_->AddCandidate(&jice3));
  SM_EXPECT_EQ(std::string("192.168.1.1:1236"), media_desc->connection_address().ToString());
  SM_ASSERT_TRUE(f.jsep_desc_->RemoveCandidate(&jice3));
  SM_EXPECT_EQ(std::string("[::1]:1234"), media_desc->connection_address().ToString());
  SM_ASSERT_TRUE(f.jsep_desc_->RemoveCandidate(&jice2));
  SM_EXPECT_EQ(std::string("[::1]:1234"), media_desc->connection_address().ToString());
  SM_ASSERT_TRUE(f.jsep_desc_->RemoveCandidate(&jice1));
  SM_EXPECT_EQ(std::string("0.0.0.0:9"), media_desc->connection_address().ToString());
}

SM_TEST(PcJsepSessionDescription, SdpTypeIdentity) {
  for (SdpType type : {SdpType::kOffer, SdpType::kPrAnswer, SdpType::kAnswer, SdpType::kRollback}) {
    SM_EXPECT_TRUE(SdpTypeFromString(SdpTypeToString(type)) == type);
  }
  SM_EXPECT_FALSE(SdpTypeFromString("bogus").has_value());
}

SM_TEST(PcJsepSessionDescription, CreateIceCandidateFromString) {
  SdpParseError error;
  auto c = IceCandidate::Create("audio", 0, "candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host generation 2",
                                &error);
  SM_ASSERT_TRUE(c != nullptr);
  SM_EXPECT_EQ(c->candidate().address().ToString(), std::string("192.168.1.5:1234"));
  SM_EXPECT_TRUE(IceCandidate::Create("audio", 0, "candidate:bad", &error) == nullptr);
  SM_EXPECT_EQ(error.line, std::string("candidate:bad"));
  SM_EXPECT_FALSE(error.description.empty());
}

}  // namespace
