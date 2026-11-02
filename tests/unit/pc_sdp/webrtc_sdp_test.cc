// SDP parse and write (#1062, C6 part 1): cases of libwebrtc M157's api/webrtc_sdp_unittest.cc
// (compare/webrtc/reference/api/) for the subset C6 uses, with libwebrtc's own SDP strings (kSdpFullString,
// kSdpString, the SCTP strings), on sm_test.h, plus a round trip of a Chrome-style offer and answer. libwebrtc
// builds the expected description of kSdpFullString in code and checks SdpSerialize writes kSdpFullString; here the
// same string is parsed, its fields checked, and written back byte for byte.
#include <memory>
#include <string>
#include <vector>

#include "sm/pc/jsep.h"
#include "sm/pc/webrtc_sdp.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::pc;

namespace {

// Reference sdp string
constexpr char kSdpFullString[] =
    "v=0\r\n"
    "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=extmap-allow-mixed\r\n"
    "a=msid-semantic: WMS local_stream_1\r\n"
    "m=audio 2345 RTP/SAVPF 111 103 104\r\n"
    "c=IN IP4 74.125.127.126\r\n"
    "a=rtcp:2347 IN IP4 74.125.127.126\r\n"
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host generation 2\r\n"
    "a=candidate:a0+B/1 2 udp 2130706432 192.168.1.5 1235 typ host generation 2\r\n"
    "a=candidate:a0+B/2 1 udp 2130706432 ::1 1238 typ host generation 2\r\n"
    "a=candidate:a0+B/2 2 udp 2130706432 ::1 1239 typ host generation 2\r\n"
    "a=candidate:a0+B/3 1 udp 2130706432 74.125.127.126 2345 typ srflx raddr 192.168.1.5 rport 2346 generation 2\r\n"
    "a=candidate:a0+B/3 2 udp 2130706432 74.125.127.126 2347 typ srflx raddr 192.168.1.5 rport 2348 generation 2\r\n"
    "a=ice-ufrag:ufrag_voice\r\na=ice-pwd:pwd_voice\r\n"
    "a=fingerprint:sha-1 4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"
    "a=mid:audio_content_name\r\n"
    "a=sendrecv\r\n"
    "a=msid:local_stream_1 audio_track_id_1\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=rtpmap:104 ISAC/32000\r\n"
    "a=ssrc:1 cname:stream_1_cname\r\n"
    "m=video 3457 RTP/SAVPF 120\r\n"
    "c=IN IP4 74.125.224.39\r\n"
    "a=rtcp:3456 IN IP4 74.125.224.39\r\n"
    "a=candidate:a0+B/1 2 udp 2130706432 192.168.1.5 1236 typ host generation 2\r\n"
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1237 typ host generation 2\r\n"
    "a=candidate:a0+B/2 2 udp 2130706432 ::1 1240 typ host generation 2\r\n"
    "a=candidate:a0+B/2 1 udp 2130706432 ::1 1241 typ host generation 2\r\n"
    "a=candidate:a0+B/4 2 udp 2130706432 74.125.224.39 3456 typ relay generation 2\r\n"
    "a=candidate:a0+B/4 1 udp 2130706432 74.125.224.39 3457 typ relay generation 2\r\n"
    "a=ice-ufrag:ufrag_video\r\na=ice-pwd:pwd_video\r\n"
    "a=fingerprint:sha-1 4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"
    "a=mid:video_content_name\r\n"
    "a=sendrecv\r\n"
    "a=msid:local_stream_1 video_track_id_1\r\n"
    "a=rtpmap:120 VP8/90000\r\n"
    "a=ssrc-group:FEC 2 3\r\n"
    "a=ssrc:2 cname:stream_1_cname\r\n"
    "a=ssrc:3 cname:stream_1_cname\r\n";

// SDP reference string without the candidates.
constexpr char kSdpString[] =
    "v=0\r\n"
    "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=extmap-allow-mixed\r\n"
    "a=msid-semantic: WMS local_stream_1\r\n"
    "m=audio 9 RTP/SAVPF 111 103 104\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_voice\r\na=ice-pwd:pwd_voice\r\n"
    "a=fingerprint:sha-1 4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"
    "a=mid:audio_content_name\r\n"
    "a=sendrecv\r\n"
    "a=msid:local_stream_1 audio_track_id_1\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=rtpmap:104 ISAC/32000\r\n"
    "a=ssrc:1 cname:stream_1_cname\r\n"
    "m=video 9 RTP/SAVPF 120\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_video\r\na=ice-pwd:pwd_video\r\n"
    "a=fingerprint:sha-1 4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"
    "a=mid:video_content_name\r\n"
    "a=sendrecv\r\n"
    "a=msid:local_stream_1 video_track_id_1\r\n"
    "a=rtpmap:120 VP8/90000\r\n"
    "a=ssrc-group:FEC 2 3\r\n"
    "a=ssrc:2 cname:stream_1_cname\r\n"
    "a=ssrc:3 cname:stream_1_cname\r\n";

constexpr char kSdpSessionPrefix[] =
    "v=0\r\n"
    "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n";

// draft-ietf-mmusic-sctp-sdp-03
constexpr char kSdpSctpDataChannelString[] =
    "m=application 9 UDP/DTLS/SCTP 5000\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_data\r\n"
    "a=ice-pwd:pwd_data\r\n"
    "a=fingerprint:sha-1 4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"
    "a=mid:data_content_name\r\n"
    "a=sctpmap:5000 webrtc-datachannel 65535\r\n";

// draft-ietf-mmusic-sctp-sdp-12 (invalid per -26: the separator after "sctp-port" must be a colon).
constexpr char kSdpSctpDataChannelStringWithSctpPort[] =
    "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
    "a=sctp-port 5000\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_data\r\n"
    "a=ice-pwd:pwd_data\r\n"
    "a=fingerprint:sha-1 4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"
    "a=mid:data_content_name\r\n";

// draft-ietf-mmusic-sctp-sdp-26
constexpr char kSdpSctpDataChannelStringWithSctpColonPort[] =
    "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
    "a=sctp-port:5000\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_data\r\n"
    "a=ice-pwd:pwd_data\r\n"
    "a=fingerprint:sha-1 4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"
    "a=mid:data_content_name\r\n";

// draft-hancke-tsvwg-snap: a=sctp-init:<base64("CookieMonster")>
constexpr char kSdpSctpDataChannelStringWithSctpInit[] =
    "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
    "a=sctp-port:5000\r\n"
    "a=sctp-init:Q29va2llTW9uc3Rlcg==\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_data\r\n"
    "a=ice-pwd:pwd_data\r\n"
    "a=fingerprint:sha-1 4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"
    "a=mid:data_content_name\r\n";

std::unique_ptr<SessionDescriptionInterface> Parse(const std::string& sdp, SdpParseError* error = nullptr) {
  return SdpDeserialize(SdpType::kOffer, sdp, error);
}

void Replace(const std::string& line, const std::string& newlines, std::string* message) {
  const size_t pos = message->find(line);
  if (pos != std::string::npos) message->replace(pos, line.length(), newlines);
}

void InjectAfter(const std::string& line, const std::string& newlines, std::string* message) {
  const size_t pos = message->find(line);
  if (pos != std::string::npos) message->insert(pos + line.length(), newlines);
}

// Fails to parse `bad_sdp`, and the error's line contains `bad_part`.
void ExpectParseFailure(const std::string& bad_sdp, const std::string& bad_part) {
  SdpParseError error;
  SM_EXPECT_TRUE(Parse(bad_sdp, &error) == nullptr);
  SM_EXPECT_NE(std::string::npos, error.line.find(bad_part));
}

// Fails to parse kSdpFullString with `good_part` replaced by `bad_part`.
void ExpectParseFailure(const char* good_part, const char* bad_part) {
  std::string bad_sdp = kSdpFullString;
  Replace(good_part, bad_part, &bad_sdp);
  ExpectParseFailure(bad_sdp, bad_part);
}

// Fails to parse kSdpFullString with `newlines` added after `injectpoint`.
void ExpectParseFailureWithNewLines(const std::string& injectpoint, const std::string& newlines,
                                    const std::string& bad_part) {
  std::string bad_sdp = kSdpFullString;
  InjectAfter(injectpoint, newlines, &bad_sdp);
  ExpectParseFailure(bad_sdp, bad_part);
}

const MediaContentDescription* Media(const SessionDescriptionInterface& jdesc, size_t index) {
  return jdesc.description()->contents()[index].media_description();
}

SM_TEST(PcWebRtcSdp, DeserializeAndSerializeFullString) {
  auto jdesc = Parse(kSdpFullString);
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_EXPECT_EQ(jdesc->session_id(), std::string("18446744069414584320"));
  SM_EXPECT_EQ(jdesc->session_version(), std::string("18446462598732840960"));
  const SessionDescription* desc = jdesc->description();
  SM_ASSERT_EQ(desc->contents().size(), 2u);
  SM_EXPECT_TRUE(desc->extmap_allow_mixed());
  SM_EXPECT_EQ(desc->msid_signaling(), kMsidSignalingMediaSection | kMsidSignalingSemantic);

  // Audio.
  const ContentInfo& audio = desc->contents()[0];
  SM_EXPECT_EQ(audio.mid(), std::string("audio_content_name"));
  SM_EXPECT_TRUE(audio.type == MediaProtocolType::kRtp);
  SM_EXPECT_FALSE(audio.rejected);
  const MediaContentDescription* acd = audio.media_description();
  SM_ASSERT_TRUE(acd->as_audio() != nullptr);
  SM_EXPECT_EQ(acd->protocol(), std::string("RTP/SAVPF"));
  SM_EXPECT_TRUE(acd->rtcp_mux());
  SM_EXPECT_TRUE(acd->rtcp_reduced_size());
  SM_EXPECT_TRUE(acd->direction() == RtpTransceiverDirection::kSendRecv);
  SM_EXPECT_TRUE(acd->extmap_allow_mixed_level() == MediaContentDescription::AttributeLevel::kSession);
  SM_ASSERT_EQ(acd->codecs().size(), 3u);
  SM_EXPECT_EQ(acd->codecs()[0].id, 111);
  SM_EXPECT_EQ(acd->codecs()[0].name, std::string("opus"));
  SM_EXPECT_EQ(acd->codecs()[0].clockrate, 48000);
  SM_EXPECT_EQ(acd->codecs()[0].channels, 2u);
  SM_EXPECT_EQ(acd->codecs()[1].name, std::string("ISAC"));
  SM_EXPECT_EQ(acd->codecs()[2].clockrate, 32000);
  SM_ASSERT_EQ(acd->streams().size(), 1u);
  SM_EXPECT_EQ(acd->streams()[0].id, std::string("audio_track_id_1"));
  SM_EXPECT_EQ(acd->streams()[0].cname, std::string("stream_1_cname"));
  SM_EXPECT_EQ(acd->streams()[0].first_ssrc(), 1u);
  SM_EXPECT_EQ(std::string(acd->streams()[0].first_stream_id()), std::string("local_stream_1"));
  SM_EXPECT_EQ(acd->connection_address().ToString(), std::string("74.125.127.126:2345"));

  // Video: one track with both SSRCs and the FEC group.
  const MediaContentDescription* vcd = Media(*jdesc, 1);
  SM_ASSERT_TRUE(vcd->as_video() != nullptr);
  SM_ASSERT_EQ(vcd->streams().size(), 1u);
  SM_EXPECT_EQ(vcd->streams()[0].ssrcs.size(), 2u);
  SM_ASSERT_EQ(vcd->streams()[0].ssrc_groups.size(), 1u);
  SM_EXPECT_EQ(vcd->streams()[0].ssrc_groups[0].semantics, std::string("FEC"));
  SM_EXPECT_EQ(vcd->connection_address().ToString(), std::string("74.125.224.39:3457"));

  // Transports: ICE credentials and the DTLS fingerprint per mid.
  const TransportDescription* td = desc->GetTransportDescriptionByName("video_content_name");
  SM_ASSERT_TRUE(td != nullptr);
  SM_EXPECT_EQ(td->ice_ufrag, std::string("ufrag_video"));
  SM_EXPECT_EQ(td->ice_pwd, std::string("pwd_video"));
  SM_ASSERT_TRUE(td->identity_fingerprint != nullptr);
  SM_EXPECT_EQ(td->identity_fingerprint->algorithm, std::string("sha-1"));
  SM_EXPECT_EQ(td->identity_fingerprint->digest.size(), 20u);
  SM_EXPECT_TRUE(td->connection_role == p2p::ConnectionRole::kNone);

  // Candidates, with the m= section's credentials.
  SM_ASSERT_EQ(jdesc->candidates(0)->count(), 6u);
  const Candidate& srflx = jdesc->candidates(0)->at(4)->candidate();
  SM_EXPECT_TRUE(srflx.type() == p2p::IceCandidateType::kSrflx);
  SM_EXPECT_EQ(srflx.related_address().ToString(), std::string("192.168.1.5:2346"));
  SM_EXPECT_EQ(srflx.username(), std::string("ufrag_voice"));
  SM_EXPECT_EQ(srflx.password(), std::string("pwd_voice"));
  SM_EXPECT_EQ(srflx.generation(), 2u);
  SM_EXPECT_EQ(jdesc->candidates(0)->at(0)->sdp_mid(), std::string("audio_content_name"));
  SM_EXPECT_EQ(jdesc->candidates(1)->at(0)->sdp_mline_index(), 1);

  // libwebrtc's SerializeSessionDescription: the description writes kSdpFullString.
  SM_EXPECT_EQ(SdpSerialize(*jdesc), std::string(kSdpFullString));
}

SM_TEST(PcWebRtcSdp, DeserializeAndSerializeWithoutCandidates) {
  auto jdesc = Parse(kSdpString);
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_EXPECT_EQ(jdesc->candidates(0)->count(), 0u);
  SM_EXPECT_EQ(Media(*jdesc, 0)->connection_address().ToString(), std::string("0.0.0.0:9"));
  SM_EXPECT_EQ(SdpSerialize(*jdesc), std::string(kSdpString));
}

SM_TEST(PcWebRtcSdp, DeserializeSessionDescriptionWithoutCarriageReturn) {
  std::string sdp = kSdpFullString;
  std::string no_cr;
  for (char c : sdp) {
    if (c != '\r') no_cr += c;
  }
  auto jdesc = Parse(no_cr);
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_EXPECT_EQ(SdpSerialize(*jdesc), std::string(kSdpFullString));
}

SM_TEST(PcWebRtcSdp, DeserializeSessionDescriptionWithoutMline) {
  auto jdesc = Parse(kSdpSessionPrefix);
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_EXPECT_EQ(jdesc->description()->contents().size(), 0u);
  SM_EXPECT_EQ(jdesc->description()->msid_signaling(), kMsidSignalingNotUsed);
}

SM_TEST(PcWebRtcSdp, DeserializeSessionDescriptionWithoutEndLineBreak) {
  std::string sdp = kSdpFullString;
  sdp = sdp.substr(0, sdp.size() - 2);  // Remove the last "\r\n".
  SdpParseError error;
  SM_EXPECT_TRUE(Parse(sdp, &error) == nullptr);
  SM_EXPECT_EQ(error.line, std::string("a=ssrc:3 cname:stream_1_cname"));
  SM_EXPECT_EQ(error.description, std::string("Invalid SDP line."));
}

SM_TEST(PcWebRtcSdp, DeserializeSessionDescriptionWithoutRtpmap) {
  std::string sdp = std::string(kSdpSessionPrefix) +
                    "m=audio 9 RTP/AVP 0 18 103\r\n"
                    // Codec that doesn't appear in the m= line will be ignored.
                    "a=rtpmap:104 ISAC/32000\r\n"
                    // The rtpmap line for static payload codec is optional.
                    "a=rtpmap:18 G729/8000\r\n"
                    "a=rtpmap:103 ISAC/16000\r\n";
  auto jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  const std::vector<Codec>& codecs = Media(*jdesc, 0)->codecs();
  SM_ASSERT_EQ(codecs.size(), 3u);
  SM_EXPECT_EQ(codecs[0].name, std::string("PCMU"));
  SM_EXPECT_EQ(codecs[0].id, 0);
  SM_EXPECT_EQ(codecs[1].name, std::string("G729"));
  SM_EXPECT_EQ(codecs[2].name, std::string("ISAC"));
  SM_EXPECT_EQ(codecs[2].id, 103);
}

SM_TEST(PcWebRtcSdp, DeserializeSessionDescriptionWithoutRtpmapButWithFmtp) {
  std::string sdp = std::string(kSdpSessionPrefix) +
                    "m=audio 49232 RTP/AVP 18\r\n"
                    "a=fmtp:18 annexb=yes\r\n";
  auto jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  const Codec& g729 = Media(*jdesc, 0)->codecs()[0];
  SM_EXPECT_EQ(g729.name, std::string("G729"));
  SM_EXPECT_EQ(g729.clockrate, 8000);
  std::string value;
  SM_EXPECT_TRUE(g729.GetParam("annexb", &value));
  SM_EXPECT_EQ(value, std::string("yes"));
}

SM_TEST(PcWebRtcSdp, ShrugsOnUnknownStaticAudioCodecs) {
  // An fmtp for a payload type without rtpmap and without a static payload type fails ("Failed to parse codecs").
  std::string sdp = std::string(kSdpSessionPrefix) +
                    "m=audio 9 RTP/AVP 30\r\n"
                    "a=fmtp:30 something=1\r\n";
  SdpParseError error;
  SM_EXPECT_TRUE(Parse(sdp, &error) == nullptr);
  SM_EXPECT_EQ(error.description, std::string("Failed to parse codecs correctly."));
}

SM_TEST(PcWebRtcSdp, DeserializeBrokenSdp) {
  const char kSdpDestroyer[] = "!@#$%^&";
  const char kSdpEmptyType[] = " =candidate";
  const char kSdpEqualAsPlus[] = "a+candidate";
  const char kSdpSpaceAfterEqual[] = "a= candidate";
  const char kSdpUpperType[] = "A=candidate";
  const char kSdpEmptyLine[] = "";
  const char kSdpMissingValue[] = "a=";
  const char kSdpBrokenFingerprint[] = "a=fingerprint:sha-1 4AAD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB";
  const char kSdpExtraField[] =
      "a=fingerprint:sha-1 4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB XXX";
  const char kSdpMissingSpace[] = "a=fingerprint:sha-14A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB";
  // MD5 is not allowed in fingerprints.
  const char kSdpMd5[] = "a=fingerprint:md5 4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B";

  // Broken session description
  ExpectParseFailure("v=", kSdpDestroyer);
  ExpectParseFailure("o=", kSdpDestroyer);
  ExpectParseFailure("s=-", kSdpDestroyer);
  // Broken time description
  ExpectParseFailure("t=", kSdpDestroyer);
  // Broken media description
  ExpectParseFailure("m=audio", "c=IN IP4 74.125.224.39");
  ExpectParseFailure("m=video", kSdpDestroyer);
  ExpectParseFailure("m=", "c=IN IP4 74.125.224.39");
  // Invalid lines
  ExpectParseFailure("a=candidate", kSdpEmptyType);
  ExpectParseFailure("a=candidate", kSdpEqualAsPlus);
  ExpectParseFailure("a=candidate", kSdpSpaceAfterEqual);
  ExpectParseFailure("a=candidate", kSdpUpperType);
  // Bogus fingerprint replacing a=sendrev. We selected this attribute because it's orthogonal to what we are
  // replacing and hence safe.
  ExpectParseFailure("a=sendrecv", kSdpBrokenFingerprint);
  ExpectParseFailure("a=sendrecv", kSdpExtraField);
  ExpectParseFailure("a=sendrecv", kSdpMissingSpace);
  ExpectParseFailure("a=sendrecv", kSdpMd5);
  // Empty Line
  ExpectParseFailure("a=rtcp:2347 IN IP4 74.125.127.126", kSdpEmptyLine);
  ExpectParseFailure("a=rtcp:2347 IN IP4 74.125.127.126", kSdpMissingValue);
}

SM_TEST(PcWebRtcSdp, DeserializeSdpWithInvalidAttributeValue) {
  // ssrc
  ExpectParseFailure("a=ssrc:1", "a=ssrc:badvalue");
  ExpectParseFailure("a=ssrc-group:FEC 2 3", "a=ssrc-group:FEC badvalue 3");
  ExpectParseFailure("a=ssrc-group:FEC 2 3",
                     "a=ssrc-group:FEC 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 "
                     "22 23 24 25 26 27 28 29 30 31 32 33 34 35");
  // rtpmap
  ExpectParseFailure("a=rtpmap:111 ", "a=rtpmap:badvalue ");
  ExpectParseFailure("opus/48000/2", "opus/badvalue/2");
  ExpectParseFailure("opus/48000/2", "opus/48000/badvalue");
  // candidate
  ExpectParseFailure("1 udp 2130706432", "badvalue udp 2130706432");
  ExpectParseFailure("1 udp 2130706432", "1 udp badvalue");
  ExpectParseFailure("192.168.1.5 1234", "192.168.1.5 badvalue");
  ExpectParseFailure("rport 2346", "rport badvalue");
  ExpectParseFailure("rport 2346 generation 2", "rport 2346 generation badvalue");
  // m line
  ExpectParseFailure("m=audio 2345 RTP/SAVPF 111 103 104", "m=audio 2345 RTP/SAVPF 111 badvalue 104");
  // bandwidth
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n", "b=AS:badvalue\r\n", "b=AS:badvalue");
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n", "b=AS\r\n", "b=AS");
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n", "b=AS:\r\n", "b=AS:");
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n", "b=AS:12:34\r\n", "b=AS:12:34");
  // rtcp-fb
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n", "a=rtcp-fb:badvalue nack\r\n",
                                 "a=rtcp-fb:badvalue nack");
  // extmap
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n", "a=extmap:badvalue http://example.com\r\n",
                                 "a=extmap:badvalue http://example.com");
}

