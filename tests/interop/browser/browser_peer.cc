// The native side of the browser interop call (C6 part 4, #1088): our C6 part 3 endpoint (pc::PeerEndpoint, #1064)
// over real OS sockets (apps/net/os_sockets.h, #1052), driven by run_browser_call.py.
//
//   sm_interop_browser_peer --role=offer|answer --ip=A.B.C.D [--no-data-channel]
//
// Signaling is line-based JSON on stdin (messages from Chrome) and stdout (messages to Chrome):
// {"type":"offer"|"answer","sdp":...}, {"type":"candidate","candidate":...,"sdpMid":...,"sdpMLineIndex":N} and
// {"type":"bye"}, which ends the call. The last stdout line is the verdict, {"type":"result","ok":...,"checks":{...},
// "stats":{...}}. Diagnostics go to stderr.
//
// The library has no audio or video encoder, so the media are made here: every 20 ms the 3-byte Opus silence frame
// (CELT fullband 20 ms; decoders play silence), and at 15 fps a 160x96 H.264 Constrained Baseline stream whose IDR
// frames carry an I_PCM test pattern (uncompressed macroblocks, a moving bar) once a second or on a keyframe
// request, and whose P frames skip every macroblock.
//
// Data channels (C7 part 5, #1091; built with SM_WITH_DATACHANNEL, unless --no-data-channel): as the page's peer, the
// offerer creates the channel "sm-interop" before its offer and the answerer takes Chrome's from the data channel
// callback; once it is open the peer sends "hello from sm" and checks that "hello from chrome" arrives.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "os_sockets.h"
#include "sm/core/units.h"
#include "sm/p2p/basic_port_allocator.h"
#include "sm/p2p/network_manager.h"
#include "sm/p2p/stun_crypto_tls.h"
#include "sm/pc/peer_endpoint.h"
#include "sm/pc/webrtc_sdp.h"
#if defined(SM_WITH_DATACHANNEL) && SM_WITH_DATACHANNEL
#include "sm/pc/sctp_data_channel.h"
#endif

using namespace sm;
using namespace sm::pc;

namespace {

Timestamp Now() {
  const auto d = std::chrono::steady_clock::now().time_since_epoch();
  return Timestamp::Micros(std::chrono::duration_cast<std::chrono::microseconds>(d).count() + 10'000'000);
}

// --- Minimal JSON: flat objects of strings, numbers, booleans and null --------------------------------------------

std::string JsonEscape(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out + "\"";
}

// Parses a flat JSON object; values are returned as their text (strings unescaped). Nested values are skipped.
std::map<std::string, std::string> JsonParseFlat(const std::string& s) {
  std::map<std::string, std::string> out;
  size_t i = 0;
  auto ws = [&] { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; };
  auto str = [&]() -> std::string {
    std::string v;
    ++i;  // Opening quote.
    while (i < s.size() && s[i] != '"') {
      if (s[i] == '\\' && i + 1 < s.size()) {
        const char e = s[++i];
        if (e == 'n') v += '\n';
        else if (e == 'r') v += '\r';
        else if (e == 't') v += '\t';
        else if (e == 'u' && i + 4 < s.size()) {
          const unsigned cp = static_cast<unsigned>(std::stoul(s.substr(i + 1, 4), nullptr, 16));
          v += cp < 0x80 ? static_cast<char>(cp) : '?';
          i += 4;
        } else v += e;
        ++i;
      } else {
        v += s[i++];
      }
    }
    ++i;  // Closing quote.
    return v;
  };
  ws();
  if (i >= s.size() || s[i] != '{') return out;
  ++i;
  while (true) {
    ws();
    if (i >= s.size() || s[i] != '"') break;
    const std::string key = str();
    ws();
    if (i >= s.size() || s[i] != ':') break;
    ++i;
    ws();
    if (i >= s.size()) break;
    if (s[i] == '"') {
      out[key] = str();
    } else if (s[i] == '{' || s[i] == '[') {
      int depth = 0;
      bool in_str = false;
      for (; i < s.size(); ++i) {
        if (in_str) {
          if (s[i] == '\\') ++i;
          else if (s[i] == '"') in_str = false;
        } else if (s[i] == '"') in_str = true;
        else if (s[i] == '{' || s[i] == '[') ++depth;
        else if ((s[i] == '}' || s[i] == ']') && --depth == 0) { ++i; break; }
      }
    } else {
      const size_t start = i;
      while (i < s.size() && s[i] != ',' && s[i] != '}') ++i;
      std::string v = s.substr(start, i - start);
      while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back()))) v.pop_back();
      out[key] = v;
    }
    ws();
    if (i < s.size() && s[i] == ',') { ++i; continue; }
    break;
  }
  return out;
}

