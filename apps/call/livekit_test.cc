// The native LiveKit client (C19, #1164) without a server:
// - LiveKitProto.*: the protobuf codec against hand-built bytes (the wire format of the vendored livekit_rtc.proto),
//   both directions through the server-side test helpers, unknown fields skipped, malformed input refused, and the
//   join URL.
// - LiveKitClient.PublishesAfterJoin: a CallClient in LiveKit mode answers a JoinResponse with AddTrackRequests for the
//   microphone and the camera, offers once both are published (the tracks' cids are the offer's a=msid track ids),
//   pings at the server's interval, trickles its candidates to the publisher, and leaves with CLIENT_INITIATED.
// - LiveKitClient.GroupState (C20, #1172): participants joining and leaving (ParticipantUpdate), the active speaker
//   from SpeakersChanged, our mute and video-off announced with MuteTrackRequest, a server mute applied, and
//   LiveKit's data-only first subscriber offer answered on a second connection (target SUBSCRIBER).
// The live path (a LiveKit server, Chrome and the AI server) is interop.WebApp.group_native and
// interop.WebApp.group_all (apps/web/test_group_call.py).
#include <string>
#include <vector>

#include "call_client.h"
#include "livekit_proto.h"
#include "sm/p2p/basic_port_allocator.h"
#include "sm/p2p/network_manager.h"
#include "sm/pc/peer_endpoint.h"
#include "sm/pc/webrtc_sdp.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::apps::livekit;

SM_TEST(LiveKitProto, WireFormat) {
  // SignalRequest{ping_req: Ping{timestamp: 300, rtt: 1}}: field 16 (tag 0x82 0x01), length 5, then 08 AC 02 10 01.
  SM_EXPECT_EQ(EncodePing(300, 1), std::string("\x82\x01\x05\x08\xac\x02\x10\x01", 8));
  // SignalRequest{leave: LeaveRequest{reason: CLIENT_INITIATED}}: field 8 (0x42), length 2, 10 01.
  LeaveRequest leave;
  leave.reason = kClientInitiated;
  SM_EXPECT_EQ(EncodeLeave(leave), std::string("\x42\x02\x10\x01", 4));
  // A SignalResponse{pong_resp: Pong{last_ping_timestamp: 300, timestamp: 7}} decodes.
  const SignalResponse pong = DecodeSignalResponse(std::string("\xa2\x01\x05\x08\xac\x02\x10\x07", 8));
  SM_EXPECT_TRUE(pong.kind == SignalResponse::Kind::kPongResp);
  SM_EXPECT_EQ(pong.pong.last_ping_timestamp, int64_t{300});
  SM_EXPECT_EQ(pong.pong.timestamp, int64_t{7});
  // Truncated or garbage input is not a message.
  SM_EXPECT_TRUE(DecodeSignalResponse(std::string("\xa2\x01\x05\x08", 4)).kind == SignalResponse::Kind::kInvalid);
  SM_EXPECT_TRUE(DecodeSignalResponse("").kind == SignalResponse::Kind::kInvalid);
  SM_EXPECT_TRUE(DecodeSignalResponse(std::string("\xff\xff\xff", 3)).kind == SignalResponse::Kind::kInvalid);
  // A member this client doesn't use (connection_quality = 12) is kOther, and unknown fields inside a known one are
  // skipped (a fixed64 and a fixed32 in a TrickleRequest).
  SM_EXPECT_TRUE(DecodeSignalResponse(std::string("\x62\x00", 2)).kind == SignalResponse::Kind::kOther);
  // SignalResponse{speakers_changed: {speakers: [{sid: "PA_a", level: 0.5, active: true}]}}: field 10 (0x52), the
  // level a fixed32 float (0x15, 00 00 00 3F little-endian).
  const SignalResponse sp = DecodeSignalResponse(
      std::string("\x52\x10\x0a\x0e\x0a\x04PA_a\x15\x00\x00\x00\x3f\x18\x01", 18));
  SM_ASSERT_TRUE(sp.kind == SignalResponse::Kind::kSpeakersChanged);
  SM_ASSERT_EQ(sp.speakers.size(), size_t{1});
  SM_EXPECT_EQ(sp.speakers[0].sid, std::string("PA_a"));
  SM_EXPECT_TRUE(sp.speakers[0].level == 0.5f && sp.speakers[0].active);
  // SignalRequest{mute: {sid: "TR_a", muted: true}}: field 5 (0x2a); track_setting = 7 (0x3a) with width and height.
  SM_EXPECT_EQ(EncodeMute("TR_a", true), std::string("\x2a\x08\x0a\x04TR_a\x10\x01", 10));
  SM_EXPECT_EQ(EncodeUpdateTrackSettings({"TR_v"}, false, 320, 180),
               std::string("\x3a\x0c\x0a\x04TR_v\x28\xc0\x02\x30\xb4\x01", 14));
  const SignalResponse t = DecodeSignalResponse(std::string("\x22\x14\x0a\x02{}\x49\x01\x02\x03\x04\x05\x06\x07\x08"
                                                            "\x55\x01\x02\x03\x04\x10\x01", 22));
  SM_EXPECT_TRUE(t.kind == SignalResponse::Kind::kTrickle);
  SM_EXPECT_EQ(t.trickle.candidate_init, std::string("{}"));
  SM_EXPECT_TRUE(t.trickle.target == SignalTarget::kSubscriber);
}