SM_TEST(PcWebRtcSdp, DeserializeSdpWithInvalidExtmapUri) {
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n", "a=extmap:1 http://example.com/\x01\r\n",
                                 "a=extmap:1 http://example.com/\x01");
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n", "a=extmap:1 http://example.com/%0a\r\n",
                                 "a=extmap:1 http://example.com/%0a");
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n", "a=extmap:1 http://example.com/%0d\r\n",
                                 "a=extmap:1 http://example.com/%0d");
}

SM_TEST(PcWebRtcSdp, ExtmapIdRange) {
  // DeserializeSdpWithZeroExtmapId / DeserializeSdpWithTooLargeExtmapId.
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n", "a=extmap:0 urn:ietf:params:rtp-hdrext:toffset\r\n",
                                 "a=extmap:0 urn:ietf:params:rtp-hdrext:toffset");
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n",
                                 "a=extmap:256 urn:ietf:params:rtp-hdrext:toffset\r\n",
                                 "a=extmap:256 urn:ietf:params:rtp-hdrext:toffset");
}

SM_TEST(PcWebRtcSdp, DeserializeSessionDescriptionWithMediaLevelExtmap) {
  std::string sdp = kSdpString;
  InjectAfter("a=mid:video_content_name\r\n",
              "a=extmap:1/sendrecv urn:ietf:params:rtp-hdrext:toffset\r\n"
              "a=extmap:2 urn:ietf:params:rtp-hdrext:encrypt http://example.com/082005/ext.htm#ttime a1 a2\r\n",
              &sdp);
  auto jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  const RtpHeaderExtensions& exts = Media(*jdesc, 1)->rtp_header_extensions();
  SM_ASSERT_EQ(exts.size(), 2u);
  SM_EXPECT_TRUE(exts[0] == RtpExtension("urn:ietf:params:rtp-hdrext:toffset", 1, false));
  SM_EXPECT_TRUE(exts[1] == RtpExtension("http://example.com/082005/ext.htm#ttime", 2, true));
  // Written at media level, after the direction-independent attributes and before a=sendrecv.
  const std::string out = SdpSerialize(*jdesc);
  SM_EXPECT_NE(out.find("a=extmap:1 urn:ietf:params:rtp-hdrext:toffset\r\n"
                        "a=extmap:2 urn:ietf:params:rtp-hdrext:encrypt http://example.com/082005/ext.htm#ttime\r\n"
                        "a=sendrecv\r\n"),
               std::string::npos);
  SM_EXPECT_EQ(Media(*jdesc, 0)->rtp_header_extensions().size(), 0u);
}

