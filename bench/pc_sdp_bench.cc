// Connectivity benchmarks for 1.10 (SDP parse/write) and 1.11 (offer/answer generation), in the p2p section. Built
// with sm::pc_sdp (SM_WITH_TLS: the DTLS fingerprint comes from sm::tls), so not in the Wasm build. Budgets are about
// 5-10x the Release time on an Apple M4.
#include <memory>
#include <string>
#include <vector>

#include "sm/p2p/stun_crypto_tls.h"
#include "sm/pc/codec_vendor.h"
#include "sm/pc/jsep.h"
#include "sm/pc/media_session.h"
#include "sm/pc/webrtc_sdp.h"
#include "sm/tls/ssl_identity.h"
#include "sm_bench.h"

using namespace sm;
using namespace sm::pc;

namespace {

// webrtc_sdp_test.cc's kChromeOffer: an offer as Chrome's libwebrtc writes it (Unified Plan, BUNDLE, trickle ICE,
// DTLS actpass, Opus with RED, VP8 and H.264 with RTX, a data channel).
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

}  // namespace

// ---------------------------------------------------------------------------------------------------- 1.10 SDP

// Parses Chrome's 3-section offer into the session description model.
SM_BENCH(p2p, Sdp_ParseChromeOffer) {
  int ok = 0;
  for (auto _ : st) {
    auto desc = SdpDeserialize(SdpType::kOffer, kChromeOffer);
    ok += desc != nullptr;
    sm::bench::DoNotOptimize(desc);
  }
  st.Require("parsed_fraction", static_cast<double>(ok) / static_cast<double>(st.iterations()), ">=", 1);
  // One parse of a 113-line, 3-m-line offer (25 us on an M4).
  st.BudgetNsPerItem(200000);
}

// Writes the parsed offer back to SDP text (byte for byte the input).
SM_BENCH(p2p, Sdp_WriteChromeOffer) {
  auto desc = SdpDeserialize(SdpType::kOffer, kChromeOffer);
  int same = 0;
  for (auto _ : st) {
    const std::string sdp = SdpSerialize(*desc);
    same += sdp == kChromeOffer;
  }
  st.Require("round_trip_fraction", static_cast<double>(same) / static_cast<double>(st.iterations()), ">=", 1);
  // One write of the 3-m-line offer (7.5 us on an M4).
  st.BudgetNsPerItem(50000);
}

// ---------------------------------------------------------------------------------------------- 1.11 offer/answer

namespace {

// One side of media_session_test.cc's fixture with the engines' default codecs (Opus; H.264 CB with RTX).
struct Side {
  explicit Side(const char* name) : cert(tls::SslIdentity::Create(name)) { tdf.set_certificate(cert); }
  void Reset() {
    ssrc_generator = std::make_unique<UniqueRandomIdGenerator>();
    f = std::make_unique<MediaSessionDescriptionFactory>(ssrc_generator.get(), &tdf, &vendor, &suggester);
  }
  std::shared_ptr<const tls::SslIdentity> cert;
  TransportDescriptionFactory tdf;
  SdpPayloadTypeSuggester suggester;
  CodecVendor vendor{DefaultAudioCodecs(), DefaultAudioCodecs(), DefaultVideoCodecs(), DefaultVideoCodecs()};
  std::unique_ptr<UniqueRandomIdGenerator> ssrc_generator;
  std::unique_ptr<MediaSessionDescriptionFactory> f;
};

// media_session_test.cc's DefaultSessionOptions(true): audio, video and data, BUNDLE, one sender per track.
MediaSessionOptions CallOptions() {
  MediaSessionOptions opts;
  opts.bundle_enabled = true;
  opts.media_description_options.emplace_back(MediaType::AUDIO, "0", RtpTransceiverDirection::kSendRecv, false);
  opts.media_description_options.emplace_back(MediaType::VIDEO, "1", RtpTransceiverDirection::kSendRecv, false);
  opts.media_description_options[0].header_extensions = DefaultAudioRtpHeaderExtensions();
  opts.media_description_options[1].header_extensions = DefaultVideoRtpHeaderExtensions();
  opts.media_description_options[0].AddAudioSender("audio_1", {"stream_1"});
  opts.media_description_options[1].AddVideoSender("video_1", {"stream_1"}, {}, SimulcastLayerList(), 1);
  opts.media_description_options.emplace_back(MediaType::DATA, "2", RtpTransceiverDirection::kSendRecv, false);
  return opts;
}

}  // namespace

// The offerer's factory makes an offer and the answerer's factory answers it: codec, header extension and transport
// negotiation, SSRCs, ICE credentials and the DTLS fingerprint.
SM_BENCH(p2p, OfferAnswer_CreateOfferAndAnswer) {
  p2p::InstallStunCryptoFromTls();
  Side offerer("offerer"), answerer("answerer");
  offerer.Reset();
  answerer.Reset();
  const MediaSessionOptions opts = CallOptions();
  int ok = 0;
  int64_t n = 0;
  for (auto _ : st) {
    // The SSRC generators remember every SSRC handed out; start them afresh now and then, outside the timing.
    if (++n % 1024 == 0) {
      st.PauseTiming();
      offerer.Reset();
      answerer.Reset();
      st.ResumeTiming();
    }
    auto offer = offerer.f->CreateOfferOrError(opts, nullptr);
    auto answer = offer ? answerer.f->CreateAnswerOrError(offer.get(), opts, nullptr) : nullptr;
    ok += answer != nullptr && answer->contents().size() == 3;
    sm::bench::DoNotOptimize(answer);
  }
  st.Require("answered_fraction", static_cast<double>(ok) / static_cast<double>(st.iterations()), ">=", 1);
  // One offer and one answer of audio, video and data (21 us on an M4).
  st.BudgetNsPerItem(150000);
}