SM_TEST(LiveKitProto, RoundTrips) {
  SignalResponse join;
  join.kind = SignalResponse::Kind::kJoin;
  join.join.room_sid = "RM_1";
  join.join.room_name = "team";
  join.join.participant.sid = "PA_me";
  join.join.participant.identity = "me";
  ParticipantInfo other;
  other.sid = "PA_o";
  other.identity = "web-1";
  other.state = ParticipantInfo::State::kActive;
  TrackInfo cam;
  cam.sid = "TR_v";
  cam.type = TrackType::kVideo;
  cam.source = TrackSource::kCamera;
  cam.width = 1280;
  cam.height = 720;
  cam.mime_type = "video/H264";
  other.tracks.push_back(cam);
  join.join.other_participants.push_back(other);
  join.join.ice_servers.push_back({{"turn:t.example:3478", "turns:t.example:443"}, "u", "c"});
  join.join.subscriber_primary = true;
  join.join.ping_interval_s = 5;
  join.join.ping_timeout_s = 15;
  const SignalResponse j = DecodeSignalResponse(EncodeSignalResponseForTest(join));
  SM_ASSERT_TRUE(j.kind == SignalResponse::Kind::kJoin);
  SM_EXPECT_EQ(j.join.room_name, std::string("team"));
  SM_EXPECT_EQ(j.join.participant.identity, std::string("me"));
  SM_ASSERT_EQ(j.join.other_participants.size(), size_t{1});
  SM_EXPECT_TRUE(j.join.other_participants[0].state == ParticipantInfo::State::kActive);
  SM_ASSERT_EQ(j.join.other_participants[0].tracks.size(), size_t{1});
  SM_EXPECT_EQ(j.join.other_participants[0].tracks[0].height, uint32_t{720});
  SM_EXPECT_EQ(j.join.other_participants[0].tracks[0].mime_type, std::string("video/H264"));
  SM_ASSERT_EQ(j.join.ice_servers.size(), size_t{1});
  SM_EXPECT_EQ(j.join.ice_servers[0].urls.size(), size_t{2});
  SM_EXPECT_EQ(j.join.ice_servers[0].credential, std::string("c"));
  SM_EXPECT_TRUE(j.join.subscriber_primary);
  SM_EXPECT_EQ(j.join.ping_interval_s, 5);

  SessionDescription sd{"offer", "v=0\r\n", 7, {{"0", "TR_a"}, {"1", "TR_v"}}};
  const auto req = DecodeSignalRequestForTest(EncodeOffer(sd));
  SM_ASSERT_TRUE(req.has_value());
  SM_EXPECT_EQ(req->field, 1);
  SM_EXPECT_EQ(req->description.sdp, sd.sdp);
  SM_EXPECT_EQ(req->description.id, uint32_t{7});
  SM_EXPECT_EQ(req->description.mid_to_track_id.at("1"), std::string("TR_v"));
  SM_EXPECT_EQ(DecodeSignalRequestForTest(EncodeAnswer(sd))->field, 2);

  AddTrackRequest a;
  a.cid = "sm-video-x";
  a.name = "camera";
  a.type = TrackType::kVideo;
  a.source = TrackSource::kCamera;
  a.width = 640;
  a.height = 360;
  a.layers.push_back({VideoQuality::kHigh, 640, 360, 0, 0});
  const auto add = DecodeSignalRequestForTest(EncodeAddTrack(a));
  SM_ASSERT_TRUE(add.has_value());
  SM_EXPECT_EQ(add->field, 4);
  SM_EXPECT_EQ(add->add_track.cid, a.cid);
  SM_EXPECT_TRUE(add->add_track.type == TrackType::kVideo && add->add_track.source == TrackSource::kCamera);
  SM_ASSERT_EQ(add->add_track.layers.size(), size_t{1});
  SM_EXPECT_TRUE(add->add_track.layers[0].quality == VideoQuality::kHigh);
  SM_EXPECT_EQ(add->add_track.layers[0].height, uint32_t{360});

  SignalResponse speakers;
  speakers.kind = SignalResponse::Kind::kSpeakersChanged;
  speakers.speakers = {{"PA_a", 0.25f, true}, {"PA_b", 0, false}};
  const SignalResponse s = DecodeSignalResponse(EncodeSignalResponseForTest(speakers));
  SM_ASSERT_EQ(s.speakers.size(), size_t{2});
  SM_EXPECT_TRUE(s.speakers[0].level == 0.25f && s.speakers[0].active && !s.speakers[1].active);
  SignalResponse mute;
  mute.kind = SignalResponse::Kind::kMute;
  mute.text = "TR_a";
  mute.muted = true;
  const SignalResponse m = DecodeSignalResponse(EncodeSignalResponseForTest(mute));
  SM_EXPECT_TRUE(m.kind == SignalResponse::Kind::kMute && m.text == "TR_a" && m.muted);
  const auto settings = DecodeSignalRequestForTest(EncodeUpdateTrackSettings({"TR_v", "TR_w"}, true, 0, 0));
  SM_ASSERT_TRUE(settings.has_value());
  SM_EXPECT_EQ(settings->field, 7);
  SM_EXPECT_EQ(settings->settings_sids.size(), size_t{2});
  SM_EXPECT_TRUE(settings->settings_disabled);

  TrickleRequest tr{"{\"candidate\":\"candidate:1 1 udp 1 1.2.3.4 5 typ host\"}", SignalTarget::kSubscriber, false};
  const auto trickle = DecodeSignalRequestForTest(EncodeTrickle(tr));
  SM_ASSERT_TRUE(trickle.has_value());
  SM_EXPECT_EQ(trickle->trickle.candidate_init, tr.candidate_init);
  SM_EXPECT_TRUE(trickle->trickle.target == SignalTarget::kSubscriber);
}

