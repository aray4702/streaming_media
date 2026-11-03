// Offer/answer generation (#1063, C6 part 2): the cases of libwebrtc M157's pc/media_session_unittest.cc
// (compare/webrtc/reference/pc/) for C6's subset (Unified Plan, BUNDLE, rtcp-mux, audio and video codecs with RTX,
// header extensions, the SCTP m=application section, directions, transport info, ICE restart and DTLS roles), on
// sm_test.h, with the unittest's codec lists (kAudioCodecs1/2, kVideoCodecs1/2). Cases for what the port leaves out
// (Plan B, simulcast layers beyond the SSRC/RID basics, codec preferences, RED fmtp, FlexFEC, H.265, cryptex, SNAP,
// sframe, RFC 8888) are not here. Added: the default offer (Opus, H.264 CB with RTX, libwebrtc's header extension
// ids) and an offer/answer round trip through part 1's SdpSerialize / SdpDeserialize.
#include <memory>
#include <string>
#include <vector>

#include "sm/p2p/p2p_constants.h"
#include "sm/pc/codec_vendor.h"
#include "sm/pc/jsep.h"
#include "sm/pc/media_session.h"
#include "sm/pc/webrtc_sdp.h"
#include "sm/tls/ssl_fingerprint.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::pc;

namespace {

constexpr char kAudioMid[] = "0";
constexpr char kVideoMid[] = "1";
constexpr char kDataMid[] = "2";
constexpr char kMediaStream1[] = "stream_1";
constexpr char kVideoTrack1[] = "video_1";
constexpr char kAudioTrack1[] = "audio_1";
constexpr bool kStopped = true;
constexpr bool kActive = false;

std::vector<Codec> AudioCodecs1() {
  return {CreateAudioCodec(111, "opus", 48000, 2), CreateAudioCodec(63, "red", 48000, 2),
          CreateAudioCodec(103, "G722", 16000, 1), CreateAudioCodec(0, "PCMU", 8000, 1),
          CreateAudioCodec(8, "PCMA", 8000, 1),    CreateAudioCodec(107, "CN", 48000, 1)};
}
std::vector<Codec> AudioCodecs2() {
  return {CreateAudioCodec(126, "foo", 16000, 1), CreateAudioCodec(0, "PCMU", 8000, 1),
          CreateAudioCodec(127, "G722", 16000, 1)};
}
std::vector<Codec> AudioCodecsAnswer() {
  return {CreateAudioCodec(103, "G722", 16000, 1), CreateAudioCodec(0, "PCMU", 8000, 1)};
}
std::vector<Codec> VideoCodecs1() { return {CreateVideoCodec(96, "H264-SVC"), CreateVideoCodec(97, "H264")}; }
std::vector<Codec> VideoCodecs2() { return {CreateVideoCodec(126, "H264"), CreateVideoCodec(127, "H263")}; }
std::vector<Codec> VideoCodecsAnswer() { return {CreateVideoCodec(97, "H264")}; }

// CodecListsMatch: the same codecs, in order, with the same payload types.
bool CodecListsMatch(const std::vector<Codec>& expected, const std::vector<Codec>& actual) {
  if (expected.size() != actual.size()) return false;
  for (size_t i = 0; i < expected.size(); ++i) {
    if (expected[i].id != actual[i].id || !MatchesWithCodecRules(expected[i], actual[i])) return false;
  }
  return true;
}

// One side: certificate, codecs, payload type suggester, SSRC generator and the factory.
struct Side {
  explicit Side(const char* name) : cert(tls::SslIdentity::Create(name)) {
    tdf.set_certificate(cert);
    SetCodecs(AudioCodecs1(), VideoCodecs1());
  }
  void SetCodecs(std::vector<Codec> audio, std::vector<Codec> video) {
    vendor = std::make_unique<CodecVendor>(audio, audio, video, video);
    f = std::make_unique<MediaSessionDescriptionFactory>(&ssrc_generator, &tdf, vendor.get(), &suggester);
  }
  void SetAudioCodecs(std::vector<Codec> audio) { SetCodecs(std::move(audio), vendor->video_send_codecs()); }
  void SetVideoCodecs(std::vector<Codec> video) { SetCodecs(vendor->audio_send_codecs(), std::move(video)); }
  std::unique_ptr<SessionDescription> Offer(const MediaSessionOptions& opts,
                                            const SessionDescription* current = nullptr) {
    return f->CreateOfferOrError(opts, current);
  }
  std::unique_ptr<SessionDescription> Answer(const SessionDescription* offer, const MediaSessionOptions& opts,
                                             const SessionDescription* current = nullptr) {
    return f->CreateAnswerOrError(offer, opts, current);
  }

