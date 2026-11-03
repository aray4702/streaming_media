// Minimal JSON for the signaling messages (#1103): flat objects of strings, numbers, booleans and null, and arrays of
// strings. Enough for the protocol of #1102; not a general JSON library.
#pragma once

#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace sm::net {

// `s` as a quoted JSON string.
inline std::string JsonEscape(const std::string& s) {
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

namespace json_detail {
inline void SkipWs(const std::string& s, size_t& i) {
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
}
// The string starting at s[i] (its opening quote), unescaped; i ends past the closing quote.
inline std::string ReadString(const std::string& s, size_t& i) {
  std::string v;
  ++i;
  while (i < s.size() && s[i] != '"') {
    if (s[i] == '\\' && i + 1 < s.size()) {
      const char e = s[++i];
      if (e == 'n') v += '\n';
      else if (e == 'r') v += '\r';
      else if (e == 't') v += '\t';
      else if (e == 'u' && i + 4 < s.size()) {
        unsigned cp = 0;
        for (int k = 1; k <= 4; ++k) {
          const char h = s[i + static_cast<size_t>(k)];
          cp = cp * 16 + static_cast<unsigned>(std::isdigit(static_cast<unsigned char>(h)) ? h - '0' : (std::tolower(h) - 'a' + 10));
        }
        v += cp < 0x80 ? static_cast<char>(cp) : '?';
        i += 4;
      } else {
        v += e;
      }
      ++i;
    } else {
      v += s[i++];
    }
  }
  ++i;
  return v;
}
}  // namespace json_detail

// A flat JSON object: each value as its text (strings unescaped, nested objects and arrays as their raw JSON text).
// Not an object: empty.
inline std::map<std::string, std::string> JsonParseFlat(const std::string& s) {
  using namespace json_detail;
  std::map<std::string, std::string> out;
  size_t i = 0;
  SkipWs(s, i);
  if (i >= s.size() || s[i] != '{') return out;
  ++i;
  while (true) {
    SkipWs(s, i);
    if (i >= s.size() || s[i] != '"') break;
    const std::string key = ReadString(s, i);
    SkipWs(s, i);
    if (i >= s.size() || s[i] != ':') break;
    ++i;
    SkipWs(s, i);
    if (i >= s.size()) break;
    if (s[i] == '"') {
      out[key] = ReadString(s, i);
    } else if (s[i] == '{' || s[i] == '[') {
      const size_t start = i;
      int depth = 0;
      bool in_str = false;
      for (; i < s.size(); ++i) {
        if (in_str) {
          if (s[i] == '\\') ++i;
          else if (s[i] == '"') in_str = false;
        } else if (s[i] == '"') {
          in_str = true;
        } else if (s[i] == '{' || s[i] == '[') {
          ++depth;
        } else if ((s[i] == '}' || s[i] == ']') && --depth == 0) {
          ++i;
          break;
        }
      }
      out[key] = s.substr(start, i - start);
    } else {
      const size_t start = i;
      while (i < s.size() && s[i] != ',' && s[i] != '}') ++i;
      std::string v = s.substr(start, i - start);
      while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back()))) v.pop_back();
      out[key] = v;
    }
    SkipWs(s, i);
    if (i < s.size() && s[i] == ',') {
      ++i;
      continue;
    }
    break;
  }
  return out;
}

// The strings of a JSON array (other elements are skipped).
inline std::vector<std::string> JsonStringArray(const std::string& s) {
  using namespace json_detail;
  std::vector<std::string> out;
  size_t i = 0;
  SkipWs(s, i);
  if (i >= s.size() || s[i] != '[') return out;
  ++i;
  while (i < s.size()) {
    SkipWs(s, i);
    if (i >= s.size() || s[i] == ']') break;
    if (s[i] == '"') {
      out.push_back(ReadString(s, i));
    } else {
      while (i < s.size() && s[i] != ',' && s[i] != ']') ++i;
    }
    SkipWs(s, i);
    if (i < s.size() && s[i] == ',') ++i;
  }
  return out;
}

}  // namespace sm::net