void Emit(const std::string& line) {
  std::cout << line << "\n";
  std::cout.flush();
}

// --- H.264 Constrained Baseline: SPS, PPS, I_PCM IDR and all-skip P slices ------------------------------------------

class BitWriter {
 public:
  void Bit(int b) {
    cur_ = static_cast<uint8_t>((cur_ << 1) | (b & 1));
    if (++n_ == 8) Flush();
  }
  void Bits(uint32_t v, int count) {
    for (int k = count - 1; k >= 0; --k) Bit(static_cast<int>((v >> k) & 1));
  }
  void Ue(uint32_t v) {
    const uint32_t x = v + 1;
    int len = 0;
    while ((x >> len) > 1) ++len;
    Bits(0, len);
    Bits(x, len + 1);
  }
  void Se(int32_t v) { Ue(v <= 0 ? static_cast<uint32_t>(-2 * v) : static_cast<uint32_t>(2 * v - 1)); }
  bool Aligned() const { return n_ == 0; }
  void AlignZero() { while (!Aligned()) Bit(0); }
  void Byte(uint8_t b) { Bits(b, 8); }
  void Trailing() {
    Bit(1);
    AlignZero();
  }
  const std::vector<uint8_t>& bytes() const { return out_; }

 private:
  void Flush() {
    out_.push_back(cur_);
    cur_ = 0;
    n_ = 0;
  }
  std::vector<uint8_t> out_;
  uint8_t cur_ = 0;
  int n_ = 0;
};

constexpr int kWidthMbs = 10, kHeightMbs = 6;  // 160x96.

void AppendNal(std::vector<uint8_t>& au, uint8_t header, const std::vector<uint8_t>& rbsp) {
  static constexpr uint8_t kStart[4] = {0, 0, 0, 1};
  au.insert(au.end(), kStart, kStart + 4);
  au.push_back(header);
  int zeros = 0;
  for (uint8_t b : rbsp) {  // Emulation prevention.
    if (zeros >= 2 && b <= 3) {
      au.push_back(3);
      zeros = 0;
    }
    au.push_back(b);
    zeros = b == 0 ? zeros + 1 : 0;
  }
}

std::vector<uint8_t> Sps() {
  BitWriter w;
  w.Byte(66);    // profile_idc: Baseline.
  w.Byte(0xE0);  // constraint_set0/1/2: Constrained Baseline (profile-level-id 42e01f).
  w.Byte(31);    // level_idc 3.1.
  w.Ue(0);       // seq_parameter_set_id
  w.Ue(0);       // log2_max_frame_num_minus4: frame_num has 4 bits.
  w.Ue(2);       // pic_order_cnt_type 2: output order = decode order.
  w.Ue(1);       // max_num_ref_frames
  w.Bit(0);      // gaps_in_frame_num_value_allowed_flag
  w.Ue(kWidthMbs - 1);
  w.Ue(kHeightMbs - 1);
  w.Bit(1);  // frame_mbs_only_flag
  w.Bit(1);  // direct_8x8_inference_flag
  w.Bit(0);  // frame_cropping_flag
  w.Bit(0);  // vui_parameters_present_flag
  w.Trailing();
  return w.bytes();
}