  std::shared_ptr<const tls::SslIdentity> cert;
  TransportDescriptionFactory tdf;
  SdpPayloadTypeSuggester suggester;
  UniqueRandomIdGenerator ssrc_generator;
  std::unique_ptr<CodecVendor> vendor;
  std::unique_ptr<MediaSessionDescriptionFactory> f;
};

struct Fixture {
  Side s1{"id1"};
  Side s2{"id2"};
  Fixture() { s2.SetCodecs(AudioCodecs2(), VideoCodecs2()); }
};

void AddMediaDescriptionOptions(MediaType type, const std::string& mid, RtpTransceiverDirection direction,
                                bool stopped, MediaSessionOptions* opts) {
  opts->media_description_options.push_back(MediaDescriptionOptions(type, mid, direction, stopped));
}
void AddAudioVideoSections(RtpTransceiverDirection direction, MediaSessionOptions* opts) {
  AddMediaDescriptionOptions(MediaType::AUDIO, kAudioMid, direction, kActive, opts);
  AddMediaDescriptionOptions(MediaType::VIDEO, kVideoMid, direction, kActive, opts);
}
void AddDataSection(RtpTransceiverDirection direction, MediaSessionOptions* opts) {
  AddMediaDescriptionOptions(MediaType::DATA, kDataMid, direction, kActive, opts);
}
MediaDescriptionOptions* FindByMid(const std::string& mid, MediaSessionOptions* opts) {
  for (auto& o : opts->media_description_options) {
    if (o.mid == mid) return &o;
  }
  return nullptr;
}
void AttachSender(const std::string& mid, MediaType type, const std::string& track_id,
                  const std::vector<std::string>& stream_ids, int num_sim_layers, MediaSessionOptions* opts) {
  MediaDescriptionOptions* o = FindByMid(mid, opts);
  if (type == MediaType::AUDIO) {
    o->AddAudioSender(track_id, stream_ids);
  } else {
    o->AddVideoSender(track_id, stream_ids, {}, SimulcastLayerList(), num_sim_layers);
  }
}
MediaSessionOptions CreateAudioMediaSession() {
  MediaSessionOptions session_options;
  AddMediaDescriptionOptions(MediaType::AUDIO, kAudioMid, RtpTransceiverDirection::kRecvOnly, kActive,
                             &session_options);
  return session_options;
}
std::vector<RtpHeaderExtensionCapability> Caps(const std::vector<RtpExtension>& extensions) {
  std::vector<RtpHeaderExtensionCapability> capabilities;
  for (const RtpExtension& e : extensions) {
    capabilities.emplace_back(e.uri, e.id, e.encrypt, RtpTransceiverDirection::kSendRecv);
  }
  return capabilities;
}
void SetAudioVideoRtpHeaderExtensions(const std::vector<RtpExtension>& audio, const std::vector<RtpExtension>& video,
                                      MediaSessionOptions* opts) {
  for (MediaDescriptionOptions& entry : opts->media_description_options) {
    if (entry.type == MediaType::AUDIO) entry.header_extensions = Caps(audio);
    if (entry.type == MediaType::VIDEO) entry.header_extensions = Caps(video);
  }
}
bool HasCn(const ContentInfo* content) {
  for (const Codec& codec : content->media_description()->codecs()) {
    if (codec.name == "CN") return true;
  }
  return false;
}

SM_TEST(PcMediaSession, TestCreateAudioOffer) {
  Fixture t;
  auto offer = t.s1.Offer(CreateAudioMediaSession());
  SM_ASSERT_TRUE(offer != nullptr);
  const ContentInfo* ac = offer->GetContentByName(kAudioMid);
  SM_ASSERT_TRUE(ac != nullptr);
  SM_EXPECT_TRUE(offer->GetContentByName(kVideoMid) == nullptr);
  SM_EXPECT_TRUE(ac->type == MediaProtocolType::kRtp);
  const MediaContentDescription* acd = ac->media_description();
  SM_EXPECT_TRUE(acd->type() == MediaType::AUDIO);
  SM_EXPECT_TRUE(CodecListsMatch(t.s1.vendor->audio_sendrecv_codecs(), acd->codecs()));
  SM_EXPECT_EQ(acd->first_ssrc(), 0u);  // No sender is attached.
  SM_EXPECT_EQ(acd->bandwidth(), kAutoBandwidth);
  SM_EXPECT_TRUE(acd->rtcp_mux());  // rtcp-mux defaults on.
  SM_EXPECT_TRUE(acd->rtcp_reduced_size());
  SM_EXPECT_EQ(acd->protocol(), std::string(kMediaProtocolDtlsSavpf));
}

SM_TEST(PcMediaSession, CreateAudioOfferEnablesReceiveNonSenderRtt) {
  Fixture t;
  auto offer = t.s1.Offer(CreateAudioMediaSession());
  SM_ASSERT_TRUE(GetFirstAudioContentDescription(offer.get()) != nullptr);
  SM_EXPECT_TRUE(GetFirstAudioContentDescription(offer.get())->receive_non_sender_rtt());
}

SM_TEST(PcMediaSession, TestCreateVideoOffer) {
  Fixture t;
  MediaSessionOptions opts;
  AddAudioVideoSections(RtpTransceiverDirection::kRecvOnly, &opts);
  auto offer = t.s1.Offer(opts);
  SM_ASSERT_TRUE(offer != nullptr);
  const ContentInfo* ac = offer->GetContentByName(kAudioMid);
  const ContentInfo* vc = offer->GetContentByName(kVideoMid);
  SM_ASSERT_TRUE(ac != nullptr);
  SM_ASSERT_TRUE(vc != nullptr);
  SM_EXPECT_TRUE(CodecListsMatch(t.s1.vendor->audio_sendrecv_codecs(), ac->media_description()->codecs()));
  const MediaContentDescription* vcd = vc->media_description();
  SM_EXPECT_TRUE(vcd->type() == MediaType::VIDEO);
  SM_EXPECT_TRUE(CodecListsMatch(t.s1.vendor->video_sendrecv_codecs(), vcd->codecs()));
  SM_EXPECT_EQ(vcd->first_ssrc(), 0u);
  SM_EXPECT_EQ(vcd->bandwidth(), kAutoBandwidth);
  SM_EXPECT_TRUE(vcd->rtcp_mux());
  SM_EXPECT_EQ(vcd->protocol(), std::string(kMediaProtocolDtlsSavpf));
  // Unified Plan signals msid both ways in an offer.
  SM_EXPECT_EQ(offer->msid_signaling(),
               kMsidSignalingSemantic | kMsidSignalingMediaSection | kMsidSignalingSsrcAttribute);
}

// The suggester knows the BUNDLE group (from an earlier description here; SetBundleGroups on libwebrtc's fake), so
// audio and video don't share payload type 126.
SM_TEST(PcMediaSession, TestBundleOfferWithSameCodecPlType) {
  Fixture t;
  const Codec offered_video_codec = t.s2.vendor->video_sendrecv_codecs()[0];
  const Codec offered_audio_codec = t.s2.vendor->audio_sendrecv_codecs()[0];
  SM_ASSERT_EQ(offered_video_codec.id, offered_audio_codec.id);
  SessionDescription bundled;
  ContentGroup group(GROUP_TYPE_BUNDLE);
  group.AddContentName(kAudioMid);
  group.AddContentName(kVideoMid);
  bundled.AddGroup(group);
  SM_ASSERT_TRUE(t.s2.suggester.Update(&bundled, /*local=*/true, p2p::SdpType::kAnswer));
  MediaSessionOptions opts;
  AddAudioVideoSections(RtpTransceiverDirection::kRecvOnly, &opts);
  opts.bundle_enabled = true;
  auto offer = t.s2.Offer(opts);
  const VideoContentDescription* vcd = GetFirstVideoContentDescription(offer.get());
  const AudioContentDescription* acd = GetFirstAudioContentDescription(offer.get());
  SM_ASSERT_TRUE(vcd != nullptr);
  SM_ASSERT_TRUE(acd != nullptr);
  SM_EXPECT_NE(vcd->codecs()[0].id, acd->codecs()[0].id);
  SM_EXPECT_EQ(vcd->codecs()[0].name, offered_video_codec.name);
  SM_EXPECT_EQ(acd->codecs()[0].name, offered_audio_codec.name);
}

SM_TEST(PcMediaSession, TestCreateUpdatedVideoOfferWithBundle) {
  Fixture t;
  MediaSessionOptions opts;
  AddMediaDescriptionOptions(MediaType::AUDIO, kAudioMid, RtpTransceiverDirection::kRecvOnly, kActive, &opts);
  AddMediaDescriptionOptions(MediaType::VIDEO, kVideoMid, RtpTransceiverDirection::kInactive, kStopped, &opts);
  opts.bundle_enabled = true;
  auto offer = t.s1.Offer(opts);
  auto answer = t.s2.Answer(offer.get(), opts);
  SM_ASSERT_TRUE(answer != nullptr);
  MediaSessionOptions updated_opts;
  AddAudioVideoSections(RtpTransceiverDirection::kRecvOnly, &updated_opts);
  updated_opts.bundle_enabled = true;
  auto updated_offer = t.s1.Offer(updated_opts, answer.get());
  const AudioContentDescription* acd = GetFirstAudioContentDescription(updated_offer.get());
  const VideoContentDescription* vcd = GetFirstVideoContentDescription(updated_offer.get());
  SM_ASSERT_TRUE(vcd != nullptr);
  SM_ASSERT_TRUE(acd != nullptr);
  SM_EXPECT_EQ(acd->protocol(), std::string(kMediaProtocolDtlsSavpf));
  SM_EXPECT_EQ(vcd->protocol(), std::string(kMediaProtocolDtlsSavpf));
}

SM_TEST(PcMediaSession, TestCreateSctpDataOffer) {
  Fixture t;
  MediaSessionOptions opts;
  opts.bundle_enabled = true;
  AddDataSection(RtpTransceiverDirection::kSendRecv, &opts);
  auto offer = t.s1.Offer(opts);
  SM_ASSERT_TRUE(offer != nullptr);
  SM_EXPECT_TRUE(offer->GetContentByName(kDataMid) != nullptr);
  auto dcd = GetFirstSctpDataContentDescription(offer.get());
  SM_ASSERT_TRUE(dcd != nullptr);
  SM_EXPECT_EQ(dcd->protocol(), std::string(kMediaProtocolUdpDtlsSctp));
  SM_EXPECT_EQ(dcd->max_message_size(), kSctpSendBufferSize);
  SM_EXPECT_FALSE(dcd->use_sctpmap());
  SM_EXPECT_TRUE(offer->GetContentByName(kDataMid)->type == MediaProtocolType::kSctp);
}

SM_TEST(PcMediaSession, TestCreateImplicitSctpDataOffer) {
  Fixture t;
  MediaSessionOptions opts;
  opts.bundle_enabled = true;
  AddDataSection(RtpTransceiverDirection::kSendRecv, &opts);
  auto offer1 = t.s1.Offer(opts);
  SM_ASSERT_TRUE(offer1 != nullptr);
  const ContentInfo* data = offer1->GetContentByName(kDataMid);
  SM_ASSERT_TRUE(data != nullptr);
  SM_ASSERT_EQ(data->media_description()->protocol(), std::string(kMediaProtocolUdpDtlsSctp));
  auto offer2 = t.s1.Offer(opts, offer1.get());
  data = offer2->GetContentByName(kDataMid);
  SM_ASSERT_TRUE(data != nullptr);
  SM_EXPECT_EQ(data->media_description()->protocol(), std::string(kMediaProtocolUdpDtlsSctp));
}

SM_TEST(PcMediaSession, ReOfferNoBundleGroupIfAllRejected) {
  Fixture t;
  MediaSessionOptions opts;
  opts.bundle_enabled = true;
  AddMediaDescriptionOptions(MediaType::AUDIO, kAudioMid, RtpTransceiverDirection::kSendRecv, kActive, &opts);
  auto offer = t.s1.Offer(opts);
  SM_EXPECT_TRUE(offer->GetGroupByName(GROUP_TYPE_BUNDLE) != nullptr);
  opts.media_description_options[0].stopped = true;
  auto reoffer = t.s1.Offer(opts, offer.get());
  SM_EXPECT_TRUE(reoffer->GetGroupByName(GROUP_TYPE_BUNDLE) == nullptr);
}

SM_TEST(PcMediaSession, ReAnswerNoBundleGroupIfAllRejected) {
  Fixture t;
  MediaSessionOptions opts;
  opts.bundle_enabled = true;
  AddMediaDescriptionOptions(MediaType::AUDIO, kAudioMid, RtpTransceiverDirection::kSendRecv, kActive, &opts);
  auto offer = t.s1.Offer(opts);
  auto answer = t.s2.Answer(offer.get(), opts);
  opts.media_description_options[0].stopped = true;
  auto reoffer = t.s1.Offer(opts, offer.get());
  auto reanswer = t.s2.Answer(reoffer.get(), opts, answer.get());
  SM_ASSERT_TRUE(reanswer != nullptr);
  // RFC 5888: the answer keeps an a=group:BUNDLE line, empty.
  const ContentGroup* group = reanswer->GetGroupByName(GROUP_TYPE_BUNDLE);
  SM_EXPECT_TRUE(group == nullptr || group->content_names().empty());
}

SM_TEST(PcMediaSession, TestCreateSendOnlyOffer) {
  Fixture t;
  MediaSessionOptions opts;
  AddAudioVideoSections(RtpTransceiverDirection::kSendOnly, &opts);
  AttachSender(kVideoMid, MediaType::VIDEO, kVideoTrack1, {kMediaStream1}, 1, &opts);
  AttachSender(kAudioMid, MediaType::AUDIO, kAudioTrack1, {kMediaStream1}, 1, &opts);
  auto offer = t.s1.Offer(opts);
  SM_ASSERT_TRUE(offer != nullptr);
  SM_EXPECT_EQ(offer->contents().size(), 2u);
  SM_EXPECT_TRUE(offer->contents()[0].media_description()->type() == MediaType::AUDIO);
  SM_EXPECT_TRUE(offer->contents()[1].media_description()->type() == MediaType::VIDEO);
  SM_EXPECT_TRUE(offer->contents()[0].media_description()->direction() == RtpTransceiverDirection::kSendOnly);
  SM_EXPECT_TRUE(offer->contents()[1].media_description()->direction() == RtpTransceiverDirection::kSendOnly);
  // One SSRC per sender, with the session's CNAME for both media types.
  const auto& audio_streams = offer->contents()[0].media_description()->streams();
  SM_ASSERT_EQ(audio_streams.size(), 1u);
  SM_EXPECT_EQ(audio_streams[0].ssrcs.size(), 1u);
  SM_EXPECT_EQ(audio_streams[0].cname, std::string(kDefaultRtcpCname));
  SM_EXPECT_EQ(audio_streams[0].id, std::string(kAudioTrack1));
  SM_EXPECT_EQ(audio_streams[0].first_stream_id(), std::string(kMediaStream1));
}

SM_TEST(PcMediaSession, TestCreateOfferContentOrder) {
  Fixture t;
  MediaSessionOptions opts;
  AddDataSection(RtpTransceiverDirection::kSendRecv, &opts);
  auto offer1 = t.s1.Offer(opts);
  SM_ASSERT_TRUE(offer1 != nullptr);
  SM_EXPECT_EQ(offer1->contents().size(), 1u);
  SM_EXPECT_TRUE(offer1->contents()[0].media_description()->type() == MediaType::DATA);
  AddMediaDescriptionOptions(MediaType::VIDEO, kVideoMid, RtpTransceiverDirection::kRecvOnly, kActive, &opts);
  auto offer2 = t.s1.Offer(opts, offer1.get());
  SM_ASSERT_TRUE(offer2 != nullptr);
  SM_EXPECT_EQ(offer2->contents().size(), 2u);
  SM_EXPECT_TRUE(offer2->contents()[0].media_description()->type() == MediaType::DATA);
  SM_EXPECT_TRUE(offer2->contents()[1].media_description()->type() == MediaType::VIDEO);
  AddMediaDescriptionOptions(MediaType::AUDIO, kAudioMid, RtpTransceiverDirection::kRecvOnly, kActive, &opts);
  auto offer3 = t.s1.Offer(opts, offer2.get());
  SM_ASSERT_TRUE(offer3 != nullptr);
  SM_EXPECT_EQ(offer3->contents().size(), 3u);
  SM_EXPECT_TRUE(offer3->contents()[0].media_description()->type() == MediaType::DATA);
  SM_EXPECT_TRUE(offer3->contents()[1].media_description()->type() == MediaType::VIDEO);
  SM_EXPECT_TRUE(offer3->contents()[2].media_description()->type() == MediaType::AUDIO);
}

SM_TEST(PcMediaSession, TestCreateAudioAnswer) {
  Fixture t;
  auto offer = t.s1.Offer(CreateAudioMediaSession());
  SM_ASSERT_TRUE(offer != nullptr);
  auto answer = t.s2.Answer(offer.get(), CreateAudioMediaSession());
  SM_ASSERT_TRUE(answer != nullptr);
  const ContentInfo* ac = answer->GetContentByName(kAudioMid);
  SM_ASSERT_TRUE(ac != nullptr);
  SM_EXPECT_TRUE(answer->GetContentByName(kVideoMid) == nullptr);
  SM_EXPECT_FALSE(ac->rejected);
  const MediaContentDescription* acd = ac->media_description();
  SM_EXPECT_TRUE(CodecListsMatch(AudioCodecsAnswer(), acd->codecs()));
  SM_EXPECT_EQ(acd->first_ssrc(), 0u);
  SM_EXPECT_EQ(acd->bandwidth(), kAutoBandwidth);
  SM_EXPECT_TRUE(acd->rtcp_mux());
  SM_EXPECT_EQ(acd->protocol(), std::string(kMediaProtocolDtlsSavpf));
}

SM_TEST(PcMediaSession, TestCreateAudioAnswerWithNoCommonCodecs) {
  Fixture t;
  MediaSessionOptions opts;
  AddMediaDescriptionOptions(MediaType::AUDIO, kAudioMid, RtpTransceiverDirection::kSendRecv, kActive, &opts);
  t.s1.SetAudioCodecs({CreateAudioCodec(96, "opus", 48000, 1)});
  t.s2.SetAudioCodecs({CreateAudioCodec(0, "PCMU", 8000, 1)});
  auto offer = t.s1.Offer(opts);
  auto answer = t.s2.Answer(offer.get(), opts);
  const ContentInfo* ac = answer->GetContentByName(kAudioMid);
  SM_ASSERT_TRUE(ac != nullptr);
  SM_EXPECT_TRUE(ac->rejected);
}

SM_TEST(PcMediaSession, TestCreateVideoAnswer) {
  Fixture t;
  MediaSessionOptions opts;
  AddAudioVideoSections(RtpTransceiverDirection::kRecvOnly, &opts);
  auto offer = t.s1.Offer(opts);
  SM_ASSERT_TRUE(offer != nullptr);
  auto answer = t.s2.Answer(offer.get(), opts);
  const ContentInfo* ac = answer->GetContentByName(kAudioMid);
  const ContentInfo* vc = answer->GetContentByName(kVideoMid);
  SM_ASSERT_TRUE(ac != nullptr);
  SM_ASSERT_TRUE(vc != nullptr);
  SM_EXPECT_TRUE(CodecListsMatch(AudioCodecsAnswer(), ac->media_description()->codecs()));
  SM_EXPECT_TRUE(CodecListsMatch(VideoCodecsAnswer(), vc->media_description()->codecs()));
  SM_EXPECT_EQ(vc->media_description()->first_ssrc(), 0u);
  SM_EXPECT_TRUE(vc->media_description()->rtcp_mux());
  SM_EXPECT_EQ(vc->media_description()->protocol(), std::string(kMediaProtocolDtlsSavpf));
}

SM_TEST(PcMediaSession, TestCreateVideoAnswerWithNoCommonCodecs) {
  Fixture t;
  MediaSessionOptions opts;
  AddMediaDescriptionOptions(MediaType::VIDEO, kVideoMid, RtpTransceiverDirection::kSendRecv, kActive, &opts);
  t.s1.SetVideoCodecs({CreateVideoCodec(96, "H264")});
  t.s2.SetVideoCodecs({CreateVideoCodec(97, "VP8")});
  auto offer = t.s1.Offer(opts);
  auto answer = t.s2.Answer(offer.get(), opts);
  const ContentInfo* vc = answer->GetContentByName(kVideoMid);
  SM_ASSERT_TRUE(vc != nullptr);
  SM_EXPECT_TRUE(vc->rejected);
}

// The H.264 profile and packetization-mode must match (codec_comparators).
SM_TEST(PcMediaSession, H264ProfileAndPacketizationModeMustMatch) {
  Fixture t;
  MediaSessionOptions opts;
  AddMediaDescriptionOptions(MediaType::VIDEO, kVideoMid, RtpTransceiverDirection::kSendRecv, kActive, &opts);
  Codec cb = CreateVideoCodec(96, "H264");
  cb.SetParam(kH264FmtpProfileLevelId, "42e01f");
  Codec high = CreateVideoCodec(98, "H264");
  high.SetParam(kH264FmtpProfileLevelId, "640c1f");
  Codec cb_mode0 = cb;
  cb_mode0.id = 100;
  cb_mode0.SetParam(kH264FmtpPacketizationMode, "0");
  t.s1.SetVideoCodecs({cb});
  t.s2.SetVideoCodecs({high, cb_mode0});
  auto answer = t.s2.Answer(t.s1.Offer(opts).get(), opts);
  SM_EXPECT_TRUE(answer->GetContentByName(kVideoMid)->rejected);
  t.s2.SetVideoCodecs({high, cb_mode0, cb});
  answer = t.s2.Answer(t.s1.Offer(opts).get(), opts);
  const auto& codecs = answer->GetContentByName(kVideoMid)->media_description()->codecs();
  SM_ASSERT_EQ(codecs.size(), 1u);
  SM_EXPECT_EQ(codecs[0].id, 96);
}

void TestMediaDirectionInAnswer(RtpTransceiverDirection direction_in_offer,
                                RtpTransceiverDirection expected_direction_in_answer) {
  Fixture t;
  MediaSessionOptions offer_opts;
  AddAudioVideoSections(direction_in_offer, &offer_opts);
  auto offer = t.s1.Offer(offer_opts);
  SM_ASSERT_TRUE(offer != nullptr);
  MediaSessionOptions answer_opts;
  AddAudioVideoSections(RtpTransceiverDirection::kSendRecv, &answer_opts);
  auto answer = t.s2.Answer(offer.get(), answer_opts);
  SM_ASSERT_TRUE(answer != nullptr);
  SM_EXPECT_TRUE(GetFirstAudioContentDescription(answer.get())->direction() == expected_direction_in_answer);
  SM_EXPECT_TRUE(GetFirstVideoContentDescription(answer.get())->direction() == expected_direction_in_answer);
}

SM_TEST(PcMediaSession, CreateAnswerToSendReceiveOffer) {
  TestMediaDirectionInAnswer(RtpTransceiverDirection::kSendRecv, RtpTransceiverDirection::kSendRecv);
}
SM_TEST(PcMediaSession, CreateAnswerToSendOnlyOffer) {
  TestMediaDirectionInAnswer(RtpTransceiverDirection::kSendOnly, RtpTransceiverDirection::kRecvOnly);
}
SM_TEST(PcMediaSession, CreateAnswerToRecvOnlyOffer) {
  TestMediaDirectionInAnswer(RtpTransceiverDirection::kRecvOnly, RtpTransceiverDirection::kSendOnly);
}
SM_TEST(PcMediaSession, CreateAnswerToInactiveOffer) {
  TestMediaDirectionInAnswer(RtpTransceiverDirection::kInactive, RtpTransceiverDirection::kInactive);
}

SM_TEST(PcMediaSession, TestCreateDataAnswerUsesSctpmap) {
  Fixture t;
  MediaSessionOptions opts;
  AddDataSection(RtpTransceiverDirection::kSendRecv, &opts);
  auto offer = t.s1.Offer(opts);
  SM_ASSERT_TRUE(offer != nullptr);
  GetFirstSctpDataContentDescription(offer.get())->set_use_sctpmap(true);
  auto answer = t.s2.Answer(offer.get(), opts);
  SM_ASSERT_TRUE(GetFirstSctpDataContentDescription(answer.get()) != nullptr);
  SM_EXPECT_TRUE(GetFirstSctpDataContentDescription(answer.get())->use_sctpmap());
}

SM_TEST(PcMediaSession, TestCreateDataAnswerWithoutSctpmap) {
  Fixture t;
  MediaSessionOptions opts;
  AddDataSection(RtpTransceiverDirection::kSendRecv, &opts);
  auto offer = t.s1.Offer(opts);
  GetFirstSctpDataContentDescription(offer.get())->set_use_sctpmap(false);
  auto answer = t.s2.Answer(offer.get(), opts);
  SM_ASSERT_TRUE(GetFirstSctpDataContentDescription(answer.get()) != nullptr);
  SM_EXPECT_FALSE(GetFirstSctpDataContentDescription(answer.get())->use_sctpmap());
}

// TestCreateDataAnswerToOfferWithDefinedMessageSize, ...WithZeroMessageSize, ...WithTooBigMessageSize.
SM_TEST(PcMediaSession, TestCreateDataAnswerMaxMessageSize) {
  for (auto [offered, expected] : {std::pair{1234, 1234}, std::pair{0, kSctpSendBufferSize},
                                   std::pair{kSctpSendBufferSize + 1, kSctpSendBufferSize}}) {
    Fixture t;
    MediaSessionOptions opts;
    AddDataSection(RtpTransceiverDirection::kSendRecv, &opts);
    auto offer = t.s1.Offer(opts);
    GetFirstSctpDataContentDescription(offer.get())->set_max_message_size(offered);
    auto answer = t.s2.Answer(offer.get(), opts);
    SM_ASSERT_TRUE(GetFirstSctpDataContentDescription(answer.get()) != nullptr);
    SM_EXPECT_EQ(GetFirstSctpDataContentDescription(answer.get())->max_message_size(), expected);
  }
}

// TestCreateDataAnswerToDifferentOfferedProtos: the answer uses the offer's protocol.
SM_TEST(PcMediaSession, TestCreateDataAnswerToDifferentOfferedProtos) {
  for (const char* proto : {"DTLS/SCTP", "UDP/DTLS/SCTP", "TCP/DTLS/SCTP"}) {
    Fixture t;
    MediaSessionOptions opts;
    AddDataSection(RtpTransceiverDirection::kSendRecv, &opts);
    auto offer = t.s1.Offer(opts);
    offer->GetContentByName(kDataMid)->media_description()->set_protocol(proto);
    auto answer = t.s2.Answer(offer.get(), opts);
    const ContentInfo* dc = answer->GetContentByName(kDataMid);
    SM_ASSERT_TRUE(dc != nullptr);
    SM_EXPECT_FALSE(dc->rejected);
    SM_EXPECT_EQ(dc->media_description()->protocol(), std::string(proto));
  }
}

SM_TEST(PcMediaSession, TestCreateVideoAnswerRtcpMux) {
  for (auto [offer_mux, answer_mux] : {std::pair{true, true}, std::pair{true, false}, std::pair{false, true},
                                       std::pair{false, false}}) {
    Fixture t;
    MediaSessionOptions offer_opts;
    AddAudioVideoSections(RtpTransceiverDirection::kSendRecv, &offer_opts);
    MediaSessionOptions answer_opts;
    AddAudioVideoSections(RtpTransceiverDirection::kSendRecv, &answer_opts);
    offer_opts.rtcp_mux_enabled = offer_mux;
    answer_opts.rtcp_mux_enabled = answer_mux;
    auto offer = t.s1.Offer(offer_opts);
    auto answer = t.s2.Answer(offer.get(), answer_opts);
    SM_ASSERT_TRUE(answer != nullptr);
    SM_EXPECT_EQ(GetFirstAudioContentDescription(offer.get())->rtcp_mux(), offer_mux);
    SM_EXPECT_EQ(GetFirstVideoContentDescription(offer.get())->rtcp_mux(), offer_mux);
    SM_EXPECT_EQ(GetFirstAudioContentDescription(answer.get())->rtcp_mux(), offer_mux && answer_mux);
    SM_EXPECT_EQ(GetFirstVideoContentDescription(answer.get())->rtcp_mux(), offer_mux && answer_mux);
  }
}

SM_TEST(PcMediaSession, TestCreateAudioAnswerToVideo) {
  Fixture t;
  MediaSessionOptions opts;
  AddAudioVideoSections(RtpTransceiverDirection::kRecvOnly, &opts);
  auto offer = t.s1.Offer(opts);
  opts.media_description_options[1].stopped = true;
  auto answer = t.s2.Answer(offer.get(), opts);
  SM_ASSERT_TRUE(answer->GetContentByName(kVideoMid) != nullptr);
  SM_EXPECT_TRUE(answer->GetContentByName(kVideoMid)->rejected);
}

SM_TEST(PcMediaSession, TestVADEnableOption) {
  Fixture t;
  MediaSessionOptions options;
  AddAudioVideoSections(RtpTransceiverDirection::kRecvOnly, &options);
  auto offer = t.s1.Offer(options);
  SM_EXPECT_TRUE(HasCn(offer->GetContentByName(kAudioMid)));
  options.vad_enabled = false;
  offer = t.s1.Offer(options);
  SM_EXPECT_FALSE(HasCn(offer->GetContentByName(kAudioMid)));
  auto answer = t.s1.Answer(offer.get(), options);
  SM_ASSERT_TRUE(answer != nullptr);
  SM_EXPECT_FALSE(HasCn(answer->GetContentByName(kAudioMid)));
}

// TestCreateMultiStreamVideoOffer (one sender) and RTX: a video sender gets a primary and an RTX SSRC in a FID
// group when RTX is offered; the answer's RTX apt points at the offer's payload type.
SM_TEST(PcMediaSession, RtxSsrcsAndAptMapping) {
  Fixture t;
  std::vector<Codec> f1_codecs = {CreateVideoCodec(96, "H264"), CreateVideoRtxCodec(97, 96)};
  std::vector<Codec> f2_codecs = {CreateVideoCodec(100, "H264"), CreateVideoRtxCodec(101, 100)};
  t.s1.SetVideoCodecs(f1_codecs);
  t.s2.SetVideoCodecs(f2_codecs);
  MediaSessionOptions opts;
  AddMediaDescriptionOptions(MediaType::VIDEO, kVideoMid, RtpTransceiverDirection::kSendRecv, kActive, &opts);
  AttachSender(kVideoMid, MediaType::VIDEO, kVideoTrack1, {kMediaStream1}, 1, &opts);
  auto offer = t.s1.Offer(opts);
  const VideoContentDescription* vcd = GetFirstVideoContentDescription(offer.get());
  SM_ASSERT_TRUE(CodecListsMatch(f1_codecs, vcd->codecs()));
  SM_ASSERT_EQ(vcd->streams().size(), 1u);
  SM_EXPECT_EQ(vcd->streams()[0].ssrcs.size(), 2u);
  uint32_t fid = 0;
  SM_EXPECT_TRUE(vcd->streams()[0].GetFidSsrc(vcd->streams()[0].first_ssrc(), &fid));
  SM_EXPECT_NE(fid, 0u);
  auto answer = t.s2.Answer(offer.get(), opts);
  const VideoContentDescription* acd = GetFirstVideoContentDescription(answer.get());
  SM_ASSERT_EQ(acd->codecs().size(), 2u);
  SM_EXPECT_EQ(acd->codecs()[0].id, 96);
  SM_EXPECT_EQ(acd->codecs()[1].id, 97);
  std::string apt;
  SM_EXPECT_TRUE(acd->codecs()[1].GetParam(kCodecParamAssociatedPayloadType, &apt));
  SM_EXPECT_EQ(apt, std::string("96"));
  SM_EXPECT_EQ(acd->streams().size(), 1u);
}

// RtxWithoutApt: an offered RTX codec without apt is not answered.
SM_TEST(PcMediaSession, RtxWithoutApt) {
  Fixture t;
  std::vector<Codec> codecs = {CreateVideoCodec(96, "H264"), CreateVideoRtxCodec(97, 96)};
  t.s1.SetVideoCodecs(codecs);
  t.s2.SetVideoCodecs(codecs);
  MediaSessionOptions opts;
  AddMediaDescriptionOptions(MediaType::VIDEO, kVideoMid, RtpTransceiverDirection::kSendRecv, kActive, &opts);
  auto offer = t.s1.Offer(opts);
  auto* vcd = GetFirstVideoContentDescription(offer.get());
  std::vector<Codec> offered = vcd->codecs();
  offered[1].RemoveParam(kCodecParamAssociatedPayloadType);
  vcd->set_codecs(offered);
  auto answer = t.s2.Answer(offer.get(), opts);
  const auto& answered = GetFirstVideoContentDescription(answer.get())->codecs();
  SM_ASSERT_EQ(answered.size(), 1u);
  SM_EXPECT_EQ(answered[0].name, std::string("H264"));
}

// FilterOutRtxIfAptDoesntMatch: RTX whose apt points at a codec we don't share is not answered.
SM_TEST(PcMediaSession, FilterOutRtxIfAptDoesntMatch) {
  Fixture t;
  t.s1.SetVideoCodecs({CreateVideoCodec(96, "H264"), CreateVideoCodec(98, "VP8"), CreateVideoRtxCodec(97, 98)});
  t.s2.SetVideoCodecs({CreateVideoCodec(96, "H264"), CreateVideoRtxCodec(97, 96)});
  MediaSessionOptions opts;
  AddMediaDescriptionOptions(MediaType::VIDEO, kVideoMid, RtpTransceiverDirection::kSendRecv, kActive, &opts);
  auto answer = t.s2.Answer(t.s1.Offer(opts).get(), opts);
  const auto& answered = GetFirstVideoContentDescription(answer.get())->codecs();
  SM_ASSERT_EQ(answered.size(), 1u);
  SM_EXPECT_EQ(answered[0].name, std::string("H264"));
}

// The H.264 level in the answer (H264GenerateProfileLevelIdForAnswer).
SM_TEST(PcMediaSession, H264LevelInAnswer) {
  struct Case {
    const char* local;
    const char* remote;
    bool asymmetry;
    const char* expected;
  };
  for (const Case& c : {Case{"42e01f", "42e015", false, "42e015"}, Case{"42e015", "42e01f", false, "42e015"},
                        Case{"42e01f", "42e015", true, "42e01f"}, Case{"42e00b", "42e01f", true, "42e00b"}}) {
    Fixture t;
    Codec local = CreateVideoCodec(96, "H264");
    local.SetParam(kH264FmtpProfileLevelId, c.local);
    Codec remote = CreateVideoCodec(100, "H264");
    remote.SetParam(kH264FmtpProfileLevelId, c.remote);
    if (c.asymmetry) {
      local.SetParam(kH264FmtpLevelAsymmetryAllowed, "1");
      remote.SetParam(kH264FmtpLevelAsymmetryAllowed, "1");
    }
    t.s1.SetVideoCodecs({remote});
    t.s2.SetVideoCodecs({local});
    MediaSessionOptions opts;
    AddMediaDescriptionOptions(MediaType::VIDEO, kVideoMid, RtpTransceiverDirection::kSendRecv, kActive, &opts);
    auto answer = t.s2.Answer(t.s1.Offer(opts).get(), opts);
    const auto& codecs = GetFirstVideoContentDescription(answer.get())->codecs();
    SM_ASSERT_EQ(codecs.size(), 1u);
    SM_EXPECT_EQ(codecs[0].id, 100);
    std::string plid;
    SM_EXPECT_TRUE(codecs[0].GetParam(kH264FmtpProfileLevelId, &plid));
    SM_EXPECT_EQ(plid, std::string(c.expected));
  }
}

// TestOfferAnswerWithRtpExtensions-style: ids from the options, made unique across audio and video, and the
// answer takes the offer's ids for the extensions both have.
SM_TEST(PcMediaSession, RtpExtensionIdsInOfferAndAnswer) {
  Fixture t;
  MediaSessionOptions opts;
  AddAudioVideoSections(RtpTransceiverDirection::kSendRecv, &opts);
  SetAudioVideoRtpHeaderExtensions(
      {RtpExtension("urn:ietf:params:rtp-hdrext:ssrc-audio-level", 8),
       RtpExtension("http://google.com/testing/audio_something", 10)},
      {RtpExtension("urn:ietf:params:rtp-hdrext:toffset", 14),
       RtpExtension("http://google.com/testing/video_something", 13)},
      &opts);
  auto offer = t.s1.Offer(opts);
  SM_ASSERT_TRUE(offer != nullptr);
  const auto& audio = GetFirstAudioContentDescription(offer.get())->rtp_header_extensions();
  const auto& video = GetFirstVideoContentDescription(offer.get())->rtp_header_extensions();
  SM_ASSERT_EQ(audio.size(), 2u);
  SM_ASSERT_EQ(video.size(), 2u);
  SM_EXPECT_EQ(audio[0].id, 8);
  SM_EXPECT_EQ(audio[1].id, 10);
  SM_EXPECT_EQ(video[0].id, 14);
  SM_EXPECT_EQ(video[1].id, 13);
  MediaSessionOptions answer_opts;
  AddAudioVideoSections(RtpTransceiverDirection::kSendRecv, &answer_opts);
  SetAudioVideoRtpHeaderExtensions(
      {RtpExtension("urn:ietf:params:rtp-hdrext:ssrc-audio-level", 2),
       RtpExtension("http://google.com/testing/audio_something_else", 8)},
      {RtpExtension("urn:ietf:params:rtp-hdrext:toffset", 2),
       RtpExtension("http://google.com/testing/video_something_else", 14)},
      &answer_opts);
  auto answer = t.s2.Answer(offer.get(), answer_opts);
  const auto& a_audio = GetFirstAudioContentDescription(answer.get())->rtp_header_extensions();
  const auto& a_video = GetFirstVideoContentDescription(answer.get())->rtp_header_extensions();
  SM_ASSERT_EQ(a_audio.size(), 1u);
  SM_EXPECT_EQ(a_audio[0], RtpExtension("urn:ietf:params:rtp-hdrext:ssrc-audio-level", 8));
  SM_ASSERT_EQ(a_video.size(), 1u);
  SM_EXPECT_EQ(a_video[0], RtpExtension("urn:ietf:params:rtp-hdrext:toffset", 14));
}

// RtpExtensionIdReused: an extension in both audio and video gets one id, and a colliding id is moved.
SM_TEST(PcMediaSession, RtpExtensionIdReused) {
  Fixture t;
  MediaSessionOptions opts;
  AddAudioVideoSections(RtpTransceiverDirection::kSendRecv, &opts);
  SetAudioVideoRtpHeaderExtensions({RtpExtension("urn:ietf:params:rtp-hdrext:ssrc-audio-level", 2),
                                    RtpExtension("http://google.com/testing/both_audio_and_video", 7)},
                                   {RtpExtension("urn:ietf:params:rtp-hdrext:toffset", 2),
                                    RtpExtension("http://google.com/testing/both_audio_and_video", 5)},
                                   &opts);
  auto offer = t.s1.Offer(opts);
  const auto& audio = GetFirstAudioContentDescription(offer.get())->rtp_header_extensions();
  const auto& video = GetFirstVideoContentDescription(offer.get())->rtp_header_extensions();
  SM_ASSERT_EQ(video.size(), 2u);
  SM_EXPECT_EQ(audio[1].id, 7);
  SM_EXPECT_EQ(video[1].id, 7);   // Reused from audio.
  SM_EXPECT_NE(video[0].id, 2);   // Taken by audio-level.
  SM_EXPECT_EQ(video[0].id, 14);  // The highest free one-byte id.
}

// TestTransportSequenceNumberNegotiation: the answer has transport-cc 01 / 02 only when both sides have it.
SM_TEST(PcMediaSession, TransportSequenceNumberNegotiation) {
  const RtpExtension tcc01(RtpExtension::kTransportSequenceNumberUri, 1);
  const RtpExtension tcc02(RtpExtension::kTransportSequenceNumberV2Uri, 2);
  struct Case {
    std::vector<RtpExtension> local, offered, expected;
  };
  for (const Case& c : {Case{{tcc01}, {tcc01}, {tcc01}}, Case{{tcc01}, {tcc01, tcc02}, {tcc01}},
                        Case{{tcc01, tcc02}, {tcc01}, {tcc01}}, Case{{tcc02}, {tcc01, tcc02}, {tcc02}},
                        Case{{tcc01, tcc02}, {tcc01, tcc02}, {tcc01, tcc02}}, Case{{tcc02}, {tcc01}, {}}}) {
    Fixture t;
    MediaSessionOptions opts;
    AddAudioVideoSections(RtpTransceiverDirection::kRecvOnly, &opts);
    SetAudioVideoRtpHeaderExtensions(c.offered, c.offered, &opts);
    auto offer = t.s1.Offer(opts);
    SetAudioVideoRtpHeaderExtensions(c.local, c.local, &opts);
    auto answer = t.s2.Answer(offer.get(), opts);
    SM_EXPECT_TRUE(GetFirstAudioContentDescription(answer.get())->rtp_header_extensions() == c.expected);
    SM_EXPECT_TRUE(GetFirstVideoContentDescription(answer.get())->rtp_header_extensions() == c.expected);
  }
}

// AnswersUnstoppedExtensions: stopped extensions are left out of the offer and the answer.
SM_TEST(PcMediaSession, StoppedExtensionsNotOffered) {
  Fixture t;
  MediaSessionOptions opts;
  AddMediaDescriptionOptions(MediaType::AUDIO, kAudioMid, RtpTransceiverDirection::kSendRecv, kActive, &opts);
  opts.media_description_options[0].header_extensions = {
      RtpHeaderExtensionCapability("uri1", 1, RtpTransceiverDirection::kStopped),
      RtpHeaderExtensionCapability("uri2", 2, RtpTransceiverDirection::kSendOnly),
      RtpHeaderExtensionCapability("uri3", 3, RtpTransceiverDirection::kStopped)};
  auto offer = t.s1.Offer(opts);
  const auto& exts = GetFirstAudioContentDescription(offer.get())->rtp_header_extensions();
  SM_ASSERT_EQ(exts.size(), 1u);
  SM_EXPECT_EQ(exts[0], RtpExtension("uri2", 2));
}

// TestTransportInfo{Offer,Answer}{Audio,Multimedia,Bundle}{,Current}.
void TestTransportInfo(bool offer, bool bundle, bool has_current_desc, bool renomination) {
  Fixture t;
  MediaSessionOptions options;
  AddAudioVideoSections(RtpTransceiverDirection::kRecvOnly, &options);
  AddDataSection(RtpTransceiverDirection::kSendRecv, &options);
  options.bundle_enabled = bundle;
  for (auto& o : options.media_description_options) o.transport_options.enable_ice_renomination = renomination;
  std::unique_ptr<SessionDescription> current_desc;
  if (has_current_desc) {
    current_desc = std::make_unique<SessionDescription>();
    current_desc->AddTransportInfo(TransportInfo(kAudioMid, TransportDescription("current_audio_ufrag", "current_audio_pwd")));
    current_desc->AddTransportInfo(TransportInfo(kVideoMid, TransportDescription("current_video_ufrag", "current_video_pwd")));
    current_desc->AddTransportInfo(TransportInfo(kDataMid, TransportDescription("current_data_ufrag", "current_data_pwd")));
  }
  std::unique_ptr<SessionDescription> desc;
  if (offer) {
    desc = t.s1.Offer(options, current_desc.get());
  } else {
    auto offer_desc = t.s1.Offer(options);
    desc = t.s1.Answer(offer_desc.get(), options, current_desc.get());
  }
  SM_ASSERT_TRUE(desc != nullptr);
  const TransportInfo* ti_audio = desc->GetTransportInfoByName(kAudioMid);
  const TransportInfo* ti_video = desc->GetTransportInfoByName(kVideoMid);
  const TransportInfo* ti_data = desc->GetTransportInfoByName(kDataMid);
  SM_ASSERT_TRUE(ti_audio && ti_video && ti_data);
  if (has_current_desc) {
    SM_EXPECT_EQ(ti_audio->description.ice_ufrag, std::string("current_audio_ufrag"));
    SM_EXPECT_EQ(ti_audio->description.ice_pwd, std::string("current_audio_pwd"));
  } else {
    SM_EXPECT_EQ(ti_audio->description.ice_ufrag.size(), static_cast<size_t>(p2p::ICE_UFRAG_LENGTH));
    SM_EXPECT_EQ(ti_audio->description.ice_pwd.size(), static_cast<size_t>(p2p::ICE_PWD_LENGTH));
  }
  for (const TransportInfo* ti : {ti_video, ti_data}) {
    if (bundle) {
      SM_EXPECT_EQ(ti->description.ice_ufrag, ti_audio->description.ice_ufrag);
      SM_EXPECT_EQ(ti->description.ice_pwd, ti_audio->description.ice_pwd);
      SM_EXPECT_TRUE(ti->description.connection_role == ti_audio->description.connection_role);
    } else if (has_current_desc) {
      SM_EXPECT_EQ(ti->description.ice_ufrag, std::string(ti == ti_video ? "current_video_ufrag" : "current_data_ufrag"));
    } else {
      SM_EXPECT_EQ(ti->description.ice_ufrag.size(), static_cast<size_t>(p2p::ICE_UFRAG_LENGTH));
      SM_EXPECT_NE(ti->description.ice_ufrag, ti_audio->description.ice_ufrag);
    }
  }
  for (const TransportInfo* ti : {ti_audio, ti_video, ti_data}) {
    SM_EXPECT_EQ(ti->description.HasOption("renomination"), renomination);
    SM_EXPECT_TRUE(ti->description.identity_fingerprint != nullptr);
    SM_EXPECT_TRUE(ti->description.connection_role ==
                   (offer ? ConnectionRole::kActpass : ConnectionRole::kActive));
  }
}

SM_TEST(PcMediaSession, TestTransportInfoOffer) {
  TestTransportInfo(true, false, false, false);
  TestTransportInfo(true, false, true, false);
  TestTransportInfo(true, false, false, true);
}
SM_TEST(PcMediaSession, TestTransportInfoOfferBundle) {
  TestTransportInfo(true, true, false, false);
  TestTransportInfo(true, true, true, false);
}
SM_TEST(PcMediaSession, TestTransportInfoAnswer) {
  TestTransportInfo(false, false, false, false);
  TestTransportInfo(false, false, true, false);
  TestTransportInfo(false, false, false, true);
}
SM_TEST(PcMediaSession, TestTransportInfoAnswerBundle) {
  TestTransportInfo(false, true, false, false);
  TestTransportInfo(false, true, true, false);
}

// TestOfferDtlsSavpfCreateAnswer: the answer keeps the offer's protocol.
SM_TEST(PcMediaSession, TestOfferDtlsSavpfCreateAnswer) {
  Fixture t;
  MediaSessionOptions opts = CreateAudioMediaSession();
  auto offer = t.s1.Offer(opts);
  SM_ASSERT_TRUE(offer != nullptr);
  offer->GetContentByName(kAudioMid)->media_description()->set_protocol("TCP/TLS/RTP/SAVPF");
  auto answer = t.s2.Answer(offer.get(), opts);
  const ContentInfo* ac = answer->GetContentByName(kAudioMid);
  SM_ASSERT_TRUE(ac != nullptr);
  SM_EXPECT_FALSE(ac->rejected);
  SM_EXPECT_EQ(ac->media_description()->protocol(), std::string("TCP/TLS/RTP/SAVPF"));
}

// An answer to an offer without a fingerprint fails (DTLS is required).
SM_TEST(PcMediaSession, AnswerToOfferWithoutDtlsFails) {
  Fixture t;
  MediaSessionOptions opts = CreateAudioMediaSession();
  auto offer = t.s1.Offer(opts);
  offer->transport_infos()[0].description.identity_fingerprint.reset();
  std::string error;
  SM_EXPECT_TRUE(t.s2.f->CreateAnswerOrError(offer.get(), opts, nullptr, &error) == nullptr);
  SM_EXPECT_FALSE(error.empty());
}

// TestIceRestart through the factory: the re-offer keeps the credentials unless an ICE restart is asked for.
SM_TEST(PcMediaSession, IceRestartInReoffer) {
  Fixture t;
  MediaSessionOptions opts;
  AddAudioVideoSections(RtpTransceiverDirection::kSendRecv, &opts);
  opts.bundle_enabled = true;
  auto offer = t.s1.Offer(opts);
  auto reoffer = t.s1.Offer(opts, offer.get());
  SM_EXPECT_EQ(reoffer->transport_infos()[0].description.ice_ufrag, offer->transport_infos()[0].description.ice_ufrag);
  for (auto& o : opts.media_description_options) o.transport_options.ice_restart = true;
  auto restart = t.s1.Offer(opts, offer.get());
  SM_EXPECT_NE(restart->transport_infos()[0].description.ice_ufrag, offer->transport_infos()[0].description.ice_ufrag);
  SM_EXPECT_EQ(restart->transport_infos()[1].description.ice_ufrag,
               restart->transport_infos()[0].description.ice_ufrag);  // BUNDLE shares it.
}

// TestMIDsMatchesExistingOffer / CreateAnswer with options that don't match the offer fails.
SM_TEST(PcMediaSession, AnswerOptionsMustMatchOffer) {
  Fixture t;
  MediaSessionOptions opts;
  AddAudioVideoSections(RtpTransceiverDirection::kSendRecv, &opts);
  auto offer = t.s1.Offer(opts);
  MediaSessionOptions audio_only = CreateAudioMediaSession();
  SM_EXPECT_TRUE(t.s2.Answer(offer.get(), audio_only) == nullptr);
  SM_EXPECT_TRUE(t.s2.f->CreateAnswerOrError(nullptr, opts, nullptr) == nullptr);
}

// The answer's msid signalling follows the offer's (Unified Plan).
SM_TEST(PcMediaSession, AnswerMsidSignaling) {
  Fixture t;
  MediaSessionOptions opts;
  AddAudioVideoSections(RtpTransceiverDirection::kSendRecv, &opts);
  auto offer = t.s1.Offer(opts);
  auto answer = t.s2.Answer(offer.get(), opts);
  SM_EXPECT_EQ(answer->msid_signaling(), kMsidSignalingSemantic | kMsidSignalingMediaSection);
  offer->set_msid_signaling(kMsidSignalingSemantic | kMsidSignalingSsrcAttribute);
  answer = t.s2.Answer(offer.get(), opts);
  SM_EXPECT_EQ(answer->msid_signaling(), kMsidSignalingSemantic | kMsidSignalingSsrcAttribute);
}

// The default offer, as libwebrtc writes it for C6's subset: Opus 111, H.264 CB 96 with RTX 97, the engines'
// header extensions with the ids libwebrtc gives them (Chrome's offer: audio 1-4, video toffset 14, abs-send-time
// 2, video-orientation 13, transport-cc 3, playout-delay 5, content-type 6, timing 7, color-space 8, mid 4, rid 10,
// repaired-rid 11), max-bundle and rtcp-mux.
MediaSessionOptions DefaultSessionOptions(bool with_data) {
  MediaSessionOptions opts;
  opts.bundle_enabled = true;
  AddAudioVideoSections(RtpTransceiverDirection::kSendRecv, &opts);
  opts.media_description_options[0].header_extensions = DefaultAudioRtpHeaderExtensions();
  opts.media_description_options[1].header_extensions = DefaultVideoRtpHeaderExtensions();
  AttachSender(kAudioMid, MediaType::AUDIO, kAudioTrack1, {kMediaStream1}, 1, &opts);
  AttachSender(kVideoMid, MediaType::VIDEO, kVideoTrack1, {kMediaStream1}, 1, &opts);
  if (with_data) AddDataSection(RtpTransceiverDirection::kSendRecv, &opts);
  return opts;
}

SM_TEST(PcMediaSession, DefaultOfferMatchesLibwebrtc) {
  Fixture t;
  t.s1.SetCodecs(DefaultAudioCodecs(), DefaultVideoCodecs());
  auto offer = t.s1.Offer(DefaultSessionOptions(false));
  SM_ASSERT_TRUE(offer != nullptr);
  const auto* acd = GetFirstAudioContentDescription(offer.get());
  const auto* vcd = GetFirstVideoContentDescription(offer.get());
  SM_ASSERT_EQ(acd->codecs().size(), 1u);
  SM_EXPECT_EQ(acd->codecs()[0].id, 111);
  SM_ASSERT_EQ(vcd->codecs().size(), 2u);
  SM_EXPECT_EQ(vcd->codecs()[0].id, 96);
  SM_EXPECT_EQ(vcd->codecs()[1].id, 97);
  const std::vector<std::pair<std::string, int>> audio_ids = {
      {RtpExtension::kAudioLevelUri, 1}, {RtpExtension::kAbsSendTimeUri, 2},
      {RtpExtension::kTransportSequenceNumberUri, 3}, {RtpExtension::kMidUri, 4}};
  const std::vector<std::pair<std::string, int>> video_ids = {
      {RtpExtension::kTimestampOffsetUri, 14}, {RtpExtension::kAbsSendTimeUri, 2},
      {RtpExtension::kVideoRotationUri, 13},   {RtpExtension::kTransportSequenceNumberUri, 3},
      {RtpExtension::kPlayoutDelayUri, 5},     {RtpExtension::kVideoContentTypeUri, 6},
      {RtpExtension::kVideoTimingUri, 7},      {RtpExtension::kColorSpaceUri, 8},
      {RtpExtension::kMidUri, 4},              {RtpExtension::kRidUri, 10},
      {RtpExtension::kRepairedRidUri, 11}};
  SM_ASSERT_EQ(acd->rtp_header_extensions().size(), audio_ids.size());
  for (size_t i = 0; i < audio_ids.size(); ++i) {
    SM_EXPECT_EQ(acd->rtp_header_extensions()[i].uri, audio_ids[i].first);
    SM_EXPECT_EQ(acd->rtp_header_extensions()[i].id, audio_ids[i].second);
  }
  SM_ASSERT_EQ(vcd->rtp_header_extensions().size(), video_ids.size());
  for (size_t i = 0; i < video_ids.size(); ++i) {
    SM_EXPECT_EQ(vcd->rtp_header_extensions()[i].uri, video_ids[i].first);
    SM_EXPECT_EQ(vcd->rtp_header_extensions()[i].id, video_ids[i].second);
  }
  // The audio and video SSRCs differ; the video one has its RTX SSRC.
  SM_EXPECT_NE(acd->first_ssrc(), vcd->first_ssrc());
  SM_EXPECT_EQ(vcd->streams()[0].ssrcs.size(), 2u);
}

bool Contains(const std::string& sdp, const std::string& line) { return sdp.find(line + "\r\n") != std::string::npos; }

// An offer created here and answered by another factory round-trips through part 1's serializer and parser.
SM_TEST(PcMediaSession, OfferAnswerRoundTripThroughSdp) {
  Fixture t;
  t.s1.SetCodecs(DefaultAudioCodecs(), DefaultVideoCodecs());
  t.s2.SetCodecs(DefaultAudioCodecs(), DefaultVideoCodecs());
  const MediaSessionOptions opts = DefaultSessionOptions(true);
  auto offer_desc = t.s1.Offer(opts);
  SM_ASSERT_TRUE(offer_desc != nullptr);
  auto offer = SessionDescriptionInterface::Create(p2p::SdpType::kOffer, std::move(offer_desc), "1", "2");
  const std::string offer_sdp = SdpSerialize(*offer);
  SdpParseError parse_error;
  auto parsed_offer = SdpDeserialize(p2p::SdpType::kOffer, offer_sdp, &parse_error);
  SM_ASSERT_TRUE(parsed_offer != nullptr);
  SM_EXPECT_EQ(SdpSerialize(*parsed_offer), offer_sdp);
  for (const char* line : {"a=group:BUNDLE 0 1 2", "a=setup:actpass", "a=rtcp-mux", "a=rtcp-rsize",
                           "m=audio 9 UDP/TLS/RTP/SAVPF 111", "a=rtpmap:111 opus/48000/2",
                           "a=fmtp:111 minptime=10;useinbandfec=1", "a=rtcp-fb:111 transport-cc",
                           "m=video 9 UDP/TLS/RTP/SAVPF 96 97", "a=rtpmap:96 H264/90000",
                           "a=fmtp:96 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f",
                           "a=rtcp-fb:96 goog-remb", "a=rtcp-fb:96 transport-cc", "a=rtcp-fb:96 ccm fir",
                           "a=rtcp-fb:96 nack", "a=rtcp-fb:96 nack pli", "a=rtpmap:97 rtx/90000", "a=fmtp:97 apt=96",
                           "a=extmap:3 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01",
                           "a=extmap:2 http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time",
                           "m=application 9 UDP/DTLS/SCTP webrtc-datachannel", "a=sctp-port:5000",
                           "a=max-message-size:262144", "a=ice-options:trickle"}) {
    SM_EXPECT_TRUE(Contains(offer_sdp, line));
  }

  auto answer_desc = t.s2.Answer(parsed_offer->description(), opts);
  SM_ASSERT_TRUE(answer_desc != nullptr);
  auto answer = SessionDescriptionInterface::Create(p2p::SdpType::kAnswer, std::move(answer_desc), "3", "2");
  const std::string answer_sdp = SdpSerialize(*answer);
  auto parsed_answer = SdpDeserialize(p2p::SdpType::kAnswer, answer_sdp, &parse_error);
  SM_ASSERT_TRUE(parsed_answer != nullptr);
  SM_EXPECT_EQ(SdpSerialize(*parsed_answer), answer_sdp);
  for (const char* line : {"a=group:BUNDLE 0 1 2", "a=setup:active", "a=rtcp-mux", "a=rtpmap:111 opus/48000/2",
                           "a=rtpmap:96 H264/90000", "a=fmtp:97 apt=96",
                           "a=fmtp:96 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f",
                           "m=application 9 UDP/DTLS/SCTP webrtc-datachannel", "a=max-message-size:262144"}) {
    SM_EXPECT_TRUE(Contains(answer_sdp, line));
  }
  // The answer's sections all share the first one's transport, and nothing is rejected.
  const SessionDescription* a = parsed_answer->description();
  for (const ContentInfo& content : a->contents()) SM_EXPECT_FALSE(content.rejected);
  SM_EXPECT_EQ(a->transport_infos()[1].description.ice_ufrag, a->transport_infos()[0].description.ice_ufrag);
  SM_EXPECT_TRUE(a->transport_infos()[0].description.identity_fingerprint != nullptr);

  // The suggester records both descriptions, and a re-offer from the answerer keeps the negotiated payload types.
  SM_EXPECT_TRUE(t.s2.suggester.Update(parsed_offer->description(), /*local=*/false, p2p::SdpType::kOffer));
  SM_EXPECT_TRUE(t.s2.suggester.Update(a, /*local=*/true, p2p::SdpType::kAnswer));
  auto reoffer = t.s2.Offer(opts, a);
  SM_ASSERT_TRUE(reoffer != nullptr);
  SM_EXPECT_TRUE(CodecListsMatch(GetFirstVideoContentDescription(a)->codecs(),
                                 GetFirstVideoContentDescription(reoffer.get())->codecs()));
  SM_EXPECT_TRUE(GetFirstVideoContentDescription(a)->rtp_header_extensions() ==
                 GetFirstVideoContentDescription(reoffer.get())->rtp_header_extensions());
}

}  // namespace
