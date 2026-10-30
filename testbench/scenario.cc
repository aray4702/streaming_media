#include "scenario.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace sm::testbench {
namespace {

std::vector<std::string> Split(const std::string& line) {
  std::vector<std::string> out;
  std::istringstream in(line);
  std::string tok;
  while (in >> tok) out.push_back(tok);
  return out;
}

std::string Lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// Parses "<number><suffix>" and returns the number and lower-cased suffix.
bool SplitNumber(const std::string& s, double* v, std::string* unit) {
  if (s.empty()) return false;
  const char* begin = s.c_str();
  char* end = nullptr;
  *v = std::strtod(begin, &end);
  if (end == begin || !std::isfinite(*v)) return false;
  *unit = Lower(std::string(end));
  return true;
}

bool ParseTime(const std::string& s, TimeDelta* out) {
  double v;
  std::string u;
  if (!SplitNumber(s, &v, &u) || v < 0) return false;
  if (u == "ms") *out = TimeDelta::MillisF(v);
  else if (u == "s" || u.empty()) *out = TimeDelta::SecondsF(v);
  else if (u == "us") *out = TimeDelta::Micros(static_cast<int64_t>(v));
  else if (u == "min") *out = TimeDelta::SecondsF(v * 60);
  else return false;
  return true;
}

bool ParseRate(const std::string& s, DataRate* out) {
  std::string l = Lower(s);
  if (l == "inf" || l == "unlimited") {
    *out = DataRate::PlusInfinity();
    return true;
  }
  double v;
  std::string u;
  if (!SplitNumber(s, &v, &u) || v < 0) return false;
  double bps;
  if (u == "bps") bps = v;
  else if (u == "kbps" || u == "k") bps = v * 1e3;
  else if (u == "mbps" || u == "m") bps = v * 1e6;
  else return false;
  *out = DataRate::BitsPerSec(static_cast<int64_t>(std::llround(bps)));
  return true;
}

// "2%" -> 0.02, "0.02" -> 0.02.
bool ParseFraction(const std::string& s, double* out) {
  double v;
  std::string u;
  if (!SplitNumber(s, &v, &u) || v < 0) return false;
  if (u == "%") v /= 100;
  else if (!u.empty()) return false;
  if (v > 1) return false;
  *out = v;
  return true;
}

bool ParseBool(const std::string& s, bool* out) {
  std::string l = Lower(s);
  if (l == "on" || l == "true" || l == "yes" || l == "1") *out = true;
  else if (l == "off" || l == "false" || l == "no" || l == "0") *out = false;
  else return false;
  return true;
}

std::string FormatTime(TimeDelta t) {
  char buf[32];
  if (t.us() % 1000000 == 0) std::snprintf(buf, sizeof(buf), "%llds", static_cast<long long>(t.us() / 1000000));
  else if (t.us() % 1000 == 0) std::snprintf(buf, sizeof(buf), "%lldms", static_cast<long long>(t.ms()));
  else std::snprintf(buf, sizeof(buf), "%lldus", static_cast<long long>(t.us()));
  return buf;
}

std::string FormatRate(DataRate r) {
  if (!r.IsFinite()) return "inf";
  char buf[32];
  if (r.bps() % 1000 == 0) std::snprintf(buf, sizeof(buf), "%lldkbps", static_cast<long long>(r.bps() / 1000));
  else std::snprintf(buf, sizeof(buf), "%lldbps", static_cast<long long>(r.bps()));
  return buf;
}

std::string FormatNumber(double v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.10g", v);
  return buf;
}

std::string FormatPercent(double v) { return FormatNumber(v * 100) + "%"; }

// Parses "at <time> key=value... [down|up]" starting at tokens[i].
bool ParseChange(const std::vector<std::string>& tok, size_t i, LinkChange* c, std::string* err) {
  if (i + 1 >= tok.size() || Lower(tok[i]) != "at") {
    *err = "expected 'at <time> ...'";
    return false;
  }
  if (!ParseTime(tok[i + 1], &c->at)) {
    *err = "bad time '" + tok[i + 1] + "'";
    return false;
  }
  for (size_t k = i + 2; k < tok.size(); ++k) {
    std::string t = Lower(tok[k]);
    if (t == "down") {
      c->down = true;
      continue;
    }
    if (t == "up") {
      c->down = false;
      continue;
    }
    size_t eq = tok[k].find('=');
    if (eq == std::string::npos) {
      *err = "expected key=value, got '" + tok[k] + "'";
      return false;
    }
    std::string key = Lower(tok[k].substr(0, eq));
    std::string val = tok[k].substr(eq + 1);
    bool ok = true;
    if (key == "capacity" || key == "bw") {
      DataRate r;
      ok = ParseRate(val, &r);
      c->capacity = r;
    } else if (key == "delay") {
      TimeDelta d;
      ok = ParseTime(val, &d);
      c->delay = d;
    } else if (key == "jitter") {
      TimeDelta d;
      ok = ParseTime(val, &d);
      c->jitter = d;
    } else if (key == "loss") {
      double f;
      ok = ParseFraction(val, &f);
      c->loss = f;
    } else if (key == "burst") {
      double v;
      std::string u;
      ok = SplitNumber(val, &v, &u) && u.empty() && v >= 1;
      c->burst_len = v;
    } else if (key == "dup" || key == "duplicate") {
      double f;
      ok = ParseFraction(val, &f);
      c->duplicate = f;
    } else if (key == "reorder") {
      bool b;
      ok = ParseBool(val, &b);
      c->reorder = b;
    } else if (key == "queue") {
      TimeDelta d;
      ok = ParseTime(val, &d);
      c->queue = d;
    } else if (key == "cross") {
      DataRate r;
      ok = ParseRate(val, &r) && r.IsFinite();
      c->cross = r;
    } else {
      *err = "unknown link key '" + key + "'";
      return false;
    }
    if (!ok) {
      *err = "bad value for " + key + ": '" + val + "'";
      return false;
    }
  }
  return true;
}

std::string FormatChange(const LinkChange& c) {
  std::string s = "at " + FormatTime(c.at);
  if (c.down) s += *c.down ? " down" : " up";
  if (c.capacity) s += " capacity=" + FormatRate(*c.capacity);
  if (c.delay) s += " delay=" + FormatTime(*c.delay);
  if (c.jitter) s += " jitter=" + FormatTime(*c.jitter);
  if (c.loss) s += " loss=" + FormatPercent(*c.loss);
  if (c.burst_len) s += " burst=" + FormatNumber(*c.burst_len);
  if (c.duplicate) s += " dup=" + FormatPercent(*c.duplicate);
  if (c.reorder) s += std::string(" reorder=") + (*c.reorder ? "on" : "off");
  if (c.queue) s += " queue=" + FormatTime(*c.queue);
  if (c.cross) s += " cross=" + FormatRate(*c.cross);
  return s;
}

}  // namespace