std::vector<uint8_t> Pps() {
  BitWriter w;
  w.Ue(0);    // pic_parameter_set_id
  w.Ue(0);    // seq_parameter_set_id
  w.Bit(0);   // entropy_coding_mode_flag: CAVLC.
  w.Bit(0);   // bottom_field_pic_order_in_frame_present_flag
  w.Ue(0);    // num_slice_groups_minus1
  w.Ue(0);    // num_ref_idx_l0_default_active_minus1
  w.Ue(0);    // num_ref_idx_l1_default_active_minus1
  w.Bit(0);   // weighted_pred_flag
  w.Bits(0, 2);  // weighted_bipred_idc
  w.Se(0);    // pic_init_qp_minus26
  w.Se(0);    // pic_init_qs_minus26
  w.Se(0);    // chroma_qp_index_offset
  w.Bit(1);   // deblocking_filter_control_present_flag
  w.Bit(0);   // constrained_intra_pred_flag
  w.Bit(0);   // redundant_pic_cnt_present_flag
  w.Trailing();
  return w.bytes();
}

// A keyframe: SPS, PPS and an IDR slice of I_PCM macroblocks showing a bar at column `bar` (in macroblocks).
std::vector<uint8_t> IdrFrame(int idr_id, int bar) {
  std::vector<uint8_t> au;
  AppendNal(au, 0x67, Sps());
  AppendNal(au, 0x68, Pps());
  BitWriter w;
  w.Ue(0);  // first_mb_in_slice
  w.Ue(7);  // slice_type: I (all slices).
  w.Ue(0);  // pic_parameter_set_id
  w.Bits(0, 4);  // frame_num
  w.Ue(static_cast<uint32_t>(idr_id & 1));  // idr_pic_id
  w.Bit(0);  // no_output_of_prior_pics_flag
  w.Bit(0);  // long_term_reference_flag
  w.Se(0);   // slice_qp_delta
  w.Ue(1);   // disable_deblocking_filter_idc: off.
  for (int y = 0; y < kHeightMbs; ++y) {
    for (int x = 0; x < kWidthMbs; ++x) {
      w.Ue(25);  // mb_type I_PCM.
      w.AlignZero();
      const bool on_bar = x == bar;
      const uint8_t luma = on_bar ? 235 : static_cast<uint8_t>(40 + 16 * y);
      for (int k = 0; k < 256; ++k) w.Byte(luma);
      for (int k = 0; k < 64; ++k) w.Byte(on_bar ? 128 : static_cast<uint8_t>(60 + 12 * x));  // Cb
      for (int k = 0; k < 64; ++k) w.Byte(on_bar ? 128 : 160);                                 // Cr
    }
  }
  w.Trailing();
  AppendNal(au, 0x65, w.bytes());
  return au;
}

// A P frame that repeats the reference picture: every macroblock skipped.
std::vector<uint8_t> SkipFrame(int frame_num) {
  BitWriter w;
  w.Ue(0);  // first_mb_in_slice
  w.Ue(5);  // slice_type: P (all slices).
  w.Ue(0);  // pic_parameter_set_id
  w.Bits(static_cast<uint32_t>(frame_num & 15), 4);
  w.Bit(0);  // num_ref_idx_active_override_flag
  w.Bit(0);  // ref_pic_list_modification_flag_l0
  w.Bit(0);  // adaptive_ref_pic_marking_mode_flag
  w.Se(0);   // slice_qp_delta
  w.Ue(1);   // disable_deblocking_filter_idc
  w.Ue(kWidthMbs * kHeightMbs);  // mb_skip_run
  w.Trailing();
  std::vector<uint8_t> au;
  AppendNal(au, 0x41, w.bytes());  // nal_ref_idc 2, coded slice of a non-IDR picture.
  return au;
}

// Counts the audio frames the session decodes; plays silence.
class CountingDecoder : public rtp::AudioDecoder {
 public:
  std::optional<std::vector<float>> Decode(std::span<const uint8_t>, uint32_t) override {
    ++frames;
    return std::vector<float>(960, 0.0f);
  }
  int frames = 0;
};

// Lines read from stdin by a thread; the main loop takes them.
class StdinLines {
 public:
  StdinLines() {
    std::thread([this] {
      std::string line;
      while (std::getline(std::cin, line)) {
        std::lock_guard<std::mutex> lock(mu_);
        lines_.push_back(line);
      }
      std::lock_guard<std::mutex> lock(mu_);
      eof_ = true;
    }).detach();
  }
  std::deque<std::string> Take(bool* eof) {
    std::lock_guard<std::mutex> lock(mu_);
    std::deque<std::string> out;
    out.swap(lines_);
    *eof = eof_;
    return out;
  }