SM_TEST(PcWebRtcSdp, SessionLevelExtmapAndMixedUsage) {
  // ParseSessionLevelExtmapAttributes / RejectSessionLevelMediaLevelExtmapMixedUsage.
  std::string sdp = kSdpString;
  InjectAfter("t=0 0\r\n", "a=extmap:3 urn:ietf:params:rtp-hdrext:ssrc-audio-level\r\n", &sdp);
  auto jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_EXPECT_EQ(Media(*jdesc, 0)->rtp_header_extensions().size(), 1u);
  SM_EXPECT_EQ(Media(*jdesc, 1)->rtp_header_extensions().size(), 1u);
  InjectAfter("a=mid:video_content_name\r\n", "a=extmap:1 urn:ietf:params:rtp-hdrext:toffset\r\n", &sdp);
  SdpParseError error;
  SM_EXPECT_TRUE(Parse(sdp, &error) == nullptr);
  SM_EXPECT_EQ(error.description, std::string("The a=extmap MUST be either all session level or all media level."));
}

SM_TEST(PcWebRtcSdp, ExtmapAllowMixedLevels) {
  // DeserializeSessionDescriptionWithoutExtmapAllowMixed / DeserializeMediaContentDescriptionWithExtmapAllowMixed.
  std::string sdp = kSdpString;
  Replace("a=extmap-allow-mixed\r\n", "", &sdp);
  InjectAfter("a=mid:video_content_name\r\n", "a=extmap-allow-mixed\r\n", &sdp);
  auto jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_EXPECT_FALSE(jdesc->description()->extmap_allow_mixed());
  SM_EXPECT_TRUE(Media(*jdesc, 0)->extmap_allow_mixed_level() == MediaContentDescription::AttributeLevel::kNone);
  SM_EXPECT_TRUE(Media(*jdesc, 1)->extmap_allow_mixed_level() == MediaContentDescription::AttributeLevel::kMedia);
  // Written back at media level only.
  const std::string out = SdpSerialize(*jdesc);
  SM_EXPECT_EQ(out.find("a=extmap-allow-mixed"), out.rfind("a=extmap-allow-mixed"));
  SM_EXPECT_GT(out.find("a=extmap-allow-mixed"), out.find("m=video"));
}

SM_TEST(PcWebRtcSdp, DirectionsRoundTrip) {
  // DeserializeSessionDescriptionWith{RecvOnly,SendOnly,Inactive}Content and the Serialize cases.
  const std::pair<const char*, RtpTransceiverDirection> kCases[] = {
      {"a=recvonly", RtpTransceiverDirection::kRecvOnly},
      {"a=sendonly", RtpTransceiverDirection::kSendOnly},
      {"a=inactive", RtpTransceiverDirection::kInactive}};
  for (const auto& [line, direction] : kCases) {
    std::string sdp = kSdpFullString;
    Replace("a=sendrecv", line, &sdp);
    Replace("a=sendrecv", line, &sdp);
    auto jdesc = Parse(sdp);
    SM_ASSERT_TRUE(jdesc != nullptr);
    SM_EXPECT_TRUE(Media(*jdesc, 0)->direction() == direction);
    SM_EXPECT_TRUE(Media(*jdesc, 1)->direction() == direction);
    SM_EXPECT_EQ(SdpSerialize(*jdesc), sdp);
  }
}

SM_TEST(PcWebRtcSdp, RejectedContentRoundTrip) {
  // DeserializeSessionDescriptionWithRejected{Audio,Video,AudioVideo} and SerializeSessionDescriptionWith*Rejected.
  std::string sdp = kSdpString;
  Replace("m=audio 9", "m=audio 0", &sdp);
  Replace("m=video 9", "m=video 0", &sdp);
  auto jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_EXPECT_TRUE(jdesc->description()->contents()[0].rejected);
  SM_EXPECT_TRUE(jdesc->description()->contents()[1].rejected);
  SM_EXPECT_EQ(SdpSerialize(*jdesc), sdp);
}

SM_TEST(PcWebRtcSdp, BundleAndBundleOnly) {
  // DeserializeSessionDescriptionWithBundle, DeserializeBundleOnlyAttribute, IgnoreBundleOnlyWithNonzeroPort,
  // SerializeBundleOnlyAttribute.
  std::string sdp = kSdpString;
  InjectAfter("t=0 0\r\n", "a=group:BUNDLE audio_content_name video_content_name\r\n", &sdp);
  Replace("m=video 9", "m=video 0", &sdp);
  InjectAfter("c=IN IP4 0.0.0.0\r\na=rtcp:9 IN IP4 0.0.0.0\r\na=ice-ufrag:ufrag_video", "", &sdp);
  Replace("a=rtcp:9 IN IP4 0.0.0.0\r\na=ice-ufrag:ufrag_video",
          "a=bundle-only\r\na=rtcp:9 IN IP4 0.0.0.0\r\na=ice-ufrag:ufrag_video", &sdp);
  auto jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  const ContentGroup* group = jdesc->description()->GetGroupByName(GROUP_TYPE_BUNDLE);
  SM_ASSERT_TRUE(group != nullptr);
  SM_EXPECT_EQ(group->content_names().size(), 2u);
  const ContentInfo& video = jdesc->description()->contents()[1];
  SM_EXPECT_TRUE(video.bundle_only);
  SM_EXPECT_FALSE(video.rejected);
  SM_EXPECT_EQ(SdpSerialize(*jdesc), sdp);

  // bundle-only with a nonzero port is ignored.
  Replace("m=video 0", "m=video 9", &sdp);
  jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_EXPECT_FALSE(jdesc->description()->contents()[1].bundle_only);
  SM_EXPECT_FALSE(jdesc->description()->contents()[1].rejected);
}

SM_TEST(PcWebRtcSdp, RejectsInvalidCharactersInBundleGroup) {
  std::string sdp = kSdpString;
  InjectAfter("t=0 0\r\n", "a=group:BUNDLE \u1f937\r\n", &sdp);
  ExpectParseFailure(sdp, "a=group:BUNDLE \u1f937");
}

SM_TEST(PcWebRtcSdp, Bandwidth) {
  // DeserializeSessionDescriptionWithBandwidth / TiasBandwidth / DeserializeLargeBandwidthLimit /
  // DeserializingNegativeBandwidthLimitFails / SerializeSessionDescriptionWithBandwidth.
  std::string sdp = kSdpString;
  InjectAfter("a=mid:video_content_name\r\na=sendrecv\r\n", "b=AS:100\r\n", &sdp);
  InjectAfter("a=mid:audio_content_name\r\na=sendrecv\r\n", "b=AS:50\r\n", &sdp);
  auto jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_EXPECT_EQ(Media(*jdesc, 0)->bandwidth(), 50000);
  SM_EXPECT_EQ(Media(*jdesc, 1)->bandwidth(), 100000);
  SM_EXPECT_NE(SdpSerialize(*jdesc).find("c=IN IP4 0.0.0.0\r\nb=AS:100\r\n"), std::string::npos);

  sdp = kSdpString;
  InjectAfter("a=mid:video_content_name\r\na=sendrecv\r\n", "b=TIAS:100000\r\n", &sdp);
  jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_EXPECT_EQ(Media(*jdesc, 1)->bandwidth(), 100000);
  SM_EXPECT_EQ(Media(*jdesc, 1)->bandwidth_type(), std::string("TIAS"));
  SM_EXPECT_NE(SdpSerialize(*jdesc).find("b=TIAS:100000\r\n"), std::string::npos);

  sdp = kSdpString;
  InjectAfter("a=mid:video_content_name\r\na=sendrecv\r\n", "b=AS:2147483647\r\n", &sdp);
  jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_EXPECT_EQ(Media(*jdesc, 1)->bandwidth(), 2147483000);

  sdp = kSdpString;
  InjectAfter("a=mid:video_content_name\r\na=sendrecv\r\n", "b=AS:-1000\r\n", &sdp);
  ExpectParseFailure(sdp, "b=AS:-1000");

  // An unknown bandwidth type is ignored.
  sdp = kSdpString;
  InjectAfter("a=mid:video_content_name\r\na=sendrecv\r\n", "b=CT:100\r\n", &sdp);
  jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_EXPECT_EQ(Media(*jdesc, 1)->bandwidth(), kAutoBandwidth);
}

SM_TEST(PcWebRtcSdp, IceOptionsUfragPwdAndIceLite) {
  // DeserializeSessionDescriptionWithIceOptions / UfragPwd / DeserializeAndSerializeSdpWithIceLite.
  std::string sdp = kSdpString;
  InjectAfter("t=0 0\r\n", "a=ice-options:iceoption3\r\n", &sdp);
  InjectAfter("a=ice-pwd:pwd_voice\r\n", "a=ice-options:iceoption1 iceoption2\r\n", &sdp);
  auto jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  const TransportDescription* audio_td = jdesc->description()->GetTransportDescriptionByName("audio_content_name");
  const TransportDescription* video_td = jdesc->description()->GetTransportDescriptionByName("video_content_name");
  SM_ASSERT_TRUE(audio_td != nullptr && video_td != nullptr);
  // Media-level options add to the session-level ones.
  SM_EXPECT_EQ(audio_td->transport_options.size(), 3u);
  SM_EXPECT_EQ(video_td->transport_options.size(), 1u);
  SM_EXPECT_TRUE(audio_td->HasOption("iceoption2"));
  SM_EXPECT_TRUE(video_td->HasOption("iceoption3"));

  sdp = kSdpString;
  InjectAfter("t=0 0\r\n", "a=ice-lite\r\n", &sdp);
  jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_EXPECT_TRUE(jdesc->description()->transport_infos()[0].description.ice_mode == p2p::ICEMODE_LITE);
  std::string expected = kSdpString;
  InjectAfter("a=msid-semantic: WMS local_stream_1\r\n", "a=ice-lite\r\n", &expected);
  SM_EXPECT_EQ(SdpSerialize(*jdesc), expected);
}

SM_TEST(PcWebRtcSdp, DtlsSetupAttribute) {
  // SerializeDtlsSetupAttribute / DeserializeDtlsSetupAttribute{Actpass,Active,Passive}.
  const std::pair<const char*, p2p::ConnectionRole> kCases[] = {{"actpass", p2p::ConnectionRole::kActpass},
                                                                {"active", p2p::ConnectionRole::kActive},
                                                                {"passive", p2p::ConnectionRole::kPassive}};
  for (const auto& [role_str, role] : kCases) {
    std::string sdp = kSdpString;
    const std::string setup = std::string("a=setup:") + role_str + "\r\n";
    InjectAfter("a=fingerprint:sha-1 4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n", setup, &sdp);
    auto jdesc = Parse(sdp);
    SM_ASSERT_TRUE(jdesc != nullptr);
    SM_EXPECT_TRUE(jdesc->description()->transport_infos()[0].description.connection_role == role);
    SM_EXPECT_TRUE(jdesc->description()->transport_infos()[1].description.connection_role ==
                   p2p::ConnectionRole::kNone);
    SM_EXPECT_EQ(SdpSerialize(*jdesc), sdp);
  }
  std::string sdp = kSdpString;
  InjectAfter("a=mid:audio_content_name\r\n", "a=setup:bogus\r\n", &sdp);
  ExpectParseFailure(sdp, "a=setup:bogus");
}