const char* ToString(CompareOp op) {
  switch (op) {
    case CompareOp::kLt: return "<";
    case CompareOp::kLe: return "<=";
    case CompareOp::kGt: return ">";
    case CompareOp::kGe: return ">=";
    case CompareOp::kEq: return "==";
  }
  return "?";
}

bool Evaluate(CompareOp op, double lhs, double rhs) {
  switch (op) {
    case CompareOp::kLt: return lhs < rhs;
    case CompareOp::kLe: return lhs <= rhs;
    case CompareOp::kGt: return lhs > rhs;
    case CompareOp::kGe: return lhs >= rhs;
    case CompareOp::kEq: return std::fabs(lhs - rhs) <= 1e-9 * std::max(1.0, std::fabs(rhs));
  }
  return false;
}

ParseResult ParseScenario(const std::string& text) {
  Scenario s;
  std::istringstream in(text);
  std::string line;
  int n = 0;
  auto fail = [&](const std::string& msg) {
    ParseResult r;
    r.error = "line " + std::to_string(n) + ": " + msg;
    return r;
  };
  while (std::getline(in, line)) {
    ++n;
    size_t hash = line.find('#');
    if (hash != std::string::npos) line.resize(hash);
    auto tok = Split(line);
    if (tok.empty()) continue;
    std::string key = Lower(tok[0]);
    auto rest = [&]() {
      size_t p = line.find(tok[0]) + tok[0].size();
      size_t b = line.find_first_not_of(" \t", p);
      size_t e = line.find_last_not_of(" \t\r");
      return b == std::string::npos ? std::string() : line.substr(b, e - b + 1);
    };
    auto need = [&](size_t count) { return tok.size() == count; };

    if (key == "name") {
      if (!need(2)) return fail("usage: name <identifier>");
      s.name = tok[1];
    } else if (key == "describe" || key == "description") {
      s.description = rest();
    } else if (key == "duration") {
      if (!need(2) || !ParseTime(tok[1], &s.duration) || s.duration.us() <= 0) return fail("usage: duration <time>");
      if (s.duration > TimeDelta::Seconds(3600)) return fail("duration is limited to 1 hour");
    } else if (key == "seed") {
      if (!need(2)) return fail("usage: seed <integer>");
      s.seed = std::strtoull(tok[1].c_str(), nullptr, 10);
    } else if (key == "compare_seeds") {
      int v = need(2) ? std::atoi(tok[1].c_str()) : 0;
      if (v < 1 || v > 200) return fail("usage: compare_seeds <1..200> (the RTCP interval seed)");
      s.compare_seeds = v;
    } else if (key == "compare_clock_start") {
      const long long v = need(2) ? std::atoll(tok[1].c_str()) : 0;
      if (v < 1 || v > 1000000) return fail("usage: compare_clock_start <1..1000000 seconds> (the RTCP interval seed)");
      s.compare_clock_start_s = v;
    } else if (key == "call") {
      if (!need(2) || (tok[1] != "one-way" && tok[1] != "two-way")) return fail("usage: call one-way|two-way");
      s.two_way = tok[1] == "two-way";
    } else if (key == "audio" || key == "video" || key == "red" || key == "nack" || key == "fec") {
      bool b;
      if (!need(2) || !ParseBool(tok[1], &b)) return fail("usage: " + key + " on|off");
      (key == "audio" ? s.audio : key == "video" ? s.video : key == "red" ? s.red : key == "nack" ? s.nack : s.fec) = b;
    } else if (key == "audio_rate") {
      DataRate r;
      if (!need(2) || !ParseRate(tok[1], &r) || !r.IsFinite() || r.bps() < 6000) return fail("usage: audio_rate <rate >= 6kbps>");
      s.audio_bitrate = r;
    } else if (key == "audio_sample_rate") {
      int v = need(2) ? std::atoi(tok[1].c_str()) : 0;
      // The four NetEq rates, plus 12, 24 and 44.1 kHz (issue #256), which the bench encodes at Opus's 48 kHz (#298).
      if (v != 8000 && v != 12000 && v != 16000 && v != 24000 && v != 32000 && v != 44100 && v != 48000)
        return fail("audio_sample_rate must be 8000|12000|16000|24000|32000|44100|48000");
      s.audio_sample_rate = v;
    } else if (key == "video_fps") {
      int v = need(2) ? std::atoi(tok[1].c_str()) : 0;
      if (v < 1 || v > 120) return fail("video_fps must be 1..120");
      s.video_fps = v;
    } else if (key == "video_size") {
      int w = 0, h = 0;
      char x = 0, extra = 0;
      if (!need(2) || std::sscanf(tok[1].c_str(), "%d%c%d%c", &w, &x, &h, &extra) != 3 || x != 'x' || w < 16 ||
          h < 16 || w > 7680 || h > 4320 || w % 2 || h % 2)
        return fail("usage: video_size <W>x<H> (even, 16..7680 x 16..4320)");
      s.video_width = w;
      s.video_height = h;
    } else if (key == "video_complexity" || key == "encoder_speed") {
      char* end = nullptr;
      const double v = need(2) ? std::strtod(tok[1].c_str(), &end) : 0;
      if (!need(2) || end == tok[1].c_str() || *end != '\0' || !(v >= 0.01 && v <= 100))
        return fail("usage: " + key + " <0.01..100>");
      (key == "video_complexity" ? s.video_complexity : s.encoder_speed) = v;
    } else if (key == "encoder_overshoot") {
      char* end = nullptr;
      const double v = need(2) ? std::strtod(tok[1].c_str(), &end) : 0;
      if (!need(2) || end == tok[1].c_str() || *end != '\0' || !(v >= 0.1 && v <= 10))
        return fail("usage: encoder_overshoot <0.1..10>");
      s.encoder_overshoot = v;
    } else if (key == "scene_change") {
      TimeDelta interval;
      char* end1 = nullptr;
      char* end2 = nullptr;
      const long frames = need(4) ? std::strtol(tok[2].c_str(), &end1, 10) : 0;
      const double f = need(4) ? std::strtod(tok[3].c_str(), &end2) : 0;
      if (!need(4) || !ParseTime(tok[1], &interval) || interval <= TimeDelta::Zero() || end1 == tok[2].c_str() ||
          *end1 != '\0' || frames < 1 || frames > 1000 || end2 == tok[3].c_str() || *end2 != '\0' || !(f >= 0.1 && f <= 100))
        return fail("usage: scene_change <interval> <frames 1..1000> <factor 0.1..100>");
      s.scene_change_interval = interval;
      s.scene_change_frames = static_cast<int>(frames);
      s.scene_change_factor = f;
    } else if (key == "video_max_payload") {
      // Any value reaches the library, which clamps it (issue #257): the bench tests that.
      char* end = nullptr;
      const long v = need(2) ? std::strtol(tok[1].c_str(), &end, 10) : -1;
      if (!need(2) || end == tok[1].c_str() || *end != '\0' || v < 0 || v > 65535)
        return fail("usage: video_max_payload <bytes 0..65535>");
      s.video_max_payload = static_cast<size_t>(v);
    } else if (key == "video_max" || key == "video_min" || key == "start_rate" || key == "min_rate" || key == "max_rate") {
      DataRate r;
      if (!need(2) || !ParseRate(tok[1], &r) || !r.IsFinite() || r.bps() <= 0) return fail("usage: " + key + " <rate>");
      (key == "video_max" ? s.video_max : key == "video_min" ? s.video_min : key == "start_rate" ? s.start_rate
       : key == "min_rate" ? s.min_rate : s.max_rate) = r;
    } else if (key == "timing") {
      if (!need(2) || (tok[1] != "v1" && tok[1] != "v2")) return fail("usage: timing v1|v2");
      s.timing_v2 = tok[1] == "v2";
    } else if (key == "neteq") {
      if (!need(2) || (tok[1] != "v1" && tok[1] != "v2")) return fail("usage: neteq v1|v2");
      s.neteq_v2 = tok[1] == "v2";
    } else if (key == "av_sync") {
      if (!need(2) || (tok[1] != "on" && tok[1] != "off")) return fail("usage: av_sync on|off");
      s.av_sync = tok[1] == "on";
    } else if (key == "audio_receive_rtcp") {
      if (!need(2) || (tok[1] != "on" && tok[1] != "off")) return fail("usage: audio_receive_rtcp on|off");
      s.audio_receive_rtcp = tok[1] == "on";
    } else if (key == "send_rate_stats") {
      if (!need(2) || (tok[1] != "on" && tok[1] != "off")) return fail("usage: send_rate_stats on|off");
      s.send_rate_stats = tok[1] == "on";
    } else if (key == "link_headroom") {
      if (!need(2) || (tok[1] != "on" && tok[1] != "off")) return fail("usage: link_headroom on|off");
      s.link_headroom = tok[1] == "on";
    } else if (key == "control_handler") {
      if (!need(2) || (tok[1] != "on" && tok[1] != "off")) return fail("usage: control_handler on|off");
      s.control_handler = tok[1] == "on";
    } else if (key == "bitrate_adjuster") {
      if (!need(2) || (tok[1] != "on" && tok[1] != "off")) return fail("usage: bitrate_adjuster on|off");
      s.bitrate_adjuster = tok[1] == "on";
    } else if (key == "rate_utilization_tracker") {
      if (!need(2) || (tok[1] != "on" && tok[1] != "off")) return fail("usage: rate_utilization_tracker on|off");
      s.rate_utilization_tracker = tok[1] == "on";
    } else if (key == "adjuster_on_update") {
      if (!need(2) || (tok[1] != "on" && tok[1] != "off")) return fail("usage: adjuster_on_update on|off");
      s.adjuster_on_update = tok[1] == "on";
    } else if (key == "per_stream_rtt") {
      if (!need(2) || (tok[1] != "on" && tok[1] != "off")) return fail("usage: per_stream_rtt on|off");
      s.per_stream_rtt = tok[1] == "on";
    } else if (key == "receive_statistics_v2") {
      if (!need(2) || (tok[1] != "on" && tok[1] != "off")) return fail("usage: receive_statistics_v2 on|off");
      s.receive_statistics_v2 = tok[1] == "on";
    } else if (key == "expected_packet_overhead") {
      if (!need(2) || (tok[1] != "on" && tok[1] != "off")) return fail("usage: expected_packet_overhead on|off");
      s.expected_packet_overhead = tok[1] == "on";
    } else if (key == "sps_pps_tracker") {
      if (!need(2) || (tok[1] != "on" && tok[1] != "off")) return fail("usage: sps_pps_tracker on|off");
      s.sps_pps_tracker = tok[1] == "on";
    } else if (key == "render_smoothing") {
      if (!need(2) || (tok[1] != "on" && tok[1] != "off")) return fail("usage: render_smoothing on|off");
      s.render_smoothing = tok[1] == "on";
    } else if (key == "keyframe_flush_padding") {
      if (!need(2) || (tok[1] != "on" && tok[1] != "off")) return fail("usage: keyframe_flush_padding on|off");
      s.keyframe_flush_padding = tok[1] == "on";
    } else if (key == "h264_missing_packets") {
      if (!need(2) || (tok[1] != "on" && tok[1] != "off")) return fail("usage: h264_missing_packets on|off");
      s.h264_missing_packets = tok[1] == "on";
    } else if (key == "encode_delay") {
      if (!need(2) || (tok[1] != "on" && tok[1] != "off")) return fail("usage: encode_delay on|off");
      s.encode_delay = tok[1] == "on";
    } else if (key == "quality_scaling") {
      if (!need(2) || (tok[1] != "on" && tok[1] != "off")) return fail("usage: quality_scaling on|off");
      s.quality_scaling = tok[1] == "on";
    } else if (key == "host_rtt_inf") {
      TimeDelta t;
      if (!need(2) || !ParseTime(tok[1], &t)) return fail("usage: host_rtt_inf <time>");
      s.host_rtt_inf = t;
    } else if (key == "apm_rate") {
      char* end = nullptr;
      const long v = need(2) ? std::strtol(tok[1].c_str(), &end, 10) : 0;
      if (!need(2) || *end != '\0' || v < -384000 || v > 384000) return fail("usage: apm_rate <hz>");
      s.apm_rate = static_cast<int>(v);
    } else if (key == "host_rtt") {
      TimeDelta t, rtt;
      if (!need(3) || !ParseTime(tok[1], &t) || !ParseTime(tok[2], &rtt)) return fail("usage: host_rtt <time> <rtt>");
      s.host_rtt = std::make_pair(t, rtt);
    } else if (key == "keyframe_interval") {
      if (!need(2) || !ParseTime(tok[1], &s.keyframe_interval)) return fail("usage: keyframe_interval <time>");
    } else if (key == "at") {
      LinkChange c;
      std::string err;
      if (!ParseChange(tok, 0, &c, &err)) return fail(err);
      s.forward.push_back(c);
    } else if (key == "reverse") {
      LinkChange c;
      std::string err;
      if (!ParseChange(tok, 1, &c, &err)) return fail(err);
      s.reverse.push_back(c);
    } else if (key == "expect") {
      if (!need(4)) return fail("usage: expect <metric> <|<=|>|>=|== <number>");
      Expectation e;
      e.metric = tok[1];
      const std::string& op = tok[2];
      if (op == "<") e.op = CompareOp::kLt;
      else if (op == "<=") e.op = CompareOp::kLe;
      else if (op == ">") e.op = CompareOp::kGt;
      else if (op == ">=") e.op = CompareOp::kGe;
      else if (op == "==") e.op = CompareOp::kEq;
      else return fail("unknown operator '" + op + "'");
      char* end = nullptr;
      e.value = std::strtod(tok[3].c_str(), &end);
      if (end == tok[3].c_str() || *end != '\0') return fail("bad number '" + tok[3] + "'");
      e.text = tok[1] + " " + tok[2] + " " + tok[3];
      s.expectations.push_back(e);
    } else {
      return fail("unknown directive '" + tok[0] + "'");
    }
  }
  if (s.video_min > s.video_max) {
    ParseResult r;
    r.error = "video_min exceeds video_max";
    return r;
  }
  if (s.min_rate > s.max_rate) {
    ParseResult r;
    r.error = "min_rate exceeds max_rate";
    return r;
  }
  auto by_time = [](const LinkChange& a, const LinkChange& b) { return a.at < b.at; };
  std::stable_sort(s.forward.begin(), s.forward.end(), by_time);
  std::stable_sort(s.reverse.begin(), s.reverse.end(), by_time);
  ParseResult r;
  r.scenario = s;
  return r;
}