 private:
  std::mutex mu_;
  std::deque<std::string> lines_;
  bool eof_ = false;
};

}  // namespace

int main(int argc, char** argv) {
  std::string role = "answer";
  std::string ip_text = "127.0.0.1";
  int max_seconds = 60;
#if defined(SM_WITH_DATACHANNEL) && SM_WITH_DATACHANNEL
  bool use_dc = true;
#else
  bool use_dc = false;
#endif
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--no-data-channel") use_dc = false;
    else if (a.rfind("--role=", 0) == 0) role = a.substr(7);
    else if (a.rfind("--ip=", 0) == 0) ip_text = a.substr(5);
    else if (a.rfind("--max-seconds=", 0) == 0) max_seconds = std::stoi(a.substr(14));
  }
  const auto ip = p2p::IpAddress::FromString(ip_text);
  if (!ip) {
    std::fprintf(stderr, "bad --ip=%s\n", ip_text.c_str());
    return 2;
  }
  p2p::InstallStunCryptoFromTls();

  interop::OsSocketServer sockets;
  p2p::StaticNetworkManager network_manager;
  network_manager.AddInterface(*ip);
  Timestamp now = Now();
  p2p::BasicPortAllocator allocator(&network_manager, &sockets);
  allocator.SetConfiguration(now, p2p::ServerAddresses{}, {}, 0, p2p::NO_PRUNE);
  allocator.set_flags(p2p::PORTALLOCATOR_DISABLE_TCP);

  CountingDecoder decoder;
  PeerEndpointConfig config;
  config.allocator = &allocator;
  config.certificate_name = "sm-browser-peer";
  config.audio_decoder = &decoder;
  config.audio_stream.nominal_rate = DataRate::KilobitsPerSec(32);
  config.session.send_side.gcc.start_rate = DataRate::KilobitsPerSec(800);
  config.enable_data_channels = use_dc;
  PeerEndpoint ep(std::move(config), now);

  std::vector<std::string> errors;
#if defined(SM_WITH_DATACHANNEL) && SM_WITH_DATACHANNEL
  // The data channel: ours if we offer, Chrome's if it does.
  struct DcLog : DataChannelObserver {
    std::shared_ptr<SctpDataChannel> channel;
    std::vector<std::string> received;
    void OnStateChange() override {
      std::fprintf(stderr, "data channel %s: state %d\n", channel->label().c_str(), static_cast<int>(channel->state()));
    }
    void OnMessage(const DataBuffer& b) override {
      received.emplace_back(b.data.begin(), b.data.end());
      std::fprintf(stderr, "data channel message: %s\n", received.back().c_str());
    }
  } dc;
  bool dc_sent = false;
  if (use_dc) {
    ep.SetOnDataChannel([&dc](std::shared_ptr<SctpDataChannel> c) {
      if (dc.channel) return;
      dc.channel = std::move(c);
      dc.channel->RegisterObserver(&dc);
    });
    if (role == "offer") {
      RtcError err;
      dc.channel = ep.CreateDataChannel("sm-interop", DataChannelInit{}, now, &err);
      if (!dc.channel) errors.push_back("CreateDataChannel: " + err.message());
      else dc.channel->RegisterObserver(&dc);
    }
  }