SM_TEST(LiveKitProto, JoinPath) {
  SM_EXPECT_EQ(JoinPath("/", "a.b-c_d", false), std::string("/rtc?access_token=a.b-c_d&auto_subscribe=0&protocol=15"
                                                            "&adaptive_stream=0"));
  SM_EXPECT_EQ(JoinPath("/lk", "t+/=", true).substr(0, 36), std::string("/lk/rtc?access_token=t%2B%2F%3D&auto"));
}

SM_TEST(LiveKitClient, PublishesAfterJoin) {
  std::vector<std::string> sent;
  apps::CallClientConfig cfg;
  cfg.livekit = true;
  cfg.room = "team";
  cfg.peer_id = "nat";
  cfg.audio_processing = false;
  cfg.interfaces.push_back(*p2p::IpAddress::FromString("127.0.0.1"));
  apps::CallClient client(cfg, [&](const std::string& m) { sent.push_back(m); });
  Timestamp now = Timestamp::Seconds(100);
  client.Start(now);
  SM_EXPECT_TRUE(sent.empty());  // The join is the WebSocket's URL.

  SignalResponse join;
  join.kind = SignalResponse::Kind::kJoin;
  join.join.room_name = "team";
  join.join.participant.identity = "nat";
  join.join.ping_interval_s = 2;
  client.OnSignal(EncodeSignalResponseForTest(join), now);
  SM_ASSERT_EQ(sent.size(), size_t{2});
  const auto audio = DecodeSignalRequestForTest(sent[0]);
  const auto video = DecodeSignalRequestForTest(sent[1]);
  SM_ASSERT_TRUE(audio && video && audio->field == 4 && video->field == 4);
  SM_EXPECT_TRUE(audio->add_track.type == TrackType::kAudio && audio->add_track.source == TrackSource::kMicrophone);
  SM_EXPECT_TRUE(audio->add_track.disable_red);
  SM_EXPECT_TRUE(video->add_track.type == TrackType::kVideo && video->add_track.source == TrackSource::kCamera);
  SM_EXPECT_EQ(video->add_track.width, uint32_t{640});

  // The offer waits for both tracks.
  for (const auto* t : {&*audio, &*video}) {
    SignalResponse pub;
    pub.kind = SignalResponse::Kind::kTrackPublished;
    pub.track_published.cid = t->add_track.cid;
    pub.track_published.track.sid = "TR_" + t->add_track.cid;
    client.OnSignal(EncodeSignalResponseForTest(pub), now);
  }
  SM_ASSERT_EQ(sent.size(), size_t{3});
  const auto offer = DecodeSignalRequestForTest(sent[2]);
  SM_ASSERT_TRUE(offer && offer->field == 1);
  SM_EXPECT_EQ(offer->description.type, std::string("offer"));
  SM_EXPECT_EQ(offer->description.id, uint32_t{1});
  const std::string& sdp = offer->description.sdp;
  SM_EXPECT_TRUE(sdp.find("m=audio") != std::string::npos && sdp.find("m=video") != std::string::npos);
  SM_EXPECT_TRUE(sdp.find(" " + audio->add_track.cid) != std::string::npos);  // a=msid:<stream> <cid>
  SM_EXPECT_TRUE(sdp.find(" " + video->add_track.cid) != std::string::npos);
  SM_EXPECT_TRUE(sdp.find("m=application") == std::string::npos);

  // Candidates trickle to the publisher; a ping goes out at the server's interval.
  bool trickled = false, pinged = false;
  for (int i = 0; i < 300 && !(trickled && pinged); ++i) {
    client.Step(now);
    client.Wait(now, TimeDelta::Millis(1));
    now += TimeDelta::Millis(10);
    for (const std::string& m : sent) {
      const auto r = DecodeSignalRequestForTest(m);
      if (r && r->field == 3 && r->trickle.target == SignalTarget::kPublisher &&
          r->trickle.candidate_init.find("\"candidate\":\"candidate:") != std::string::npos) {
        trickled = true;
      }
      if (r && r->field == 16 && r->ping_timestamp > 0) pinged = true;
    }
  }
  SM_EXPECT_TRUE(trickled);
  SM_EXPECT_TRUE(pinged);
  client.Hangup(now);
  const auto leave = DecodeSignalRequestForTest(sent.back());
  SM_ASSERT_TRUE(leave && leave->field == 8);
  SM_EXPECT_EQ(leave->leave.reason, kClientInitiated);
  SM_EXPECT_TRUE(client.state() == apps::CallState::kEnded);
}