std::string FormatScenario(const Scenario& s) {
  Scenario d;  // Defaults; only differences are written.
  std::string o;
  o += "name " + s.name + "\n";
  if (!s.description.empty()) o += "describe " + s.description + "\n";
  o += "duration " + FormatTime(s.duration) + "\n";
  o += "seed " + std::to_string(s.seed) + "\n";
  if (s.two_way != d.two_way) o += std::string("call ") + (s.two_way ? "two-way" : "one-way") + "\n";
  auto onoff = [&](const char* k, bool v, bool def) {
    if (v != def) o += std::string(k) + (v ? " on\n" : " off\n");
  };
  onoff("audio", s.audio, d.audio);
  onoff("video", s.video, d.video);
  onoff("red", s.red, d.red);
  onoff("nack", s.nack, d.nack);
  onoff("fec", s.fec, d.fec);
  auto rate = [&](const char* k, DataRate v, DataRate def) {
    if (v != def) o += std::string(k) + " " + FormatRate(v) + "\n";
  };
  rate("audio_rate", s.audio_bitrate, d.audio_bitrate);
  if (s.audio_sample_rate != d.audio_sample_rate) o += "audio_sample_rate " + std::to_string(s.audio_sample_rate) + "\n";
  if (s.video_fps != d.video_fps) o += "video_fps " + std::to_string(s.video_fps) + "\n";
  if (s.video_width != d.video_width || s.video_height != d.video_height)
    o += "video_size " + std::to_string(s.video_width) + "x" + std::to_string(s.video_height) + "\n";
  if (s.video_complexity != d.video_complexity) o += "video_complexity " + FormatNumber(s.video_complexity) + "\n";
  if (s.encoder_speed != d.encoder_speed) o += "encoder_speed " + FormatNumber(s.encoder_speed) + "\n";
  if (s.encoder_overshoot != d.encoder_overshoot) o += "encoder_overshoot " + FormatNumber(s.encoder_overshoot) + "\n";
  if (s.scene_change_interval != d.scene_change_interval)
    o += "scene_change " + FormatTime(s.scene_change_interval) + " " + std::to_string(s.scene_change_frames) + " " +
         FormatNumber(s.scene_change_factor) + "\n";
  rate("video_max", s.video_max, d.video_max);
  rate("video_min", s.video_min, d.video_min);
  rate("start_rate", s.start_rate, d.start_rate);
  rate("min_rate", s.min_rate, d.min_rate);
  rate("max_rate", s.max_rate, d.max_rate);
  if (s.keyframe_interval != d.keyframe_interval) o += "keyframe_interval " + FormatTime(s.keyframe_interval) + "\n";
  if (s.bitrate_adjuster != d.bitrate_adjuster) o += std::string("bitrate_adjuster ") + (s.bitrate_adjuster ? "on" : "off") + "\n";
  if (s.rate_utilization_tracker != d.rate_utilization_tracker)
    o += std::string("rate_utilization_tracker ") + (s.rate_utilization_tracker ? "on" : "off") + "\n";
  if (s.adjuster_on_update != d.adjuster_on_update)
    o += std::string("adjuster_on_update ") + (s.adjuster_on_update ? "on" : "off") + "\n";
  if (s.per_stream_rtt != d.per_stream_rtt) o += std::string("per_stream_rtt ") + (s.per_stream_rtt ? "on" : "off") + "\n";
  if (s.receive_statistics_v2 != d.receive_statistics_v2)
    o += std::string("receive_statistics_v2 ") + (s.receive_statistics_v2 ? "on" : "off") + "\n";
  if (s.expected_packet_overhead != d.expected_packet_overhead)
    o += std::string("expected_packet_overhead ") + (s.expected_packet_overhead ? "on" : "off") + "\n";
  if (s.sps_pps_tracker != d.sps_pps_tracker) o += std::string("sps_pps_tracker ") + (s.sps_pps_tracker ? "on" : "off") + "\n";
  if (s.render_smoothing != d.render_smoothing) o += std::string("render_smoothing ") + (s.render_smoothing ? "on" : "off") + "\n";
  if (s.keyframe_flush_padding != d.keyframe_flush_padding)
    o += std::string("keyframe_flush_padding ") + (s.keyframe_flush_padding ? "on" : "off") + "\n";
  if (s.h264_missing_packets != d.h264_missing_packets)
    o += std::string("h264_missing_packets ") + (s.h264_missing_packets ? "on" : "off") + "\n";
  if (s.encode_delay != d.encode_delay) o += std::string("encode_delay ") + (s.encode_delay ? "on" : "off") + "\n";
  if (s.control_handler != d.control_handler) o += std::string("control_handler ") + (s.control_handler ? "on" : "off") + "\n";
  if (s.send_rate_stats != d.send_rate_stats) o += std::string("send_rate_stats ") + (s.send_rate_stats ? "on" : "off") + "\n";
  if (s.link_headroom != d.link_headroom) o += std::string("link_headroom ") + (s.link_headroom ? "on" : "off") + "\n";
  if (s.quality_scaling != d.quality_scaling) o += std::string("quality_scaling ") + (s.quality_scaling ? "on" : "off") + "\n";
  if (s.timing_v2 != d.timing_v2) o += std::string("timing ") + (s.timing_v2 ? "v2" : "v1") + "\n";
  if (s.neteq_v2 != d.neteq_v2) o += std::string("neteq ") + (s.neteq_v2 ? "v2" : "v1") + "\n";
  if (s.av_sync != d.av_sync) o += std::string("av_sync ") + (s.av_sync ? "on" : "off") + "\n";
  if (s.audio_receive_rtcp != d.audio_receive_rtcp) o += std::string("audio_receive_rtcp ") + (s.audio_receive_rtcp ? "on" : "off") + "\n";
  for (const auto& c : s.forward) o += FormatChange(c) + "\n";
  for (const auto& c : s.reverse) o += "reverse " + FormatChange(c) + "\n";
  for (const auto& e : s.expectations) o += "expect " + e.metric + " " + ToString(e.op) + " " + FormatNumber(e.value) + "\n";
  return o;
}

std::string UnsupportedReason(const Scenario& s) {
  if (s.two_way) return "call two-way is not supported yet: the test bench runs one-way calls only (#743; #746 adds two-way)";
  return "";
}

}  // namespace sm::testbench