SM_TEST(PcWebRtcSdp, SessionLevelFingerprintAppliesToEveryTransport) {
  // DeserializeJsepSessionDescriptionWithFingerprint: a session-level fingerprint is the default.
  std::string sdp = std::string(kSdpSessionPrefix) +
                    "a=fingerprint:sha-256 "
                    "6E:B5:4A:72:E6:63:65:40:A2:2A:32:BE:6D:6D:D2:B8:21:B5:F7:E8:66:2B:4D:55:B0:A6:5D:BF:8E:9F:24:C5\r\n"
                    "a=setup:actpass\r\n"
                    "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n"
                    "a=mid:0\r\n"
                    "a=rtpmap:111 opus/48000/2\r\n";
  auto jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  const TransportDescription& td = jdesc->description()->transport_infos()[0].description;
  SM_ASSERT_TRUE(td.identity_fingerprint != nullptr);
  SM_EXPECT_EQ(td.identity_fingerprint->algorithm, std::string("sha-256"));
  SM_EXPECT_TRUE(td.connection_role == p2p::ConnectionRole::kActpass);
  // Two session-level fingerprints fail.
  std::string twice = sdp;
  InjectAfter("a=setup:actpass\r\n", "a=fingerprint:sha-1 4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n",
              &twice);
  SdpParseError error;
  SM_EXPECT_TRUE(Parse(twice, &error) == nullptr);
  SM_EXPECT_EQ(error.description, std::string("Can't have multiple fingerprint attributes at the same level."));
}

SM_TEST(PcWebRtcSdp, ConferenceFlagAndRemoteEstimate) {
  // DeserializeSdpWithConferenceFlag / SerializeSdpWithConferenceFlag / SerializeAndDeserializeRemoteNetEstimate.
  std::string sdp = kSdpString;
  InjectAfter("a=mid:video_content_name\r\n", "a=x-google-flag:conference\r\na=remote-net-estimate\r\n", &sdp);
  auto jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_EXPECT_TRUE(Media(*jdesc, 1)->conference_mode());
  SM_EXPECT_TRUE(Media(*jdesc, 1)->remote_estimate());
  SM_EXPECT_FALSE(Media(*jdesc, 0)->conference_mode());
  auto again = Parse(SdpSerialize(*jdesc));
  SM_ASSERT_TRUE(again != nullptr);
  SM_EXPECT_TRUE(Media(*again, 1)->conference_mode());
  SM_EXPECT_TRUE(Media(*again, 1)->remote_estimate());
}

SM_TEST(PcWebRtcSdp, SframeAndRtcpXr) {
  // DeserializeSframeAttribute / SframeAttributeRoundTrip / DeserializeSerializeRtcpXrRcvrRtt.
  std::string sdp = kSdpString;
  InjectAfter("a=mid:video_content_name\r\n", "a=sframe\r\na=rtcp-xr:rcvr-rtt=sender\r\n", &sdp);
  auto jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_EXPECT_TRUE(Media(*jdesc, 1)->sframe_enabled());
  SM_EXPECT_TRUE(Media(*jdesc, 1)->receive_non_sender_rtt());
  SM_EXPECT_FALSE(Media(*jdesc, 0)->sframe_enabled());
  const std::string out = SdpSerialize(*jdesc);
  SM_EXPECT_NE(out.find("a=rtcp-xr:rcvr-rtt=all\r\na=sframe\r\na=rtpmap:120"), std::string::npos);
  // A partial match ("rcvr-rttx") is not rcvr-rtt.
  sdp = kSdpString;
  InjectAfter("a=mid:video_content_name\r\n", "a=rtcp-xr:rcvr-rtt\r\n", &sdp);
  jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_EXPECT_FALSE(Media(*jdesc, 1)->receive_non_sender_rtt());
}

SM_TEST(PcWebRtcSdp, RtcpFbAndFmtp) {
  // DeserializeSerializeCodecParams / DeserializeSerializeRtcpFb / DeserializeVideoFmtp / packetization.
  std::string sdp = std::string(kSdpSessionPrefix) +
                    "m=audio 9 RTP/SAVPF 111 104 103 105\r\n"
                    "a=mid:0\r\n"
                    "a=rtpmap:111 opus/48000/2\r\n"
                    "a=rtcp-fb:111 transport-cc\r\n"
                    "a=fmtp:111 minptime=10; stereo=1; sprop-stereo=1; useinbandfec=1; maxaveragebitrate=128000\r\n"
                    "a=rtpmap:104 ISAC/32000\r\n"
                    "a=rtpmap:103 ISAC/16000\r\n"
                    "a=rtpmap:105 telephone-event/8000\r\n"
                    "a=fmtp:105 0-15\r\n"
                    "a=ptime:20\r\n"
                    "a=maxptime:120\r\n"
                    "m=video 9 RTP/SAVPF 101 102\r\n"
                    "a=mid:1\r\n"
                    "a=rtpmap:101 VP8/90000\r\n"
                    "a=rtcp-fb:101 nack\r\n"
                    "a=rtcp-fb:101 nack pli\r\n"
                    "a=rtcp-fb:101 goog-remb\r\n"
                    "a=rtcp-fb:101 ccm fir\r\n"
                    "a=rtpmap:102 rtx/90000\r\n"
                    "a=fmtp:102 apt=101\r\n"
                    "a=packetization:101 raw\r\n";
  auto jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  const std::vector<Codec>& audio = Media(*jdesc, 0)->codecs();
  SM_ASSERT_EQ(audio.size(), 4u);
  std::string value;
  SM_EXPECT_TRUE(audio[0].GetParam("useinbandfec", &value));
  SM_EXPECT_EQ(value, std::string("1"));
  SM_EXPECT_TRUE(audio[0].GetParam("maxaveragebitrate", &value));
  SM_EXPECT_EQ(value, std::string("128000"));
  // a=ptime and a=maxptime apply to every codec of the m= section.
  SM_EXPECT_TRUE(audio[2].GetParam("ptime", &value));
  SM_EXPECT_EQ(value, std::string("20"));
  SM_EXPECT_TRUE(audio[2].GetParam("maxptime", &value));
  SM_EXPECT_EQ(value, std::string("120"));
  // An unnamed fmtp parameter (RFC 4733's event list).
  SM_EXPECT_TRUE(audio[3].GetParam("", &value));
  SM_EXPECT_EQ(value, std::string("0-15"));
  SM_EXPECT_TRUE(audio[0].HasFeedbackParam(FeedbackParam("transport-cc")));
  SM_EXPECT_TRUE(Media(*jdesc, 0)->rtcp_fb_ack_transport_cc());

  const std::vector<Codec>& video = Media(*jdesc, 1)->codecs();
  SM_ASSERT_EQ(video.size(), 2u);
  SM_EXPECT_EQ(video[0].feedback_params.params().size(), 4u);
  SM_EXPECT_TRUE(video[0].HasFeedbackParam(FeedbackParam("nack", "pli")));
  SM_EXPECT_TRUE(video[0].HasFeedbackParam(FeedbackParam("ccm", "fir")));
  SM_EXPECT_TRUE(video[0].packetization == std::optional<std::string>("raw"));
  SM_EXPECT_TRUE(video[1].GetParam("apt", &value));
  SM_EXPECT_EQ(value, std::string("101"));

  // Written back as libwebrtc writes them: fmtp sorted by name, ptime/maxptime as their own lines.
  const std::string out = SdpSerialize(*jdesc);
  SM_EXPECT_NE(out.find("a=rtpmap:111 opus/48000/2\r\na=rtcp-fb:111 transport-cc\r\n"
                        "a=fmtp:111 maxaveragebitrate=128000;minptime=10;sprop-stereo=1;stereo=1;useinbandfec=1\r\n"),
               std::string::npos);
  SM_EXPECT_NE(out.find("a=fmtp:105 0-15\r\na=maxptime:120\r\na=ptime:20\r\n"), std::string::npos);
  SM_EXPECT_NE(out.find("a=rtpmap:101 VP8/90000\r\na=packetization:101 raw\r\na=rtcp-fb:101 nack\r\n"),
               std::string::npos);
  auto again = Parse(out);
  SM_ASSERT_TRUE(again != nullptr);
  SM_EXPECT_EQ(SdpSerialize(*again), out);
}

SM_TEST(PcWebRtcSdp, DeserializeVideoFmtpWithSpace) {
  std::string sdp = std::string(kSdpSessionPrefix) +
                    "m=video 3457 RTP/SAVPF 120\r\n"
                    "a=rtpmap:120 VP8/90000\r\n"
                    "a=fmtp:120   x-google-min-bitrate=10;  x-google-max-quantization=40\r\n";
  auto jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  const Codec& vp8 = Media(*jdesc, 0)->codecs()[0];
  SM_EXPECT_EQ(vp8.name, std::string("VP8"));
  SM_EXPECT_EQ(vp8.id, 120);
  std::string value;
  SM_EXPECT_TRUE(vp8.GetParam("x-google-min-bitrate", &value));
  SM_EXPECT_EQ(value, std::string("10"));
  SM_EXPECT_TRUE(vp8.GetParam("x-google-max-quantization", &value));
  SM_EXPECT_EQ(value, std::string("40"));
}

SM_TEST(PcWebRtcSdp, DeserializePacketizationAttributeWithIllegalValue) {
  std::string sdp = std::string(kSdpSessionPrefix) +
                    "m=video 9 RTP/SAVPF 101 102\r\n"
                    "a=rtpmap:101 VP8/90000\r\n"
                    "a=packetization:101 unknownpacketizationattributevalue\r\n"
                    "a=rtpmap:102 VP9/90000\r\n"
                    "a=packetization:102 raw\r\n";
  auto jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_EXPECT_FALSE(Media(*jdesc, 0)->codecs()[0].packetization.has_value());
  SM_EXPECT_TRUE(Media(*jdesc, 0)->codecs()[1].packetization == std::optional<std::string>("raw"));
}

SM_TEST(PcWebRtcSdp, RtcpFbWildcardAndCcfb) {
  // DeserializeSerializeRtcpFbWildcard / DeserializeSerializeRtcpWithCcfb.
  std::string sdp = std::string(kSdpSessionPrefix) +
                    "m=video 9 RTP/SAVPF 101 102\r\n"
                    "a=mid:0\r\n"
                    "a=rtpmap:101 VP8/90000\r\n"
                    "a=rtpmap:102 VP9/90000\r\n"
                    "a=rtcp-fb:* nack\r\n"
                    "a=rtcp-fb:* ack ccfb\r\n"
                    "a=rtcp-fb:102 goog-remb\r\n";
  auto jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  const MediaContentDescription* vcd = Media(*jdesc, 0);
  SM_ASSERT_EQ(vcd->codecs().size(), 2u);
  SM_EXPECT_TRUE(vcd->codecs()[0].HasFeedbackParam(FeedbackParam("nack")));
  SM_EXPECT_TRUE(vcd->codecs()[1].HasFeedbackParam(FeedbackParam("nack")));
  SM_EXPECT_TRUE(vcd->codecs()[1].HasFeedbackParam(FeedbackParam("goog-remb")));
  SM_EXPECT_TRUE(vcd->rtcp_fb_ack_ccfb());
  SM_EXPECT_TRUE(vcd->preferred_rtcp_cc_ack_type() == RtcpFeedbackType::CCFB);
  // Without wildcards (libwebrtc's default), every codec gets its own lines.
  const std::string out = SdpSerialize(*jdesc);
  SM_EXPECT_EQ(out.find("a=rtcp-fb:*"), std::string::npos);
  SM_EXPECT_NE(out.find("a=rtcp-fb:101 nack\r\na=rtcp-fb:101 ack ccfb\r\n"), std::string::npos);
  // With EncodingOptions::use_wildcard, the shared ones are written once.
  auto wildcard = SessionDescriptionInterface::Create(SdpType::kOffer, jdesc->description()->Clone(), "1", "2", {},
                                                      EncodingOptions{.use_wildcard = true});
  const std::string out_wildcard = SdpSerialize(*wildcard);
  SM_EXPECT_NE(out_wildcard.find("a=rtcp-fb:102 goog-remb\r\na=fmtp:102 profile-id=0\r\na=rtcp-fb:* ack ccfb\r\n"
                                 "a=rtcp-fb:* nack\r\n"),
               std::string::npos);
}