#endif
  ep.SetOnIceCandidate([](std::unique_ptr<IceCandidate> cand) {
    Emit("{\"type\":\"candidate\",\"candidate\":" + JsonEscape(cand->ToString()) +
         ",\"sdpMid\":" + JsonEscape(cand->sdp_mid()) + ",\"sdpMLineIndex\":" + std::to_string(cand->sdp_mline_index()) +
         "}");
  });

  auto send_description = [&](const SessionDescriptionInterface& desc, const char* type) {
    Emit(std::string("{\"type\":\"") + type + "\",\"sdp\":" + JsonEscape(SdpSerialize(desc)) + "}");
  };

  if (role == "offer") {
    std::string error;
    auto offer = ep.CreateOffer(&error);
    if (!offer) errors.push_back("CreateOffer: " + error);
    else {
      send_description(*offer, "offer");
      if (!ep.SetLocalDescription(std::move(offer), now, &error)) errors.push_back("SetLocalDescription: " + error);
    }
  }

  StdinLines input;
  const Timestamp start = now;
  bool bye = false, eof = false;
  bool connected = false;
  Timestamp next_audio = Timestamp::PlusInfinity(), next_video = Timestamp::PlusInfinity();
  Timestamp next_pull = Timestamp::PlusInfinity();
  int audio_sent = 0, video_sent = 0, keyframes_sent = 0, frame_num = 0, video_frames_received = 0;
  int keyframe_requests = 0;
  bool want_keyframe = true;
  Timestamp last_keyframe = Timestamp::MinusInfinity();
  std::vector<float> pcm(480);

  while (!bye && !eof && now - start < TimeDelta::Seconds(max_seconds)) {
    for (const std::string& line : input.Take(&eof)) {
      auto m = JsonParseFlat(line);
      const std::string type = m["type"];
      std::string error;
      if (type == "bye") {
        bye = true;
      } else if (type == "offer" || type == "answer") {
        SdpParseError perr;
        auto desc = SdpDeserialize(type == "offer" ? SdpType::kOffer : SdpType::kAnswer, m["sdp"], &perr);
        if (!desc) {
          errors.push_back("SdpDeserialize: " + perr.description + " at: " + perr.line);
          continue;
        }
        if (!ep.SetRemoteDescription(std::move(desc), now, &error)) {
          errors.push_back("SetRemoteDescription: " + error);
          continue;
        }
        if (type == "offer") {
          auto answer = ep.CreateAnswer(&error);
          if (!answer) {
            errors.push_back("CreateAnswer: " + error);
            continue;
          }
          send_description(*answer, "answer");
          if (!ep.SetLocalDescription(std::move(answer), now, &error)) errors.push_back("SetLocalDescription: " + error);
        }
      } else if (type == "candidate") {
        if (m["candidate"].empty()) continue;  // End of candidates.
        SdpParseError perr;
        const int index = m["sdpMLineIndex"].empty() || m["sdpMLineIndex"] == "null" ? 0 : std::stoi(m["sdpMLineIndex"]);
        const std::string mid = m["sdpMid"] == "null" ? "" : m["sdpMid"];
        auto cand = IceCandidate::Create(mid, index, m["candidate"], &perr);
        if (!cand) {
          std::fprintf(stderr, "candidate not parsed: %s\n", m["candidate"].c_str());
        } else if (!ep.AddIceCandidate(*cand, now, &error)) {
          std::fprintf(stderr, "AddIceCandidate: %s (%s)\n", error.c_str(), m["candidate"].c_str());
        }
      }
    }

    now = Now();
    ep.Process(now);
#if defined(SM_WITH_DATACHANNEL) && SM_WITH_DATACHANNEL
    if (dc.channel && !dc_sent && dc.channel->state() == DataState::kOpen) {
      dc_sent = dc.channel->Send(now, DataBuffer("hello from sm"));
      if (!dc_sent) errors.push_back("data channel Send failed");
    }
#endif
    if (!connected && ep.IsConnected() && ep.session()) {
      connected = true;
      std::fprintf(stderr, "connected after %.1f s\n", (now - start).seconds());
      next_audio = next_video = next_pull = now;
    }
    if (connected) {
      rtp::Session* s = ep.session();
      for (const auto& e : s->TakeEvents()) {
        if (e.type == rtp::SessionEventType::kKeyframeRequested) {
          ++keyframe_requests;
          want_keyframe = true;
        }
      }
      if (now >= next_audio && ep.audio_send()) {
        static constexpr uint8_t kOpusSilence[3] = {0xF8, 0xFF, 0xFE};
        s->SendAudio(*ep.audio_send(),
                     rtp::EncodedAudioFrame{static_cast<uint32_t>(audio_sent * 960), kOpusSilence, audio_sent == 0}, now);
        ++audio_sent;
        next_audio += TimeDelta::Millis(20);
      }
      if (now >= next_video && ep.video_send()) {
        const bool key = want_keyframe || now - last_keyframe >= TimeDelta::Seconds(1);
        std::vector<uint8_t> frame;
        if (key) {
          frame = IdrFrame(keyframes_sent, keyframes_sent % kWidthMbs);
          ++keyframes_sent;
          frame_num = 0;
          last_keyframe = now;
          want_keyframe = false;
        } else {
          frame = SkipFrame(++frame_num);
        }
        s->SendVideo(*ep.video_send(), rtp::EncodedVideoFrame{static_cast<uint32_t>(video_sent * 6000), frame, key},
                     now);
        ++video_sent;
        next_video += TimeDelta::Micros(66667);
      }
      if (now >= next_pull) {
        if (ep.audio_receive()) s->GetAudio(*ep.audio_receive(), pcm, now);
        if (ep.video_receive()) video_frames_received += static_cast<int>(s->TakeVideoFrames(*ep.video_receive()).size());
        next_pull += TimeDelta::Millis(10);
      }
      ep.FlushOutgoing(now);
    }
    Timestamp next = ep.NextProcessTime(now);
    next = std::min({next, next_audio, next_video, next_pull, now + TimeDelta::Millis(10)});
    sockets.Poll(now, std::clamp(next - now, TimeDelta::Zero(), TimeDelta::Millis(10)));
    now = Now();
  }

  const rtp::SessionStats* st = ep.session() ? &ep.session()->stats() : nullptr;
  const bool ok_connected = connected;
  const bool ok_audio_in = decoder.frames > 0;
  const bool ok_video_in = video_frames_received > 0;
  const bool ok_media_out = audio_sent > 0 && video_sent > 0 && ep.send_failures() == 0;
  const bool ok_signaling = errors.empty();
  for (const auto& e : errors) std::fprintf(stderr, "error: %s\n", e.c_str());
  bool ok_data = true;
  std::string data_check;