SM_TEST(LiveKitClient, ServerLeaveEndsTheCall) {
  apps::CallClientConfig cfg;
  cfg.livekit = true;
  cfg.peer_id = "nat";
  cfg.audio_processing = false;
  cfg.livekit_publish_video = false;
  std::vector<std::string> sent;
  apps::CallClient client(cfg, [&](const std::string& m) { sent.push_back(m); });
  const Timestamp now = Timestamp::Seconds(5);
  client.Start(now);
  SignalResponse join;
  join.kind = SignalResponse::Kind::kJoin;
  client.OnSignal(EncodeSignalResponseForTest(join), now);
  SM_EXPECT_EQ(sent.size(), size_t{1});  // The microphone only.
  SignalResponse leave;
  leave.kind = SignalResponse::Kind::kLeave;
  leave.leave.reason = 4;  // PARTICIPANT_REMOVED
  client.OnSignal(EncodeSignalResponseForTest(leave), now);
  SM_EXPECT_TRUE(client.state() == apps::CallState::kEnded);
  SM_EXPECT_TRUE(client.stats().ended_reason.find("leave") != std::string::npos);
}

namespace {

ParticipantInfo Participant(const std::string& identity, bool muted_mic) {
  ParticipantInfo p;
  p.sid = "PA_" + identity;
  p.identity = identity;
  p.state = ParticipantInfo::State::kActive;
  TrackInfo mic;
  mic.sid = "TR_a" + identity;
  mic.type = TrackType::kAudio;
  mic.muted = muted_mic;
  TrackInfo cam;
  cam.sid = "TR_v" + identity;
  cam.type = TrackType::kVideo;
  p.tracks = {mic, cam};
  return p;
}

}  // namespace