SM_TEST(PcWebRtcSdp, RtpmapConflictsAndLimits) {
  // MaxChannels / DuplicateAudioRtpmapWithConflict / DuplicateVideoRtpmapWithConflict / FmtpBeforeRtpMap /
  // StaticallyAssignedPayloadTypeWithDifferentCasing.
  const std::string head = "v=0\r\no=- 11 22 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n";
  ExpectParseFailure(head + "m=audio 49232 RTP/AVP 108\r\na=rtpmap:108 ISAC/16000/512\r\n",
                     "a=rtpmap:108 ISAC/16000/512");
  ExpectParseFailure(head + "m=audio 49232 RTP/AVP 108\r\na=rtpmap:108 ISAC/16000\r\na=rtpmap:108 G711/16000\r\n",
                     "a=rtpmap:108 G711/16000");
  ExpectParseFailure(head + "m=video 49232 RTP/AVP 108\r\na=rtpmap:108 VP8/90000\r\na=rtpmap:108 VP9/90000\r\n",
                     "a=rtpmap:108 VP9/90000");
  SM_EXPECT_TRUE(Parse(head + "m=video 49232 RTP/AVP 108\r\na=fmtp:108 profile-level=1\r\na=rtpmap:108 VP9/90000\r\n") !=
                 nullptr);
  SM_EXPECT_TRUE(Parse(head + "m=audio 49232 RTP/AVP 18\r\na=rtpmap:18 g729/8000\r\n") != nullptr);
  // Payload types out of 0-127 in the m= line fail.
  SM_EXPECT_TRUE(Parse(head + "m=audio 49232 RTP/AVP 128\r\n") == nullptr);
}

SM_TEST(PcWebRtcSdp, BackfillsDefaultFmtpValues) {
  std::string sdp = std::string("v=0\r\no=- 0 3 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n") +
                    "a=group:BUNDLE 0\r\n"
                    "a=fingerprint:sha-1 4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"
                    "a=setup:actpass\r\n"
                    "a=ice-ufrag:ETEn\r\n"
                    "a=ice-pwd:OtSK0WpNtpUjkY4+86js7Z/l\r\n"
                    "m=video 9 UDP/TLS/RTP/SAVPF 96 97 98 99\r\n"
                    "c=IN IP4 0.0.0.0\r\n"
                    "a=rtcp-mux\r\n"
                    "a=sendonly\r\n"
                    "a=mid:0\r\n"
                    "a=rtpmap:96 H264/90000\r\n"
                    "a=rtpmap:97 VP9/90000\r\n"
                    "a=rtpmap:98 AV1/90000\r\n"
                    "a=rtpmap:99 H265/90000\r\n"
                    "a=ssrc:1234 cname:test\r\n";
  auto jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  const std::vector<Codec>& codecs = Media(*jdesc, 0)->codecs();
  SM_ASSERT_EQ(codecs.size(), 4u);
  std::string value;
  SM_EXPECT_TRUE(codecs[0].GetParam("packetization-mode", &value));
  SM_EXPECT_EQ(value, std::string("0"));
  SM_EXPECT_TRUE(codecs[1].GetParam("profile-id", &value));
  SM_EXPECT_EQ(value, std::string("0"));
  SM_EXPECT_TRUE(codecs[2].GetParam("level-idx", &value));
  SM_EXPECT_EQ(value, std::string("5"));
  SM_EXPECT_TRUE(codecs[2].GetParam("tier", &value));
  SM_EXPECT_EQ(value, std::string("0"));
  SM_EXPECT_TRUE(codecs[3].GetParam("level-id", &value));
  SM_EXPECT_EQ(value, std::string("93"));
  SM_EXPECT_TRUE(codecs[3].GetParam("tx-mode", &value));
  SM_EXPECT_EQ(value, std::string("SRST"));
}

SM_TEST(PcWebRtcSdp, DeserializeSdpWithReorderedPltypes) {
  std::string sdp = std::string(kSdpSessionPrefix) +
                    "m=audio 9 RTP/SAVPF 104 103\r\n"
                    "a=rtpmap:103 ISAC/16000\r\n"
                    "a=rtpmap:104 ISAC/32000\r\n";
  auto jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  // The codecs are in the m= line's order.
  SM_EXPECT_EQ(Media(*jdesc, 0)->codecs()[0].id, 104);
  SM_EXPECT_EQ(Media(*jdesc, 0)->codecs()[1].id, 103);
}

SM_TEST(PcWebRtcSdp, SctpDataChannels) {
  // DeserializeSdpWithSctpDataChannels / WithSctpPort / WithSctpColonPort / WithSctpInit / WithMaxMessageSize /
  // DeserializeSdpWithMultiSctpPort / SerializeSessionDescriptionWithSctpDataChannel.
  std::string sdp = std::string(kSdpSessionPrefix) + kSdpSctpDataChannelString;
  auto jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  const ContentInfo& data = jdesc->description()->contents()[0];
  SM_EXPECT_TRUE(data.type == MediaProtocolType::kSctp);
  const SctpDataContentDescription* sctp = data.media_description()->as_sctp();
  SM_ASSERT_TRUE(sctp != nullptr);
  SM_EXPECT_TRUE(sctp->use_sctpmap());
  SM_EXPECT_EQ(sctp->port(), 5000);
  SM_EXPECT_EQ(sctp->max_message_size(), 65536);
  SM_EXPECT_EQ(sctp->protocol(), std::string("UDP/DTLS/SCTP"));
  SM_EXPECT_EQ(SdpSerialize(*jdesc), sdp);

  for (const char* section : {kSdpSctpDataChannelStringWithSctpPort, kSdpSctpDataChannelStringWithSctpColonPort}) {
    jdesc = Parse(std::string(kSdpSessionPrefix) + section);
    SM_ASSERT_TRUE(jdesc != nullptr);
    sctp = Media(*jdesc, 0)->as_sctp();
    SM_EXPECT_FALSE(sctp->use_sctpmap());
    SM_EXPECT_EQ(sctp->port(), 5000);
  }

  std::string with_port = std::string(kSdpSessionPrefix) + kSdpSctpDataChannelStringWithSctpColonPort;
  Replace("a=sctp-port:5000", "a=sctp-port:34567\r\na=max-message-size:12345", &with_port);
  jdesc = Parse(with_port);
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_EXPECT_EQ(Media(*jdesc, 0)->as_sctp()->port(), 34567);
  SM_EXPECT_EQ(Media(*jdesc, 0)->as_sctp()->max_message_size(), 12345);
  // Written as draft-26: a=sctp-port, and max-message-size when not 64 KiB.
  const std::string out = SdpSerialize(*jdesc);
  SM_EXPECT_NE(out.find("m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"), std::string::npos);
  SM_EXPECT_NE(out.find("a=mid:data_content_name\r\na=sctp-port:34567\r\na=max-message-size:12345\r\n"),
               std::string::npos);

  jdesc = Parse(std::string(kSdpSessionPrefix) + kSdpSctpDataChannelStringWithSctpInit);
  SM_ASSERT_TRUE(jdesc != nullptr);
  const auto& cookie = Media(*jdesc, 0)->as_sctp()->sctp_init();
  SM_ASSERT_TRUE(cookie.has_value());
  SM_EXPECT_EQ(std::string(cookie->begin(), cookie->end()), std::string("CookieMonster"));
  SM_EXPECT_NE(SdpSerialize(*jdesc).find("a=sctp-init:Q29va2llTW9uc3Rlcg==\r\n"), std::string::npos);

  // sctp-port with the sctpmap form fails; bad ports fail.
  std::string multi = sdp;
  InjectAfter("a=sctpmap:5000 webrtc-datachannel 65535\r\n", "a=sctp-port:5000\r\n", &multi);
  ExpectParseFailure(multi, "a=sctp-port:5000");
  for (const char* bad : {"a=sctp-port:-1", "a=sctp-port:65536", "a=sctp-port:0x1234"}) {
    std::string s = std::string(kSdpSessionPrefix) + kSdpSctpDataChannelStringWithSctpColonPort;
    Replace("a=sctp-port:5000", bad, &s);
    ExpectParseFailure(s, bad);
  }
  std::string bad_cookie = std::string(kSdpSessionPrefix) + kSdpSctpDataChannelStringWithSctpInit;
  Replace("Q29va2llTW9uc3Rlcg==", "Q29va2llTW9uc3Rlcg=", &bad_cookie);
  ExpectParseFailure(bad_cookie, "a=sctp-init:Q29va2llTW9uc3Rlcg=");
}

SM_TEST(PcWebRtcSdp, DeserializeWithAllSctpProtocols) {
  for (const char* proto : {"DTLS/SCTP", "UDP/DTLS/SCTP", "TCP/DTLS/SCTP"}) {
    std::string sdp = std::string(kSdpSessionPrefix) + kSdpSctpDataChannelStringWithSctpColonPort;
    Replace("UDP/DTLS/SCTP", proto, &sdp);
    auto jdesc = Parse(sdp);
    SM_ASSERT_TRUE(jdesc != nullptr);
    SM_EXPECT_EQ(Media(*jdesc, 0)->protocol(), std::string(proto));
  }
  // SctpProtocolWithNonApplication / MediaTypeProtocolMismatch.
  std::string audio_sctp = std::string(kSdpSessionPrefix) + "m=audio 9 UDP/DTLS/SCTP 111\r\n";
  ExpectParseFailure(audio_sctp, "m=audio 9 UDP/DTLS/SCTP 111");
}

SM_TEST(PcWebRtcSdp, DeserializeSdpWithUnsupportedMediaType) {
  std::string sdp = kSdpString;
  sdp +=
      "m=bogus 9 RTP/SAVPF 0 8\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=mid:bogusmid\r\n"
      "m=audio/something 9 RTP/SAVPF 0 8\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=mid:somethingmid\r\n";
  auto jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_ASSERT_EQ(jdesc->description()->contents().size(), 4u);
  const ContentInfo& bogus = jdesc->description()->contents()[2];
  SM_EXPECT_TRUE(bogus.rejected);
  SM_ASSERT_TRUE(bogus.media_description()->as_unsupported() != nullptr);
  SM_EXPECT_EQ(bogus.media_description()->as_unsupported()->media_type(), std::string("bogus"));
  // Written back, rejected.
  SM_EXPECT_NE(SdpSerialize(*jdesc).find("m=bogus 0 RTP/SAVPF 0\r\n"), std::string::npos);
}

SM_TEST(PcWebRtcSdp, IllegalMidAndNoMid) {
  std::string sdp = kSdpString;
  Replace("a=mid:", "a=mid:[]", &sdp);
  ExpectParseFailure(sdp, "a=mid:[]");
  // ParseNoMid: without a=mid the content name is empty.
  sdp = kSdpString;
  Replace("a=mid:audio_content_name\r\n", "", &sdp);
  Replace("a=mid:video_content_name\r\n", "", &sdp);
  auto jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_EXPECT_EQ(jdesc->description()->contents()[0].mid(), std::string());
  SM_EXPECT_EQ(jdesc->description()->contents()[1].mid(), std::string());
}

SM_TEST(PcWebRtcSdp, SessionLevelLines) {
  // ExpectsTLineBeforeAttributeLine / IgnoresUnknownAttributeLines / DeserializeEmptySessionName /
  // AttributeWithPartialMatchingNameIsIgnored.
  SM_EXPECT_TRUE(Parse("v=0\r\no=- 0 3 IN IP4 127.0.0.1\r\ns=-\r\na=thisisnottherightplace\r\nt=0 0\r\n") == nullptr);
  SM_EXPECT_TRUE(Parse("v=0\r\no=- 0 3 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\na=thisisanunknownattribute\r\n") != nullptr);
  std::string sdp = kSdpString;
  Replace("s=-\r\n", "s= \r\n", &sdp);
  SM_EXPECT_TRUE(Parse(sdp) != nullptr);
  sdp = kSdpString;
  InjectAfter("a=mid:audio_content_name\r\n", "a=rtcp-muxer\r\n", &sdp);
  Replace("a=rtcp-mux\r\n", "", &sdp);
  auto jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_EXPECT_FALSE(Media(*jdesc, 0)->rtcp_mux());
  // The o= line needs six fields.
  sdp = kSdpString;
  Replace("o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1", "o=- 1 IN IP4 127.0.0.1", &sdp);
  ExpectParseFailure(sdp, "o=- 1 IN IP4 127.0.0.1");
}

