// LiveKit's client signaling messages (C19, #1164); see livekit_proto.h. Field numbers from the vendored
// apps/call/livekit/protocol/livekit_rtc.proto and livekit_models.proto.
#include "livekit_proto.h"

#include <cctype>
#include <cstdio>

namespace sm::apps::livekit {

// --- Wire format ------------------------------------------------------------------------------------------------------

void PbWriter::Raw(uint64_t v) {
  while (v >= 0x80) {
    data_.push_back(static_cast<char>((v & 0x7f) | 0x80));
    v >>= 7;
  }
  data_.push_back(static_cast<char>(v));
}

void PbWriter::Tag(int field, int wire_type) { Raw((static_cast<uint64_t>(field) << 3) | static_cast<uint64_t>(wire_type)); }

void PbWriter::Varint(int field, uint64_t v) {
  if (v == 0) return;
  Tag(field, 0);
  Raw(v);
}

void PbWriter::Bytes(int field, std::string_view v) {
  if (v.empty()) return;
  Tag(field, 2);
  Raw(v.size());
  data_.append(v);
}

void PbWriter::Message(int field, const PbWriter& m) {
  Tag(field, 2);
  Raw(m.data_.size());
  data_.append(m.data_);
}

bool PbReader::ReadVarint(uint64_t* v) {
  *v = 0;
  for (int shift = 0; shift < 64; shift += 7) {
    if (pos_ >= data_.size()) return false;
    const uint8_t b = static_cast<uint8_t>(data_[pos_++]);
    *v |= static_cast<uint64_t>(b & 0x7f) << shift;
    if (!(b & 0x80)) return true;
  }
  return false;
}

bool PbReader::Next() {
  if (!ok_ || pos_ >= data_.size()) return false;
  uint64_t tag = 0;
  if (!ReadVarint(&tag) || (tag >> 3) == 0 || (tag >> 3) > (1u << 29)) return ok_ = false;
  field_ = static_cast<int>(tag >> 3);
  wire_ = static_cast<int>(tag & 7);
  switch (wire_) {
    case 0:
      if (!ReadVarint(&varint_)) return ok_ = false;
      return true;
    case 1:  // fixed64
      if (data_.size() - pos_ < 8) return ok_ = false;
      pos_ += 8;
      return true;
    case 2: {
      uint64_t n = 0;
      if (!ReadVarint(&n) || n > data_.size() - pos_) return ok_ = false;
      bytes_ = data_.substr(pos_, n);
      pos_ += n;
      return true;
    }
    case 5:  // fixed32
      if (data_.size() - pos_ < 4) return ok_ = false;
      pos_ += 4;
      return true;
    default:  // Groups (3, 4) are not used by proto3.
      return ok_ = false;
  }
}

// --- Messages ----------------------------------------------------------------------------------------------------------

namespace {

std::string Str(std::string_view v) { return std::string(v); }

PbWriter SessionDescriptionMsg(const SessionDescription& sd) {
  PbWriter w;
  w.Bytes(1, sd.type);
  w.Bytes(2, sd.sdp);
  w.Varint(3, sd.id);
  for (const auto& [mid, track] : sd.mid_to_track_id) {  // map<string, string>: entries {key = 1, value = 2}.
    PbWriter e;
    e.Bytes(1, mid);
    e.Bytes(2, track);
    w.Message(4, e);
  }
  return w;
}

SessionDescription ReadSessionDescription(std::string_view data) {
  SessionDescription sd;
  PbReader r(data);
  while (r.Next()) {
    if (r.field() == 1 && r.is_bytes()) sd.type = Str(r.bytes());
    else if (r.field() == 2 && r.is_bytes()) sd.sdp = Str(r.bytes());
    else if (r.field() == 3 && r.is_varint()) sd.id = static_cast<uint32_t>(r.varint());
    else if (r.field() == 4 && r.is_bytes()) {
      std::string k, v;
      PbReader e(r.bytes());
      while (e.Next()) {
        if (e.field() == 1 && e.is_bytes()) k = Str(e.bytes());
        else if (e.field() == 2 && e.is_bytes()) v = Str(e.bytes());
      }
      sd.mid_to_track_id[k] = v;
    }
  }
  return sd;
}

PbWriter TrickleMsg(const TrickleRequest& t) {
  PbWriter w;
  w.Bytes(1, t.candidate_init);
  w.Varint(2, static_cast<uint64_t>(t.target));
  w.Bool(3, t.final);
  return w;
}

TrickleRequest ReadTrickle(std::string_view data) {
  TrickleRequest t;
  PbReader r(data);
  while (r.Next()) {
    if (r.field() == 1 && r.is_bytes()) t.candidate_init = Str(r.bytes());
    else if (r.field() == 2 && r.is_varint()) t.target = static_cast<SignalTarget>(r.varint() & 1);
    else if (r.field() == 3 && r.is_varint()) t.final = r.varint() != 0;
  }
  return t;
}

PbWriter LeaveMsg(const LeaveRequest& l) {
  PbWriter w;
  w.Bool(1, l.can_reconnect);
  w.Varint(2, static_cast<uint64_t>(l.reason));
  w.Varint(3, static_cast<uint64_t>(l.action));
  return w;
}

LeaveRequest ReadLeave(std::string_view data) {
  LeaveRequest l;
  PbReader r(data);
  while (r.Next()) {
    if (r.field() == 1 && r.is_varint()) l.can_reconnect = r.varint() != 0;
    else if (r.field() == 2 && r.is_varint()) l.reason = static_cast<int>(r.varint());
    else if (r.field() == 3 && r.is_varint()) l.action = static_cast<int>(r.varint());
  }
  return l;
}

TrackInfo ReadTrackInfo(std::string_view data) {
  TrackInfo t;
  PbReader r(data);
  while (r.Next()) {
    switch (r.field()) {
      case 1: if (r.is_bytes()) t.sid = Str(r.bytes()); break;
      case 2: if (r.is_varint()) t.type = static_cast<TrackType>(r.varint() % 3); break;
      case 3: if (r.is_bytes()) t.name = Str(r.bytes()); break;
      case 4: if (r.is_varint()) t.muted = r.varint() != 0; break;
      case 5: if (r.is_varint()) t.width = static_cast<uint32_t>(r.varint()); break;
      case 6: if (r.is_varint()) t.height = static_cast<uint32_t>(r.varint()); break;
      case 9: if (r.is_varint()) t.source = static_cast<TrackSource>(r.varint() % 5); break;
      case 11: if (r.is_bytes()) t.mime_type = Str(r.bytes()); break;
      case 12: if (r.is_bytes()) t.mid = Str(r.bytes()); break;
      default: break;
    }
  }
  return t;
}

PbWriter TrackInfoMsg(const TrackInfo& t) {
  PbWriter w;
  w.Bytes(1, t.sid);
  w.Varint(2, static_cast<uint64_t>(t.type));
  w.Bytes(3, t.name);
  w.Bool(4, t.muted);
  w.Varint(5, t.width);
  w.Varint(6, t.height);
  w.Varint(9, static_cast<uint64_t>(t.source));
  w.Bytes(11, t.mime_type);
  w.Bytes(12, t.mid);
  return w;
}

ParticipantInfo ReadParticipant(std::string_view data) {
  ParticipantInfo p;
  PbReader r(data);
  while (r.Next()) {
    if (r.field() == 1 && r.is_bytes()) p.sid = Str(r.bytes());
    else if (r.field() == 2 && r.is_bytes()) p.identity = Str(r.bytes());
    else if (r.field() == 3 && r.is_varint()) p.state = static_cast<ParticipantInfo::State>(r.varint() & 3);
    else if (r.field() == 4 && r.is_bytes()) p.tracks.push_back(ReadTrackInfo(r.bytes()));
    else if (r.field() == 9 && r.is_bytes()) p.name = Str(r.bytes());
  }
  return p;
}

PbWriter ParticipantMsg(const ParticipantInfo& p) {
  PbWriter w;
  w.Bytes(1, p.sid);
  w.Bytes(2, p.identity);
  w.Varint(3, static_cast<uint64_t>(p.state));
  for (const auto& t : p.tracks) w.Message(4, TrackInfoMsg(t));
  w.Bytes(9, p.name);
  return w;
}

JoinResponse ReadJoin(std::string_view data) {
  JoinResponse j;
  PbReader r(data);
  while (r.Next()) {
    switch (r.field()) {
      case 1:  // Room
        if (r.is_bytes()) {
          PbReader room(r.bytes());
          while (room.Next()) {
            if (room.field() == 1 && room.is_bytes()) j.room_sid = Str(room.bytes());
            else if (room.field() == 2 && room.is_bytes()) j.room_name = Str(room.bytes());
          }
        }
        break;
      case 2: if (r.is_bytes()) j.participant = ReadParticipant(r.bytes()); break;
      case 3: if (r.is_bytes()) j.other_participants.push_back(ReadParticipant(r.bytes())); break;
      case 4: if (r.is_bytes()) j.server_version = Str(r.bytes()); break;
      case 5:
        if (r.is_bytes()) {
          ICEServer s;
          PbReader ice(r.bytes());
          while (ice.Next()) {
            if (ice.field() == 1 && ice.is_bytes()) s.urls.push_back(Str(ice.bytes()));
            else if (ice.field() == 2 && ice.is_bytes()) s.username = Str(ice.bytes());
            else if (ice.field() == 3 && ice.is_bytes()) s.credential = Str(ice.bytes());
          }
          j.ice_servers.push_back(std::move(s));
        }
        break;
      case 6: if (r.is_varint()) j.subscriber_primary = r.varint() != 0; break;
      case 10: if (r.is_varint()) j.ping_timeout_s = static_cast<int32_t>(r.varint()); break;
      case 11: if (r.is_varint()) j.ping_interval_s = static_cast<int32_t>(r.varint()); break;
      case 12:  // ServerInfo: version = 2
        if (r.is_bytes()) {
          PbReader si(r.bytes());
          while (si.Next()) {
            if (si.field() == 2 && si.is_bytes() && j.server_version.empty()) j.server_version = Str(si.bytes());
          }
        }
        break;
      case 15: if (r.is_varint()) j.fast_publish = r.varint() != 0; break;
      default: break;
    }
  }
  return j;
}

std::string Request(int field, const PbWriter& m) {
  PbWriter w;
  w.Message(field, m);
  return w.data();
}

}  // namespace

std::string EncodeOffer(const SessionDescription& sd) { return Request(1, SessionDescriptionMsg(sd)); }
std::string EncodeAnswer(const SessionDescription& sd) { return Request(2, SessionDescriptionMsg(sd)); }
std::string EncodeTrickle(const TrickleRequest& t) { return Request(3, TrickleMsg(t)); }

std::string EncodeAddTrack(const AddTrackRequest& a) {
  PbWriter w;
  w.Bytes(1, a.cid);
  w.Bytes(2, a.name);
  w.Varint(3, static_cast<uint64_t>(a.type));
  w.Varint(4, a.width);
  w.Varint(5, a.height);
  w.Bool(6, a.muted);
  w.Varint(8, static_cast<uint64_t>(a.source));
  for (const VideoLayer& l : a.layers) {
    PbWriter v;
    v.Varint(1, static_cast<uint64_t>(l.quality));
    v.Varint(2, l.width);
    v.Varint(3, l.height);
    v.Varint(4, l.bitrate);
    v.Varint(5, l.ssrc);
    w.Message(9, v);
  }
  w.Bool(13, a.disable_red);
  return Request(4, w);
}

std::string EncodeMute(const std::string& sid, bool muted) {
  PbWriter w;
  w.Bytes(1, sid);
  w.Bool(2, muted);
  return Request(5, w);
}

std::string EncodeLeave(const LeaveRequest& l) { return Request(8, LeaveMsg(l)); }

std::string EncodePing(int64_t timestamp_ms, int64_t rtt_ms) {
  PbWriter w;
  w.Varint(1, static_cast<uint64_t>(timestamp_ms));
  w.Varint(2, static_cast<uint64_t>(rtt_ms));
  return Request(16, w);
}

SignalResponse DecodeSignalResponse(std::string_view data) {
  SignalResponse out;
  PbReader r(data);
  while (r.Next()) {  // A oneof: the last member wins.
    out.field = r.field();
    const std::string_view b = r.is_bytes() ? r.bytes() : std::string_view();
    switch (r.field()) {
      case 1: out.kind = SignalResponse::Kind::kJoin; out.join = ReadJoin(b); break;
      case 2: out.kind = SignalResponse::Kind::kAnswer; out.description = ReadSessionDescription(b); break;
      case 3: out.kind = SignalResponse::Kind::kOffer; out.description = ReadSessionDescription(b); break;
      case 4: out.kind = SignalResponse::Kind::kTrickle; out.trickle = ReadTrickle(b); break;
      case 5: {
        out.kind = SignalResponse::Kind::kUpdate;
        out.participants.clear();
        PbReader u(b);
        while (u.Next()) {
          if (u.field() == 1 && u.is_bytes()) out.participants.push_back(ReadParticipant(u.bytes()));
        }
        break;
      }
      case 6: {
        out.kind = SignalResponse::Kind::kTrackPublished;
        PbReader t(b);
        while (t.Next()) {
          if (t.field() == 1 && t.is_bytes()) out.track_published.cid = Str(t.bytes());
          else if (t.field() == 2 && t.is_bytes()) out.track_published.track = ReadTrackInfo(t.bytes());
        }
        break;
      }
      case 8: out.kind = SignalResponse::Kind::kLeave; out.leave = ReadLeave(b); break;
      case 9: out.kind = SignalResponse::Kind::kMute; break;
      case 16: out.kind = SignalResponse::Kind::kRefreshToken; out.text = Str(b); break;
      case 17: {
        out.kind = SignalResponse::Kind::kTrackUnpublished;
        PbReader t(b);
        while (t.Next()) {
          if (t.field() == 1 && t.is_bytes()) out.text = Str(t.bytes());
        }
        break;
      }
      case 18:
        out.kind = SignalResponse::Kind::kPong;
        out.pong.last_ping_timestamp = static_cast<int64_t>(r.varint());
        break;
      case 20: {
        out.kind = SignalResponse::Kind::kPongResp;
        PbReader p(b);
        while (p.Next()) {
          if (p.field() == 1 && p.is_varint()) out.pong.last_ping_timestamp = static_cast<int64_t>(p.varint());
          else if (p.field() == 2 && p.is_varint()) out.pong.timestamp = static_cast<int64_t>(p.varint());
        }
        break;
      }
      default: out.kind = SignalResponse::Kind::kOther; break;
    }
  }
  if (!r.ok() || out.field == 0) out.kind = SignalResponse::Kind::kInvalid;
  return out;
}

std::string EncodeSignalResponseForTest(const SignalResponse& r) {
  PbWriter m;
  switch (r.kind) {
    case SignalResponse::Kind::kJoin: {
      const JoinResponse& j = r.join;
      PbWriter room;
      room.Bytes(1, j.room_sid);
      room.Bytes(2, j.room_name);
      m.Message(1, room);
      m.Message(2, ParticipantMsg(j.participant));
      for (const auto& p : j.other_participants) m.Message(3, ParticipantMsg(p));
      m.Bytes(4, j.server_version);
      for (const auto& s : j.ice_servers) {
        PbWriter ice;
        for (const auto& u : s.urls) ice.Bytes(1, u);
        ice.Bytes(2, s.username);
        ice.Bytes(3, s.credential);
        m.Message(5, ice);
      }
      m.Bool(6, j.subscriber_primary);
      m.Varint(10, static_cast<uint64_t>(j.ping_timeout_s));
      m.Varint(11, static_cast<uint64_t>(j.ping_interval_s));
      m.Bool(15, j.fast_publish);
      return Request(1, m);
    }
    case SignalResponse::Kind::kAnswer: return Request(2, SessionDescriptionMsg(r.description));
    case SignalResponse::Kind::kOffer: return Request(3, SessionDescriptionMsg(r.description));
    case SignalResponse::Kind::kTrickle: return Request(4, TrickleMsg(r.trickle));
    case SignalResponse::Kind::kUpdate:
      for (const auto& p : r.participants) m.Message(1, ParticipantMsg(p));
      return Request(5, m);
    case SignalResponse::Kind::kTrackPublished:
      m.Bytes(1, r.track_published.cid);
      m.Message(2, TrackInfoMsg(r.track_published.track));
      return Request(6, m);
    case SignalResponse::Kind::kLeave: return Request(8, LeaveMsg(r.leave));
    case SignalResponse::Kind::kPongResp:
      m.Varint(1, static_cast<uint64_t>(r.pong.last_ping_timestamp));
      m.Varint(2, static_cast<uint64_t>(r.pong.timestamp));
      return Request(20, m);
    default: return {};
  }
}

std::optional<SignalRequestForTest> DecodeSignalRequestForTest(std::string_view data) {
  SignalRequestForTest out;
  PbReader r(data);
  while (r.Next()) {
    if (!r.is_bytes()) continue;
    out.field = r.field();
    switch (r.field()) {
      case 1: case 2: out.description = ReadSessionDescription(r.bytes()); break;
      case 3: out.trickle = ReadTrickle(r.bytes()); break;
      case 4: {
        PbReader a(r.bytes());
        while (a.Next()) {
          switch (a.field()) {
            case 1: out.add_track.cid = Str(a.bytes()); break;
            case 2: out.add_track.name = Str(a.bytes()); break;
            case 3: out.add_track.type = static_cast<TrackType>(a.varint() % 3); break;
            case 4: out.add_track.width = static_cast<uint32_t>(a.varint()); break;
            case 5: out.add_track.height = static_cast<uint32_t>(a.varint()); break;
            case 8: out.add_track.source = static_cast<TrackSource>(a.varint() % 5); break;
            case 9: {
              VideoLayer l;
              PbReader v(a.bytes());
              while (v.Next()) {
                if (v.field() == 1) l.quality = static_cast<VideoQuality>(v.varint() & 3);
                else if (v.field() == 2) l.width = static_cast<uint32_t>(v.varint());
                else if (v.field() == 3) l.height = static_cast<uint32_t>(v.varint());
                else if (v.field() == 4) l.bitrate = static_cast<uint32_t>(v.varint());
              }
              out.add_track.layers.push_back(l);
              break;
            }
            case 13: out.add_track.disable_red = a.varint() != 0; break;
            default: break;
          }
        }
        break;
      }
      case 8: out.leave = ReadLeave(r.bytes()); break;
      case 16: {
        PbReader p(r.bytes());
        while (p.Next()) {
          if (p.field() == 1) out.ping_timestamp = static_cast<int64_t>(p.varint());
          else if (p.field() == 2) out.ping_rtt = static_cast<int64_t>(p.varint());
        }
        break;
      }
      default: break;
    }
  }
  if (!r.ok() || out.field == 0) return std::nullopt;
  return out;
}

std::string JoinPath(std::string_view base_path, std::string_view token, bool auto_subscribe) {
  std::string path(base_path.empty() ? "/" : base_path);
  if (path.back() != '/') path += '/';
  path += "rtc?access_token=";
  for (unsigned char c : token) {  // A JWT is base64url and dots; escape anything else.
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      path += static_cast<char>(c);
    } else {
      char b[4];
      std::snprintf(b, sizeof b, "%%%02X", c);
      path += b;
    }
  }
  path += std::string("&auto_subscribe=") + (auto_subscribe ? "1" : "0");
  path += "&protocol=" + std::to_string(kProtocolVersion) + "&adaptive_stream=0";  // No sdk: ClientInfo UNKNOWN.
  return path;
}

}  // namespace sm::apps::livekit