#if defined(SM_WITH_DATACHANNEL) && SM_WITH_DATACHANNEL
  if (use_dc) {
    ok_data = dc_sent && std::find(dc.received.begin(), dc.received.end(), "hello from chrome") != dc.received.end();
    data_check = std::string(",\"dataFromChrome\":") + (ok_data ? "true" : "false");
    if (dc.channel) dc.channel->UnregisterObserver();
  }
#endif
  const bool ok = ok_connected && ok_audio_in && ok_video_in && ok_media_out && ok_signaling && ok_data;
  auto b = [](bool v) { return v ? "true" : "false"; };
  std::string errs = "[";
  for (size_t i = 0; i < errors.size(); ++i) errs += (i ? "," : "") + JsonEscape(errors[i]);
  errs += "]";
  Emit(std::string("{\"type\":\"result\",\"ok\":") + b(ok) + ",\"checks\":{\"connected\":" + b(ok_connected) +
       ",\"audioFramesDecoded\":" + b(ok_audio_in) + ",\"videoFramesReceived\":" + b(ok_video_in) +
       ",\"mediaSent\":" + b(ok_media_out) + ",\"signaling\":" + b(ok_signaling) + data_check + "},\"stats\":{\"audioFramesDecoded\":" +
       std::to_string(decoder.frames) + ",\"videoFramesReceived\":" + std::to_string(video_frames_received) +
       ",\"audioFramesSent\":" + std::to_string(audio_sent) + ",\"videoFramesSent\":" + std::to_string(video_sent) +
       ",\"keyframesSent\":" + std::to_string(keyframes_sent) + ",\"keyframeRequests\":" +
       std::to_string(keyframe_requests) + ",\"rtpIn\":" + std::to_string(st ? st->rtp_in : 0) + ",\"rtcpIn\":" +
       std::to_string(st ? st->rtcp_in : 0) + ",\"transportCcSent\":" +
       std::to_string(ep.session() ? ep.session()->rtcp().stats().transport_cc_sent : 0) + ",\"transportCcPackets\":" +
       std::to_string(ep.session() ? ep.session()->transport_feedback().stats().packets : 0) + ",\"sendFailures\":" + std::to_string(ep.send_failures()) +
       "},\"errors\":" + errs + "}");
  return ok ? 0 : 1;
}