SM_TEST(PcWebRtcSdp, ParseConnectionData) {
  // ParseConnectionData / SessionLevelOnly / IPv6 / Failure / SerializeAndDeserializeWithConnectionAddress.
  std::string sdp = kSdpString;
  Replace("m=audio 9 RTP/SAVPF", "m=audio 9999 RTP/SAVPF", &sdp);
  Replace("c=IN IP4 0.0.0.0\r\na=rtcp:9 IN IP4 0.0.0.0\r\na=ice-ufrag:ufrag_voice",
          "c=IN IP4 192.168.0.3\r\na=rtcp:9 IN IP4 0.0.0.0\r\na=ice-ufrag:ufrag_voice", &sdp);
  Replace("c=IN IP4 0.0.0.0\r\na=rtcp:9 IN IP4 0.0.0.0\r\na=ice-ufrag:ufrag_video",
          "c=IN IP6 2001:db8:85a3::8a2e:370:7335\r\na=rtcp:9 IN IP4 0.0.0.0\r\na=ice-ufrag:ufrag_video", &sdp);
  auto jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_EXPECT_EQ(Media(*jdesc, 0)->connection_address().ToString(), std::string("192.168.0.3:9999"));
  SM_EXPECT_EQ(Media(*jdesc, 1)->connection_address().ToString(), std::string("[2001:db8:85a3::8a2e:370:7335]:9"));
  const std::string out = SdpSerialize(*jdesc);
  SM_EXPECT_NE(out.find("m=audio 9999 RTP/SAVPF 111 103 104\r\nc=IN IP4 192.168.0.3\r\n"), std::string::npos);
  SM_EXPECT_NE(out.find("c=IN IP6 2001:db8:85a3::8a2e:370:7335\r\n"), std::string::npos);

  // Session-level only: used by every m= section without its own c= line.
  sdp = kSdpString;
  InjectAfter("s=-\r\n", "c=IN IP4 192.168.0.3\r\n", &sdp);
  Replace("c=IN IP4 0.0.0.0\r\n", "", &sdp);
  Replace("c=IN IP4 0.0.0.0\r\n", "", &sdp);
  jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_EXPECT_EQ(Media(*jdesc, 0)->connection_address().ToString(), std::string("192.168.0.3:9"));
  SM_EXPECT_EQ(Media(*jdesc, 1)->connection_address().ToString(), std::string("192.168.0.3:9"));

  for (const char* bad : {"c=IN IP4 74.125.224.39/127\r\n", "c=IN IP6 ::1/3\r\n", "c=IN IP6 74.125.224.39\r\n",
                          "c=IN IP4 2001:0db8:85a3:0000:0000:8a2e:0370:7334\r\n", "c=OUT IP4 74.125.224.39\r\n"}) {
    sdp = kSdpFullString;
    Replace("c=IN IP4 74.125.224.39\r\n", bad, &sdp);
    SM_EXPECT_TRUE(Parse(sdp) == nullptr);
  }
}

SM_TEST(PcWebRtcSdp, CandidatesInSdp) {
  // DeserializeCandidateWithDifferentTransport / IceCredentialsInCandidateStringIgnored /
  // DeserializeInvalidPortInCandidateAttribute / ParseTcpCandidateWithoutTcptype / ParseSslTcpCandidate.
  std::string sdp = kSdpString;
  InjectAfter("a=rtcp:9 IN IP4 0.0.0.0\r\n",
              "a=candidate:a0+B/3 1 udp 2130706432 74.125.127.126 2345 typ srflx raddr 192.168.1.5 rport 2346 "
              "generation 2 ufrag fake_ufrag pwd fake_pwd\r\n"
              "a=candidate:a0+B/4 1 tcp 2130706432 192.168.1.5 9 typ host\r\n"
              "a=candidate:a0+B/5 1 ssltcp 2130706432 192.168.1.5 443 typ host tcptype passive\r\n"
              "a=candidate:a0+B/6 1 udp 2130706432 1f2e3d4c-5b6a-7980-1a2b-3c4d5e6f7a8b.local 54321 typ host "
              "generation 0\r\n",
              &sdp);
  auto jdesc = Parse(sdp);
  SM_ASSERT_TRUE(jdesc != nullptr);
  // The hostname (mDNS) candidate is dropped (see sm/pc/webrtc_sdp.h).
  SM_ASSERT_EQ(jdesc->candidates(0)->count(), 3u);
  const Candidate& srflx = jdesc->candidates(0)->at(0)->candidate();
  // The a=candidate ufrag and pwd are replaced by the m= section's.
  SM_EXPECT_EQ(srflx.username(), std::string("ufrag_voice"));
  SM_EXPECT_EQ(srflx.password(), std::string("pwd_voice"));
  const Candidate& tcp = jdesc->candidates(0)->at(1)->candidate();
  SM_EXPECT_EQ(tcp.protocol(), std::string("tcp"));
  SM_EXPECT_EQ(tcp.tcptype(), std::string("passive"));
  SM_EXPECT_EQ(jdesc->candidates(0)->at(2)->candidate().protocol(), std::string("ssltcp"));
  // The c= line comes from the best UDP RTP candidate.
  SM_EXPECT_EQ(Media(*jdesc, 0)->connection_address().ToString(), std::string("74.125.127.126:2345"));

  sdp = kSdpString;
  InjectAfter("a=rtcp:9 IN IP4 0.0.0.0\r\n", "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 70000 typ host\r\n", &sdp);
  ExpectParseFailure(sdp, "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 70000 typ host");
  sdp = kSdpString;
  InjectAfter("a=rtcp:9 IN IP4 0.0.0.0\r\n", "a=candidate:a0+B/1 1 sctp 2130706432 192.168.1.5 1234 typ host\r\n",
              &sdp);
  ExpectParseFailure(sdp, "a=candidate:a0+B/1 1 sctp");
}

SM_TEST(PcCandidateParse, CandidateStrings) {
  // DeserializeCandidate / DeserializeRawCandidateAttribute / DeserializeInvalidCandidiate / the TCP cases.
  std::string error;
  auto c = Candidate::ParseCandidateString(
      "candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host generation 2 network-id 3 network-cost 50", &error);
  SM_ASSERT_TRUE(c.has_value());
  SM_EXPECT_EQ(c->foundation(), std::string("a0+B/1"));
  SM_EXPECT_EQ(c->component(), 1);
  SM_EXPECT_EQ(c->priority(), 2130706432u);
  SM_EXPECT_EQ(c->generation(), 2u);
  SM_EXPECT_EQ(c->network_id(), 3);
  SM_EXPECT_EQ(c->network_cost(), 50);
  SM_EXPECT_EQ(c->ToCandidateAttribute(false),
               std::string("candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host generation 2 network-id 3 "
                           "network-cost 50"));
  // With "a=", with "\r\n", and an upper-case transport.
  SM_EXPECT_TRUE(Candidate::ParseCandidateString("a=candidate:x 1 UDP 1 ::1 9 typ relay\r\n").has_value());
  // A TCP candidate without tcptype is passive; tcptype on UDP fails.
  c = Candidate::ParseCandidateString("candidate:x 1 tcp 1 192.168.1.5 9 typ host");
  SM_ASSERT_TRUE(c.has_value());
  SM_EXPECT_EQ(c->tcptype(), std::string("passive"));
  SM_EXPECT_FALSE(Candidate::ParseCandidateString("candidate:x 1 udp 1 192.168.1.5 9 typ host tcptype active", &error));
  SM_EXPECT_EQ(error, std::string("Invalid non-TCP candidate"));
  SM_EXPECT_FALSE(Candidate::ParseCandidateString("candidate:x 1 tcp 1 192.168.1.5 9 typ host tcptype bogus", &error));
  SM_EXPECT_EQ(error, std::string("Invalid TCP candidate type"));
  // TLS needs the fingerprint extension.
  SM_EXPECT_FALSE(Candidate::ParseCandidateString("candidate:x 1 tls 1 192.168.1.5 443 typ relay", &error));
  SM_EXPECT_EQ(error, std::string("Missing fingerprint extension for TLS candidate"));
  SM_EXPECT_TRUE(Candidate::ParseCandidateString("candidate:x 1 tls 1 192.168.1.5 443 typ relay fingerprint "
                                                 "sha-256;6E:B5:4A:72")
                     .has_value());
  // Malformed ones.
  SM_EXPECT_FALSE(Candidate::ParseCandidateString("candidate:x 1 udp 1 192.168.1.5 9 typ", &error));
  SM_EXPECT_EQ(error, std::string("Expect at least 8 fields."));
  SM_EXPECT_FALSE(Candidate::ParseCandidateString("candidate:x 1 udp 1 192.168.1.5 9 typ bogus", &error));
  SM_EXPECT_EQ(error, std::string("Unsupported candidate type"));
  SM_EXPECT_FALSE(Candidate::ParseCandidateString("candidatex 1 udp 1 192.168.1.5 9 typ host", &error));
  SM_EXPECT_FALSE(Candidate::ParseCandidateString("candidate:x 1 udp 1 192.168.1.5 9 typ host\r\na=x", &error));
  SM_EXPECT_EQ(error, std::string("Expect one line only"));
  SM_EXPECT_FALSE(Candidate::ParseCandidateString("candidate:x 1 udp 1 host.local 9 typ host", &error));
  SM_EXPECT_EQ(error, std::string(p2p::kUnsupportedHostnameCandidate));
}

// a=msid (RFC 8830) and Plan B a=ssrc msid: the DeserializeMsidAttribute* and *MsidSignaling cases.
std::string AudioOnly(const std::string& lines) {
  return std::string(kSdpSessionPrefix) + "m=audio 9 RTP/SAVPF 111\r\nc=IN IP4 0.0.0.0\r\na=rtpmap:111 opus/48000/2\r\n" +
         lines;
}

SM_TEST(PcWebRtcSdp, MsidAttribute) {
  auto jdesc = Parse(AudioOnly("a=msid:stream_id track_id\r\n"));
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_ASSERT_EQ(Media(*jdesc, 0)->streams().size(), 1u);
  SM_EXPECT_EQ(Media(*jdesc, 0)->streams()[0].id, std::string("track_id"));
  SM_EXPECT_EQ(Media(*jdesc, 0)->streams()[0].stream_ids().size(), 1u);
  SM_EXPECT_EQ(jdesc->description()->msid_signaling(), kMsidSignalingMediaSection);

  // "-" means no stream.
  jdesc = Parse(AudioOnly("a=msid:- track_id\r\n"));
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_EXPECT_EQ(Media(*jdesc, 0)->streams()[0].stream_ids().size(), 0u);

  // Without app data: a random 8-character track id.
  jdesc = Parse(AudioOnly("a=msid:stream_id\r\n"));
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_EXPECT_EQ(Media(*jdesc, 0)->streams()[0].id.size(), 8u);

  // Two streams, mixed with and without app data.
  jdesc = Parse(AudioOnly("a=msid:stream_id\r\na=msid:stream_id2 track_id\r\n"));
  SM_ASSERT_TRUE(jdesc != nullptr);
  const StreamParams& stream = Media(*jdesc, 0)->streams()[0];
  SM_ASSERT_EQ(stream.stream_ids().size(), 2u);
  SM_EXPECT_EQ(stream.stream_ids()[1], std::string("stream_id2"));
  SM_EXPECT_EQ(stream.id, std::string("track_id"));

  ExpectParseFailure(AudioOnly("a=msid:stream_id \r\n"), "a=msid:stream_id ");
  ExpectParseFailure(AudioOnly("a=msid: track_id\r\n"), "a=msid: track_id");
  SM_EXPECT_TRUE(Parse(AudioOnly("a=msid\r\n")) == nullptr);
  ExpectParseFailure(AudioOnly("a=msid:stream_id track_id bogus\r\n"), "a=msid:stream_id track_id bogus");
  ExpectParseFailure(AudioOnly("a=msid:stream_id track_id\r\na=msid:stream_id2 track_id2\r\n"),
                     "a=msid:stream_id2 track_id2");
  // The same msid in two m= sections fails.
  std::string twice = AudioOnly("a=msid:stream_id track_id\r\n") +
                      "m=audio 9 RTP/SAVPF 111\r\na=rtpmap:111 opus/48000/2\r\na=msid:stream_id track_id\r\n";
  SdpParseError error;
  SM_EXPECT_TRUE(Parse(twice, &error) == nullptr);
  SM_EXPECT_EQ(error.description, std::string("Duplicate a=msid lines detected"));
}