SM_TEST(LiveKitClient, GroupState) {
  std::vector<std::string> sent;
  apps::CallClientConfig cfg;
  cfg.livekit = true;
  cfg.room = "team";
  cfg.peer_id = "nat";
  cfg.audio_processing = false;
  cfg.interfaces.push_back(*p2p::IpAddress::FromString("127.0.0.1"));
  apps::CallClient client(cfg, [&](const std::string& m) { sent.push_back(m); });
  Timestamp now = Timestamp::Seconds(100);
  client.Start(now);
  SignalResponse join;
  join.kind = SignalResponse::Kind::kJoin;
  join.join.room_name = "team";
  join.join.participant.identity = "nat";
  join.join.participant.sid = "PA_nat";
  join.join.other_participants = {Participant("web-1", false)};
  join.join.subscriber_primary = true;
  client.OnSignal(EncodeSignalResponseForTest(join), now);
  for (const char* cid : {"sm-audio-nat", "sm-video-nat"}) {
    SignalResponse pub;
    pub.kind = SignalResponse::Kind::kTrackPublished;
    pub.track_published.cid = cid;
    pub.track_published.track.sid = std::string("TR_") + cid;
    client.OnSignal(EncodeSignalResponseForTest(pub), now);
  }

  // Two more join (one muted), then the first leaves.
  SignalResponse update;
  update.kind = SignalResponse::Kind::kUpdate;
  update.participants = {Participant("web-2", false), Participant("ai-1", true)};
  client.OnSignal(EncodeSignalResponseForTest(update), now);
  auto group = client.participants();
  SM_ASSERT_EQ(group.size(), size_t{3});
  SM_EXPECT_EQ(group[0].identity, std::string("web-1"));
  SM_EXPECT_EQ(group[2].identity, std::string("ai-1"));
  SM_EXPECT_TRUE(!group[0].audio_muted && group[2].audio_muted && !group[2].video_muted);
  ParticipantInfo gone = Participant("web-1", false);
  gone.state = ParticipantInfo::State::kDisconnected;
  update.participants = {gone};
  client.OnSignal(EncodeSignalResponseForTest(update), now);
  group = client.participants();
  SM_ASSERT_EQ(group.size(), size_t{2});
  SM_EXPECT_EQ(group[0].identity, std::string("web-2"));

  // SpeakersChanged: web-2 and then we are the loudest active speaker; changes only.
  SignalResponse speakers;
  speakers.kind = SignalResponse::Kind::kSpeakersChanged;
  speakers.speakers = {{"PA_web-2", 0.6f, true}, {"PA_nat", 0.2f, true}};
  client.OnSignal(EncodeSignalResponseForTest(speakers), now);
  SM_EXPECT_EQ(client.active_speaker(), std::string("web-2"));
  group = client.participants();
  SM_EXPECT_TRUE(group[0].speaking && group[0].audio_level > 0.5 && !group[1].speaking);
  speakers.speakers = {{"PA_web-2", 0, false}};
  client.OnSignal(EncodeSignalResponseForTest(speakers), now);
  SM_EXPECT_EQ(client.active_speaker(), std::string("nat"));

  // Mute and video off are announced once each (on the call thread's Step); a server mute is applied.
  sent.clear();
  client.SetMuted(true);
  client.SetVideoEnabled(false);
  client.Step(now);
  client.Step(now);
  int mutes = 0;
  for (const std::string& m : sent) {
    const auto r = DecodeSignalRequestForTest(m);
    if (r && r->field == 5) {
      ++mutes;
      SM_EXPECT_TRUE(r->mute);
    }
  }
  SM_EXPECT_EQ(mutes, 2);
  SignalResponse unmute;
  unmute.kind = SignalResponse::Kind::kMute;
  unmute.text = "TR_sm-audio-nat";
  unmute.muted = false;
  client.OnSignal(EncodeSignalResponseForTest(unmute), now);
  SM_EXPECT_TRUE(!client.muted());

  // LiveKit's first subscriber offer carries only its data channels: answered, target SUBSCRIBER.
  p2p::StaticNetworkManager network;
  network.AddInterface(*p2p::IpAddress::FromString("127.0.0.1"));
  interop::OsSocketServer sockets;
  p2p::BasicPortAllocator allocator(&network, &sockets);
  pc::PeerEndpointConfig sfu_cfg;
  sfu_cfg.allocator = &allocator;
  sfu_cfg.audio = false;
  sfu_cfg.video = false;
  pc::PeerEndpoint sfu(std::move(sfu_cfg), now);
  pc::RtcError err;
  const bool data = sfu.CreateDataChannel("_reliable", pc::DataChannelInit{}, now, &err) != nullptr;
  if (data) {  // Built with SM_WITH_DATACHANNEL.
    auto offer = sfu.CreateOffer();
    SM_ASSERT_TRUE(offer != nullptr);
    SignalResponse sub_offer;
    sub_offer.kind = SignalResponse::Kind::kOffer;
    sub_offer.description = {"offer", pc::SdpSerialize(*offer), 1, {}};
    sent.clear();
    client.OnSignal(EncodeSignalResponseForTest(sub_offer), now);
    bool answered = false;
    for (const std::string& m : sent) {
      const auto r = DecodeSignalRequestForTest(m);
      if (r && r->field == 2) {
        answered = true;
        SM_EXPECT_EQ(r->description.id, uint32_t{1});
        SM_EXPECT_TRUE(r->description.sdp.find("m=application") != std::string::npos);
      }
    }
    SM_EXPECT_TRUE(answered);
    SM_EXPECT_TRUE(client.stats().state != apps::CallState::kEnded);
  }
  client.Hangup(now);
}
