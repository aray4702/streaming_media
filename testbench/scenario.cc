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
      if (v != 8000 && v != 16000 && v != 32000 && v != 48000) return fail("audio_sample_rate must be 8000|16000|32000|48000");
      s.audio_sample_rate = v;
    } else if (key == "video_fps") {
      int v = need(2) ? std::atoi(tok[1].c_str()) : 0;
      if (v < 1 || v > 120) return fail("video_fps must be 1..120");
      s.video_fps = v;
    } else if (key == "video_max" || key == "video_min" || key == "start_rate" || key == "min_rate" || key == "max_rate") {
      DataRate r;
      if (!need(2) || !ParseRate(tok[1], &r) || !r.IsFinite() || r.bps() <= 0) return fail("usage: " + key + " <rate>");
      (key == "video_max" ? s.video_max : key == "video_min" ? s.video_min : key == "start_rate" ? s.start_rate
       : key == "min_rate" ? s.min_rate : s.max_rate) = r;
    } else if (key == "pacer") {
      if (!need(2) || (tok[1] != "v1" && tok[1] != "v2")) return fail("usage: pacer v1|v2");
      s.pacer_v2 = tok[1] == "v2";
    } else if (key == "bitrate_adjuster") {
      if (!need(2) || (tok[1] != "on" && tok[1] != "off")) return fail("usage: bitrate_adjuster on|off");
      s.bitrate_adjuster = tok[1] == "on";
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
  rate("video_max", s.video_max, d.video_max);
  rate("video_min", s.video_min, d.video_min);
  rate("start_rate", s.start_rate, d.start_rate);
  rate("min_rate", s.min_rate, d.min_rate);
  rate("max_rate", s.max_rate, d.max_rate);
  if (s.keyframe_interval != d.keyframe_interval) o += "keyframe_interval " + FormatTime(s.keyframe_interval) + "\n";
  if (s.bitrate_adjuster != d.bitrate_adjuster) o += std::string("bitrate_adjuster ") + (s.bitrate_adjuster ? "on" : "off") + "\n";
  if (s.pacer_v2 != d.pacer_v2) o += std::string("pacer ") + (s.pacer_v2 ? "v2" : "v1") + "\n";
  for (const auto& c : s.forward) o += FormatChange(c) + "\n";
  for (const auto& c : s.reverse) o += "reverse " + FormatChange(c) + "\n";
  for (const auto& e : s.expectations) o += "expect " + e.metric + " " + ToString(e.op) + " " + FormatNumber(e.value) + "\n";
  return o;
}

}  // namespace sm::testbench