SM_TEST(PcWebRtcSdp, PlanBSsrcMsidAndCname) {
  // PlanBHasSsrcAttributeMsidSignaling / ParseIgnoreUnknownSsrcSpecificAttribute / DeserializeSessionDescriptionWithoutCname
  // / RejectDuplicateSsrcInSsrcGroup.
  auto jdesc = Parse(AudioOnly("a=ssrc:1 cname:cname_1\r\na=ssrc:1 msid:stream_1 track_1\r\na=ssrc:1 bogus:value\r\n"
                               "a=ssrc:2 msid:stream_2 track_2\r\n"));
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_EXPECT_EQ(jdesc->description()->msid_signaling(), kMsidSignalingSsrcAttribute);
  SM_ASSERT_EQ(Media(*jdesc, 0)->streams().size(), 2u);
  SM_EXPECT_EQ(Media(*jdesc, 0)->streams()[0].cname, std::string("cname_1"));
  SM_EXPECT_EQ(Media(*jdesc, 0)->streams()[1].cname, std::string());
  SM_EXPECT_EQ(Media(*jdesc, 0)->streams()[1].id, std::string("track_2"));
  const std::string out = SdpSerialize(*jdesc);
  SM_EXPECT_NE(out.find("a=ssrc:1 cname:cname_1\r\na=ssrc:1 msid:stream_1 track_1\r\n"), std::string::npos);

  // No msid signalling: the "default" stream.
  jdesc = Parse(AudioOnly("a=ssrc:1 cname:cname_1\r\n"));
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_EXPECT_EQ(std::string(Media(*jdesc, 0)->streams()[0].first_stream_id()), std::string("default"));
  SM_EXPECT_EQ(SdpSerialize(*jdesc).find("a=msid-semantic"), std::string::npos);

  ExpectParseFailure(AudioOnly("a=ssrc-group:FID 1 1\r\n"), "a=ssrc-group:FID 1 1");
  ExpectParseFailure(AudioOnly("a=ssrc:1 cname\r\n"), "a=ssrc:1 cname");
}

SM_TEST(PcWebRtcSdp, UnifiedPlanNoSsrcSignaling) {
  // DeserializeUnifiedPlanSessionDescriptionNoSsrcSignaling.
  auto jdesc = Parse(AudioOnly("a=msid:local_stream_1 audio_track_id_1\r\n"));
  SM_ASSERT_TRUE(jdesc != nullptr);
  SM_ASSERT_EQ(Media(*jdesc, 0)->streams().size(), 1u);
  SM_EXPECT_FALSE(Media(*jdesc, 0)->streams()[0].has_ssrcs());
  SM_EXPECT_NE(SdpSerialize(*jdesc).find("a=msid:local_stream_1 audio_track_id_1\r\n"), std::string::npos);
}

// Simulcast and RID (RFC 8851/8853): TestDeserializeSimulcastAttribute and the RID cases.
std::string VideoWith(const std::string& lines) {
  return std::string(kSdpSessionPrefix) +
         "m=video 9 UDP/TLS/RTP/SAVPF 96 97\r\nc=IN IP4 0.0.0.0\r\na=mid:0\r\na=sendrecv\r\n"
         "a=msid:stream track\r\na=rtpmap:96 VP8/90000\r\na=rtpmap:97 VP9/90000\r\n" +
         lines;
}

SM_TEST(PcSimulcastSdp, DeserializeSimulcastAttribute) {
  auto jdesc = Parse(VideoWith("a=rid:1 send\r\na=rid:2 send\r\na=rid:3 send\r\na=rid:4 recv\r\na=rid:5 recv\r\n"
                               "a=rid:6 recv\r\na=simulcast:send 1,2;3 recv 4;5;6\r\n"));
  SM_ASSERT_TRUE(jdesc != nullptr);
  const MediaContentDescription* media = Media(*jdesc, 0);
  SM_EXPECT_TRUE(media->HasSimulcast());
  SM_EXPECT_EQ(media->simulcast_description().send_layers().size(), 2u);
  SM_EXPECT_EQ(media->simulcast_description().receive_layers().size(), 3u);
  SM_ASSERT_FALSE(media->streams().empty());
  const std::vector<RidDescription>& rids = media->streams()[0].rids();
  SM_ASSERT_EQ(rids.size(), 3u);
  SM_EXPECT_EQ(rids[0].rid, std::string("1"));
  SM_EXPECT_EQ(rids[2].rid, std::string("3"));
  SM_EXPECT_EQ(media->receive_rids().size(), 3u);
  // Written back in libwebrtc's order: the track's send rids, the receive rids, then a=simulcast.
  const std::string out = SdpSerialize(*jdesc);
  SM_EXPECT_NE(out.find("a=rid:1 send\r\na=rid:2 send\r\na=rid:3 send\r\na=rid:4 recv\r\na=rid:5 recv\r\n"
                        "a=rid:6 recv\r\na=simulcast:send 1,2;3 recv 4;5;6\r\n"),
               std::string::npos);
}

SM_TEST(PcSimulcastSdp, RemovesUnknownAndInvalidRids) {
  // TestDeserializeSimulcastAttributeRemovesUnknownRids / IgnoresEmptyRidLines / IgnoresMalformedRidLines /
  // IgnoresDuplicateRidLines / IgnoresWrongRidDirectionLines.
  auto jdesc = Parse(VideoWith("a=rid:1 send\r\na=rid:\r\na=rid:2 bogus\r\na=rid:3 send\r\na=rid:3 send\r\n"
                               "a=rid:4 recv\r\na=simulcast:send 1;2;3;~4;5\r\n"));
  SM_ASSERT_TRUE(jdesc != nullptr);
  const MediaContentDescription* media = Media(*jdesc, 0);
  // Only rid 1 remains: 2 is malformed, 3 is duplicated, 4 is a receive rid, 5 is unknown.
  const std::vector<SimulcastLayer> layers = media->simulcast_description().send_layers().GetAllLayers();
  SM_ASSERT_EQ(layers.size(), 1u);
  SM_EXPECT_EQ(layers[0].rid, std::string("1"));
  SM_ASSERT_EQ(media->streams()[0].rids().size(), 1u);
}

SM_TEST(PcSimulcastSdp, RidPayloadTypesAndRestrictions) {
  // TestDeserializeRidSendDirection / IgnoresInvalidPayloadTypesInRid / SerializeSimulcast_ComplexSerialization.
  auto jdesc = Parse(VideoWith("a=rid:1 send pt=96,97;max-width=1280;max-height=720\r\na=rid:2 send pt=98\r\n"
                               "a=rid:3 send max-fps\r\na=simulcast:send 1;2;~3\r\n"));
  SM_ASSERT_TRUE(jdesc != nullptr);
  const std::vector<RidDescription>& rids = Media(*jdesc, 0)->streams()[0].rids();
  SM_ASSERT_EQ(rids.size(), 3u);
  SM_EXPECT_EQ(rids[0].codecs.size(), 2u);
  SM_EXPECT_EQ(rids[0].restrictions.at("max-width"), std::string("1280"));
  // pt=98 is not in the m= section: the RID stays, without codecs.
  SM_EXPECT_EQ(rids[1].codecs.size(), 0u);
  SM_EXPECT_EQ(rids[2].restrictions.at("max-fps"), std::string());
  SM_EXPECT_TRUE(Media(*jdesc, 0)->simulcast_description().send_layers()[2][0].is_paused);
  const std::string out = SdpSerialize(*jdesc);
  SM_EXPECT_NE(out.find("a=rid:1 send pt=96,97;max-height=720;max-width=1280\r\na=rid:2 send\r\na=rid:3 send max-fps\r\n"
                        "a=simulcast:send 1;2;~3\r\n"),
               std::string::npos);
}

SM_TEST(PcSimulcastSdp, NegativeSimulcastAttributes) {
  // DeserializeSimulcastNegative_EmptyAttribute / DuplicateAttribute, and the serializer's errors.
  ExpectParseFailure(VideoWith("a=rid:1 send\r\na=simulcast:\r\n"), "a=simulcast:");
  ExpectParseFailure(VideoWith("a=rid:1 send\r\na=simulcast:send 1\r\na=simulcast:send 1\r\n"), "a=simulcast:send 1");
  ExpectParseFailure(VideoWith("a=simulcast:send 1 send 2\r\n"), "a=simulcast:send 1 send 2");
  SimulcastSdpSerializer serializer;
  std::string error;
  SM_EXPECT_FALSE(serializer.DeserializeSimulcastDescription("send 1;;2", &error).has_value());
  SM_EXPECT_EQ(error, std::string("Simulcast alternative layer list is empty."));
  SM_EXPECT_FALSE(serializer.DeserializeSimulcastDescription("send ~", &error).has_value());
  SM_EXPECT_EQ(error, std::string("Rid must not be empty."));
  SM_EXPECT_FALSE(serializer.DeserializeSimulcastDescription("send 1;2;3;4;5;6;7;8;9;10;11;12;13;14;15;16;17", &error)
                      .has_value());
  SM_EXPECT_EQ(error, std::string("Simulcast description contains too many RIDs."));
  VideoContentDescription media;
  SM_EXPECT_FALSE(serializer.DeserializeRidDescription(media, "toolongridvalue123 send", &error).has_value());
  SM_EXPECT_FALSE(serializer.DeserializeRidDescription(media, "1 send max-width=1;pt=96", &error).has_value());
  SM_EXPECT_EQ(error, std::string("Payload list must appear first in the restrictions."));
}

// A Chrome-style offer and answer (as Chrome's libwebrtc writes them: unified plan, BUNDLE, trickle ICE, DTLS
// actpass/active, RTX with FID groups, Opus with RED, a data channel), which must be read and written back byte for
// byte.
constexpr char kChromeOffer[] =
    "v=0\r\n"
    "o=- 4611731400430051336 2 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=group:BUNDLE 0 1 2\r\n"
    "a=extmap-allow-mixed\r\n"
    "a=msid-semantic: WMS 6f8a3e0b-6d3b-4c2a-9a7e-3d1b2c4f5e6a\r\n"
    "m=audio 9 UDP/TLS/RTP/SAVPF 111 63 9 0 8 13 110 126\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:Zf3q\r\n"
    "a=ice-pwd:xJ2qL7vOq0hN3kQ5z9Ytd1Rk\r\n"
    "a=ice-options:trickle\r\n"
    "a=fingerprint:sha-256 "
    "6E:B5:4A:72:E6:63:65:40:A2:2A:32:BE:6D:6D:D2:B8:21:B5:F7:E8:66:2B:4D:55:B0:A6:5D:BF:8E:9F:24:C5\r\n"
    "a=setup:actpass\r\n"
    "a=mid:0\r\n"
    "a=extmap:1 urn:ietf:params:rtp-hdrext:ssrc-audio-level\r\n"
    "a=extmap:2 http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time\r\n"
    "a=extmap:3 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01\r\n"
    "a=extmap:4 urn:ietf:params:rtp-hdrext:sdes:mid\r\n"
    "a=sendrecv\r\n"
    "a=msid:6f8a3e0b-6d3b-4c2a-9a7e-3d1b2c4f5e6a 9b1d2e3f-4a5b-6c7d-8e9f-0a1b2c3d4e5f\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=rtcp-fb:111 transport-cc\r\n"
    "a=fmtp:111 minptime=10;useinbandfec=1\r\n"
    "a=rtpmap:63 red/48000/2\r\n"
    "a=fmtp:63 111/111\r\n"
    "a=rtpmap:9 G722/8000\r\n"
    "a=rtpmap:0 PCMU/8000\r\n"
    "a=rtpmap:8 PCMA/8000\r\n"
    "a=rtpmap:13 CN/8000\r\n"
    "a=rtpmap:110 telephone-event/48000\r\n"
    "a=rtpmap:126 telephone-event/8000\r\n"
    "a=ssrc:3735928559 cname:4TOk42mSjXCkVIa6\r\n"
    "a=ssrc:3735928559 msid:6f8a3e0b-6d3b-4c2a-9a7e-3d1b2c4f5e6a 9b1d2e3f-4a5b-6c7d-8e9f-0a1b2c3d4e5f\r\n"
    "m=video 9 UDP/TLS/RTP/SAVPF 96 97 102 103 114 116\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:Zf3q\r\n"
    "a=ice-pwd:xJ2qL7vOq0hN3kQ5z9Ytd1Rk\r\n"
    "a=ice-options:trickle\r\n"
    "a=fingerprint:sha-256 "
    "6E:B5:4A:72:E6:63:65:40:A2:2A:32:BE:6D:6D:D2:B8:21:B5:F7:E8:66:2B:4D:55:B0:A6:5D:BF:8E:9F:24:C5\r\n"
    "a=setup:actpass\r\n"
    "a=mid:1\r\n"
    "a=extmap:14 urn:ietf:params:rtp-hdrext:toffset\r\n"
    "a=extmap:2 http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time\r\n"
    "a=extmap:13 urn:3gpp:video-orientation\r\n"
    "a=extmap:3 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01\r\n"
    "a=extmap:4 urn:ietf:params:rtp-hdrext:sdes:mid\r\n"
    "a=extmap:10 urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id\r\n"
    "a=extmap:11 urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id\r\n"
    "a=sendrecv\r\n"
    "a=msid:6f8a3e0b-6d3b-4c2a-9a7e-3d1b2c4f5e6a 0c1d2e3f-4a5b-6c7d-8e9f-a0b1c2d3e4f5\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=rtpmap:96 VP8/90000\r\n"
    "a=rtcp-fb:96 goog-remb\r\n"
    "a=rtcp-fb:96 transport-cc\r\n"
    "a=rtcp-fb:96 ccm fir\r\n"
    "a=rtcp-fb:96 nack\r\n"
    "a=rtcp-fb:96 nack pli\r\n"
    "a=rtpmap:97 rtx/90000\r\n"
    "a=fmtp:97 apt=96\r\n"
    "a=rtpmap:102 H264/90000\r\n"
    "a=rtcp-fb:102 goog-remb\r\n"
    "a=rtcp-fb:102 transport-cc\r\n"
    "a=rtcp-fb:102 ccm fir\r\n"
    "a=rtcp-fb:102 nack\r\n"
    "a=rtcp-fb:102 nack pli\r\n"
    "a=fmtp:102 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f\r\n"
    "a=rtpmap:103 rtx/90000\r\n"
    "a=fmtp:103 apt=102\r\n"
    "a=rtpmap:114 red/90000\r\n"
    "a=rtpmap:116 ulpfec/90000\r\n"
    "a=ssrc-group:FID 1985229328 2596069104\r\n"
    "a=ssrc:1985229328 cname:4TOk42mSjXCkVIa6\r\n"
    "a=ssrc:1985229328 msid:6f8a3e0b-6d3b-4c2a-9a7e-3d1b2c4f5e6a 0c1d2e3f-4a5b-6c7d-8e9f-a0b1c2d3e4f5\r\n"
    "a=ssrc:2596069104 cname:4TOk42mSjXCkVIa6\r\n"
    "a=ssrc:2596069104 msid:6f8a3e0b-6d3b-4c2a-9a7e-3d1b2c4f5e6a 0c1d2e3f-4a5b-6c7d-8e9f-a0b1c2d3e4f5\r\n"
    "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:Zf3q\r\n"
    "a=ice-pwd:xJ2qL7vOq0hN3kQ5z9Ytd1Rk\r\n"
    "a=ice-options:trickle\r\n"
    "a=fingerprint:sha-256 "
    "6E:B5:4A:72:E6:63:65:40:A2:2A:32:BE:6D:6D:D2:B8:21:B5:F7:E8:66:2B:4D:55:B0:A6:5D:BF:8E:9F:24:C5\r\n"
    "a=setup:actpass\r\n"
    "a=mid:2\r\n"
    "a=sctp-port:5000\r\n"
    "a=max-message-size:262144\r\n";

constexpr char kChromeAnswer[] =
    "v=0\r\n"
    "o=- 8053960913213574937 2 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=group:BUNDLE 0 1 2\r\n"
    "a=extmap-allow-mixed\r\n"
    "a=msid-semantic: WMS\r\n"
    "m=audio 50123 UDP/TLS/RTP/SAVPF 111 63\r\n"
    "c=IN IP4 203.0.113.7\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=candidate:3496153426 1 udp 2122260223 192.168.1.20 50123 typ host generation 0 network-id 1 network-cost 10\r\n"
    "a=candidate:842163049 1 udp 1686052607 203.0.113.7 50123 typ srflx raddr 192.168.1.20 rport 50123 generation 0 "
    "network-id 1 network-cost 10\r\n"
    "a=candidate:2297153427 1 tcp 1518280447 192.168.1.20 9 typ host tcptype active generation 0 network-id 1 "
    "network-cost 10\r\n"
    "a=ice-ufrag:Rv9K\r\n"
    "a=ice-pwd:Yb7nQ2wE5rT8uI1oP4aS6dF3\r\n"
    "a=ice-options:trickle\r\n"
    "a=fingerprint:sha-256 "
    "1A:2B:3C:4D:5E:6F:70:81:92:A3:B4:C5:D6:E7:F8:09:1A:2B:3C:4D:5E:6F:70:81:92:A3:B4:C5:D6:E7:F8:09\r\n"
    "a=setup:active\r\n"
    "a=mid:0\r\n"
    "a=extmap:1 urn:ietf:params:rtp-hdrext:ssrc-audio-level\r\n"
    "a=extmap:3 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01\r\n"
    "a=extmap:4 urn:ietf:params:rtp-hdrext:sdes:mid\r\n"
    "a=recvonly\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=rtcp-fb:111 transport-cc\r\n"
    "a=fmtp:111 minptime=10;useinbandfec=1\r\n"
    "a=rtpmap:63 red/48000/2\r\n"
    "a=fmtp:63 111/111\r\n"
    "m=video 0 UDP/TLS/RTP/SAVPF 96\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:Rv9K\r\n"
    "a=ice-pwd:Yb7nQ2wE5rT8uI1oP4aS6dF3\r\n"
    "a=ice-options:trickle\r\n"
    "a=fingerprint:sha-256 "
    "1A:2B:3C:4D:5E:6F:70:81:92:A3:B4:C5:D6:E7:F8:09:1A:2B:3C:4D:5E:6F:70:81:92:A3:B4:C5:D6:E7:F8:09\r\n"
    "a=setup:active\r\n"
    "a=mid:1\r\n"
    "a=inactive\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=rtpmap:96 VP8/90000\r\n"
    "m=application 50123 UDP/DTLS/SCTP webrtc-datachannel\r\n"
    "c=IN IP4 192.168.1.20\r\n"
    "a=candidate:3496153426 1 udp 2122260223 192.168.1.20 50123 typ host generation 0 network-id 1 network-cost 10\r\n"
    "a=ice-ufrag:Rv9K\r\n"
    "a=ice-pwd:Yb7nQ2wE5rT8uI1oP4aS6dF3\r\n"
    "a=ice-options:trickle\r\n"
    "a=fingerprint:sha-256 "
    "1A:2B:3C:4D:5E:6F:70:81:92:A3:B4:C5:D6:E7:F8:09:1A:2B:3C:4D:5E:6F:70:81:92:A3:B4:C5:D6:E7:F8:09\r\n"
    "a=setup:active\r\n"
    "a=mid:2\r\n"
    "a=sctp-port:5000\r\n"
    "a=max-message-size:262144\r\n";

SM_TEST(PcWebRtcSdp, ChromeOfferRoundTrip) {
  SdpParseError error;
  auto offer = SdpDeserialize(SdpType::kOffer, kChromeOffer, &error);
  SM_ASSERT_TRUE(offer != nullptr);
  SM_EXPECT_TRUE(offer->GetType() == SdpType::kOffer);
  const SessionDescription* desc = offer->description();
  SM_ASSERT_EQ(desc->contents().size(), 3u);
  SM_EXPECT_EQ(desc->GetGroupByName(GROUP_TYPE_BUNDLE)->content_names().size(), 3u);
  SM_EXPECT_EQ(desc->msid_signaling(),
               kMsidSignalingMediaSection | kMsidSignalingSsrcAttribute | kMsidSignalingSemantic);
  // Audio: Opus first, RED's fmtp, the transport-cc feedback.
  const MediaContentDescription* audio = Media(*offer, 0);
  SM_ASSERT_EQ(audio->codecs().size(), 8u);
  SM_EXPECT_EQ(audio->codecs()[0].name, std::string("opus"));
  std::string value;
  SM_EXPECT_TRUE(audio->codecs()[1].GetParam("", &value));
  SM_EXPECT_EQ(value, std::string("111/111"));
  SM_EXPECT_TRUE(audio->preferred_rtcp_cc_ack_type() == RtcpFeedbackType::TRANSPORT_CC);
  SM_EXPECT_EQ(audio->rtp_header_extensions().size(), 4u);
  // Video: RTX with its FID group, one track.
  const MediaContentDescription* video = Media(*offer, 1);
  SM_ASSERT_EQ(video->streams().size(), 1u);
  uint32_t rtx_ssrc = 0;
  SM_EXPECT_TRUE(video->streams()[0].GetFidSsrc(1985229328u, &rtx_ssrc));
  SM_EXPECT_EQ(rtx_ssrc, 2596069104u);
  SM_EXPECT_EQ(video->codecs()[2].name, std::string("H264"));
  SM_EXPECT_TRUE(video->codecs()[2].GetParam("profile-level-id", &value));
  SM_EXPECT_EQ(value, std::string("42e01f"));
  // Data channel.
  const SctpDataContentDescription* data = Media(*offer, 2)->as_sctp();
  SM_ASSERT_TRUE(data != nullptr);
  SM_EXPECT_EQ(data->max_message_size(), 262144);
  // DTLS and ICE.
  const TransportDescription& td = desc->transport_infos()[0].description;
  SM_EXPECT_TRUE(td.connection_role == p2p::ConnectionRole::kActpass);
  SM_EXPECT_TRUE(td.HasOption(p2p::ICE_OPTION_TRICKLE));
  SM_ASSERT_TRUE(td.identity_fingerprint != nullptr);
  SM_EXPECT_EQ(td.identity_fingerprint->digest.size(), 32u);

  SM_EXPECT_EQ(SdpSerialize(*offer), std::string(kChromeOffer));
}

SM_TEST(PcWebRtcSdp, ChromeAnswerRoundTrip) {
  SdpParseError error;
  auto answer = SdpDeserialize(SdpType::kAnswer, kChromeAnswer, &error);
  SM_ASSERT_TRUE(answer != nullptr);
  SM_EXPECT_TRUE(answer->GetType() == SdpType::kAnswer);
  const SessionDescription* desc = answer->description();
  SM_ASSERT_EQ(desc->contents().size(), 3u);
  SM_EXPECT_TRUE(desc->contents()[1].rejected);
  SM_EXPECT_TRUE(Media(*answer, 0)->direction() == RtpTransceiverDirection::kRecvOnly);
  SM_EXPECT_TRUE(desc->transport_infos()[0].description.connection_role == p2p::ConnectionRole::kActive);
  SM_ASSERT_EQ(answer->candidates(0)->count(), 3u);
  SM_EXPECT_EQ(answer->candidates(0)->at(2)->candidate().tcptype(), std::string("active"));
  SM_EXPECT_EQ(answer->candidates(0)->at(1)->candidate().network_cost(), 10);
  SM_EXPECT_EQ(Media(*answer, 0)->connection_address().ToString(), std::string("203.0.113.7:50123"));
  SM_EXPECT_EQ(answer->candidates(2)->count(), 1u);

  SM_EXPECT_EQ(SdpSerialize(*answer), std::string(kChromeAnswer));
}

}  // namespace
