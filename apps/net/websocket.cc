// The apps' signaling transport (#1103); see websocket.h.
#include "websocket.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>

#include "sm/tls/crypto_random.h"
#include "sm/tls/message_digest.h"
#include "sm/tls/tls_stream.h"

namespace sm::net {

namespace {

constexpr char kWsGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

std::string HeaderValue(const std::string& headers, std::string_view name) {
  size_t pos = 0;
  while ((pos = headers.find("\r\n", pos)) != std::string::npos) {
    pos += 2;
    const size_t colon = headers.find(':', pos);
    const size_t eol = headers.find("\r\n", pos);
    if (colon == std::string::npos || eol == std::string::npos || colon > eol) continue;
    std::string key = headers.substr(pos, colon - pos);
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
    if (key == name) {
      size_t b = colon + 1;
      while (b < eol && headers[b] == ' ') ++b;
      return headers.substr(b, eol - b);
    }
  }
  return {};
}

int OpenNonBlockingTcp(const p2p::IpAddress& ip, uint16_t port, bool* in_progress) {
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
#ifdef SO_NOSIGPIPE
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
  sockaddr_in sa{};
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  sa.sin_addr.s_addr = htonl(ip.v4());
  *in_progress = false;
  if (connect(fd, reinterpret_cast<sockaddr*>(&sa), sizeof sa) != 0) {
    if (errno != EINPROGRESS) {
      close(fd);
      return -1;
    }
    *in_progress = true;
  }
  return fd;
}

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

// A TLS client for wss:// and https://: the server's name for SNI and the certificate check, the system's roots.
std::unique_ptr<tls::TlsStream> MakeTlsClient(const Url& url) {
  tls::TlsConfig c;
  c.role = tls::TlsRole::kClient;
  c.server_name = url.host;
  c.trusted_roots = url.trusted_roots;
  c.alpn_protocols = {"http/1.1"};
  return std::make_unique<tls::TlsStream>(c);
}

// Hands every record the engine has decrypted to `in`; false when the connection is over (failed or close_notify).
bool DrainTls(tls::TlsStream& t, std::vector<uint8_t>& in) {
  uint8_t buf[16384];
  for (;;) {
    const int n = t.Read(buf);
    if (n > 0) in.insert(in.end(), buf, buf + n);
    else return n == tls::kTlsWouldBlock && t.state() != tls::TlsState::kFailed;
  }
}

}  // namespace

std::string Base64Encode(std::span<const uint8_t> data) {
  static constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  size_t i = 0;
  for (; i + 2 < data.size(); i += 3) {
    const uint32_t v = (uint32_t{data[i]} << 16) | (uint32_t{data[i + 1]} << 8) | data[i + 2];
    out += kTable[v >> 18];
    out += kTable[(v >> 12) & 63];
    out += kTable[(v >> 6) & 63];
    out += kTable[v & 63];
  }
  if (i < data.size()) {
    const bool two = i + 1 < data.size();
    const uint32_t v = (uint32_t{data[i]} << 16) | (two ? uint32_t{data[i + 1]} << 8 : 0);
    out += kTable[v >> 18];
    out += kTable[(v >> 12) & 63];
    out += two ? kTable[(v >> 6) & 63] : '=';
    out += '=';
  }
  return out;
}

std::string WebSocketAcceptKey(std::string_view client_key) {
  const std::string in = std::string(client_key) + kWsGuid;
  uint8_t digest[20];
  const size_t n = tls::ComputeDigest(tls::kDigestSha1,
                                      std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(in.data()), in.size()),
                                      digest);
  return Base64Encode(std::span<const uint8_t>(digest, n));
}

std::vector<uint8_t> WsEncodeFrame(uint8_t opcode, std::string_view payload, bool mask) {
  std::vector<uint8_t> out;
  out.push_back(static_cast<uint8_t>(0x80 | (opcode & 0x0F)));
  const uint8_t mask_bit = mask ? 0x80 : 0;
  const size_t n = payload.size();
  if (n < 126) {
    out.push_back(static_cast<uint8_t>(mask_bit | n));
  } else if (n <= 0xFFFF) {
    out.push_back(mask_bit | 126);
    out.push_back(static_cast<uint8_t>(n >> 8));
    out.push_back(static_cast<uint8_t>(n));
  } else {
    out.push_back(mask_bit | 127);
    for (int s = 56; s >= 0; s -= 8) out.push_back(static_cast<uint8_t>(static_cast<uint64_t>(n) >> s));
  }
  uint8_t key[4] = {0, 0, 0, 0};
  if (mask) {
    tls::CreateRandomBytes(key);
    out.insert(out.end(), key, key + 4);
  }
  for (size_t i = 0; i < n; ++i) out.push_back(static_cast<uint8_t>(payload[i]) ^ key[i % 4]);
  return out;
}

long WsDecodeFrame(std::span<const uint8_t> buf, WsFrame* out) {
  if (buf.size() < 2) return 0;
  const bool fin = buf[0] & 0x80;
  const uint8_t opcode = buf[0] & 0x0F;
  const bool masked = buf[1] & 0x80;
  uint64_t len = buf[1] & 0x7F;
  size_t pos = 2;
  if (len == 126) {
    if (buf.size() < 4) return 0;
    len = (uint64_t{buf[2]} << 8) | buf[3];
    pos = 4;
  } else if (len == 127) {
    if (buf.size() < 10) return 0;
    len = 0;
    for (size_t i = 0; i < 8; ++i) len = (len << 8) | buf[2 + i];
    pos = 10;
  }
  if (len > (uint64_t{1} << 24)) return -1;  // 16 MB: far beyond any signaling message.
  uint8_t key[4] = {0, 0, 0, 0};
  if (masked) {
    if (buf.size() < pos + 4) return 0;
    std::memcpy(key, &buf[pos], 4);
    pos += 4;
  }
  if (buf.size() < pos + len) return 0;
  out->fin = fin;
  out->opcode = opcode;
  out->payload.resize(static_cast<size_t>(len));
  for (size_t i = 0; i < len; ++i) out->payload[i] = static_cast<char>(buf[pos + i] ^ key[i % 4]);
  return static_cast<long>(pos + len);
}

std::string WsServerHandshakeResponse(const std::string& request) {
  const std::string key = HeaderValue(request, "sec-websocket-key");
  if (key.empty() || request.compare(0, 4, "GET ") != 0) return {};
  return "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
         WebSocketAcceptKey(key) + "\r\n\r\n";
}

std::optional<Url> ParseUrl(std::string_view url) {
  Url u;
  size_t p = url.find("://");
  if (p == std::string_view::npos) return std::nullopt;
  const std::string_view scheme = url.substr(0, p);
  if (scheme == "wss" || scheme == "https") {
    u.tls = true;
    u.port = 443;
  } else if (scheme != "ws" && scheme != "http") {
    return std::nullopt;
  }
  url.remove_prefix(p + 3);
  const size_t slash = url.find('/');
  std::string_view hostport = url.substr(0, slash);
  u.path = slash == std::string_view::npos ? "/" : std::string(url.substr(slash));
  const size_t colon = hostport.rfind(':');
  if (colon != std::string_view::npos) {
    const int port = std::atoi(std::string(hostport.substr(colon + 1)).c_str());
    if (port <= 0 || port > 65535) return std::nullopt;
    u.port = static_cast<uint16_t>(port);
    hostport = hostport.substr(0, colon);
  }
  if (hostport.empty()) return std::nullopt;
  u.host = std::string(hostport);
  return u;
}

std::optional<p2p::IpAddress> ResolveIpv4(const std::string& host) {
  if (auto ip = p2p::IpAddress::FromString(host)) return ip;
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || res == nullptr) return std::nullopt;
  const auto* sa = reinterpret_cast<const sockaddr_in*>(res->ai_addr);
  const p2p::IpAddress ip(ntohl(sa->sin_addr.s_addr));
  freeaddrinfo(res);
  return ip;
}

struct WebSocketClient::Tls {
  std::unique_ptr<tls::TlsStream> stream;
};

WebSocketClient::WebSocketClient() = default;

WebSocketClient::~WebSocketClient() {
  if (fd_ >= 0) close(fd_);
}

bool WebSocketClient::Connect(const Url& url) {
  const auto ip = ResolveIpv4(url.host);
  if (!ip) return false;
  bool in_progress = false;
  fd_ = OpenNonBlockingTcp(*ip, url.port, &in_progress);
  if (fd_ < 0) {
    state_ = State::kClosed;
    return false;
  }
  host_header_ = url.host + ":" + std::to_string(url.port);
  path_ = url.path;
  if (url.tls) tls_ = std::make_unique<Tls>(Tls{MakeTlsClient(url)});
  uint8_t nonce[16];
  tls::CreateRandomBytes(nonce);
  key_ = Base64Encode(nonce);
  state_ = State::kConnecting;
  Poll();
  return true;
}

void WebSocketClient::Send(std::string_view text) { SendFrame(kWsText, text); }

void WebSocketClient::SendBinary(std::string_view data) { SendFrame(kWsBinary, data); }

void WebSocketClient::SendFrame(uint8_t opcode, std::string_view payload) {
  if (state_ == State::kOpen) {
    const auto f = WsEncodeFrame(opcode, payload, true);
    out_.insert(out_.end(), f.begin(), f.end());
    Flush();
  } else if (state_ != State::kClosed) {
    pending_.emplace_back(opcode, std::string(payload));
  }
}

void WebSocketClient::Fail() {
  if (fd_ >= 0) close(fd_);
  fd_ = -1;
  state_ = State::kClosed;
}

void WebSocketClient::Close() {
  if (state_ == State::kOpen) {
    const auto f = WsEncodeFrame(kWsClose, "", true);
    out_.insert(out_.end(), f.begin(), f.end());
    Flush();
  }
  Fail();
}

void WebSocketClient::Flush() {
  if (!tls_) {
    wire_.insert(wire_.end(), out_.begin(), out_.end());
    out_.clear();
  } else if (fd_ >= 0) {
    tls::TlsStream& t = *tls_->stream;
    if (t.state() == tls::TlsState::kConnected && !out_.empty()) {
      if (t.Write(out_) == tls::kTlsError) {
        Fail();
        return;
      }
      out_.clear();  // Once connected, Write takes the whole buffer.
    }
    const std::vector<uint8_t> records = t.TakeOutgoing();
    wire_.insert(wire_.end(), records.begin(), records.end());
  }
  while (!wire_.empty() && fd_ >= 0) {
    const ssize_t n = send(fd_, wire_.data(), wire_.size(), kSendFlags);
    if (n > 0) {
      wire_.erase(wire_.begin(), wire_.begin() + n);
    } else {
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return;
      Fail();
      return;
    }
  }
}

void WebSocketClient::ReadAvailable() {
  uint8_t buf[16384];
  while (fd_ >= 0) {
    const ssize_t n = recv(fd_, buf, sizeof buf, 0);
    if (n > 0) {
      if (!tls_) {
        in_.insert(in_.end(), buf, buf + n);
      } else {
        tls_->stream->Feed(std::span<const uint8_t>(buf, static_cast<size_t>(n)));
        if (!DrainTls(*tls_->stream, in_)) {
          Fail();
          return;
        }
      }
    } else if (n == 0) {
      Fail();  // Closed by the server.
      return;
    } else {
      if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) Fail();
      return;
    }
  }
}

void WebSocketClient::ParseFrames() {
  while (state_ == State::kOpen) {
    WsFrame f;
    const long used = WsDecodeFrame(in_, &f);
    if (used == 0) return;
    if (used < 0) {
      Fail();
      return;
    }
    in_.erase(in_.begin(), in_.begin() + used);
    switch (f.opcode) {
      case kWsText:
      case kWsBinary:
      case kWsContinuation:
        fragments_ += f.payload;
        if (f.fin) {
          messages_.push_back(std::move(fragments_));
          fragments_.clear();
        }
        break;
      case kWsPing: {
        const auto pong = WsEncodeFrame(kWsPong, f.payload, true);
        out_.insert(out_.end(), pong.begin(), pong.end());
        break;
      }
      case kWsClose:
        Close();
        return;
      default:
        break;
    }
  }
}

void WebSocketClient::Poll() {
  if (fd_ < 0) return;
  if (state_ == State::kConnecting) {
    pollfd p{fd_, POLLOUT, 0};
    if (poll(&p, 1, 0) <= 0) return;
    int err = 0;
    socklen_t len = sizeof err;
    getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &len);
    if (err != 0) {
      Fail();
      return;
    }
    const std::string req = "GET " + path_ + " HTTP/1.1\r\nHost: " + host_header_ +
                            "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key_ +
                            "\r\nSec-WebSocket-Version: 13\r\n\r\n";
    out_.assign(req.begin(), req.end());  // With TLS, written once the TLS handshake is done.
    if (tls_ && !tls_->stream->Start()) {
      Fail();
      return;
    }
    state_ = State::kHandshake;
  }
  Flush();
  ReadAvailable();
  if (state_ == State::kHandshake) {
    const std::string text(in_.begin(), in_.end());
    const size_t end = text.find("\r\n\r\n");
    if (end == std::string::npos) return;
    const std::string headers = text.substr(0, end + 2);
    if (headers.compare(0, 7, "HTTP/1.") != 0 || headers.compare(9, 3, "101") != 0 ||
        HeaderValue(headers, "sec-websocket-accept") != WebSocketAcceptKey(key_)) {
      Fail();
      return;
    }
    in_.erase(in_.begin(), in_.begin() + static_cast<long>(end + 4));
    state_ = State::kOpen;
    for (const auto& [opcode, m] : pending_) {
      const auto f = WsEncodeFrame(opcode, m, true);
      out_.insert(out_.end(), f.begin(), f.end());
    }
    pending_.clear();
  }
  ParseFrames();
  Flush();
}

std::vector<std::string> WebSocketClient::TakeMessages() {
  std::vector<std::string> out;
  out.swap(messages_);
  return out;
}

std::optional<std::string> HttpGet(const Url& url, int timeout_ms) {
  const auto ip = ResolveIpv4(url.host);
  if (!ip) return std::nullopt;
  bool in_progress = false;
  const int fd = OpenNonBlockingTcp(*ip, url.port, &in_progress);
  if (fd < 0) return std::nullopt;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  auto wait = [&](short events) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
    if (left.count() <= 0) return false;
    pollfd p{fd, events, 0};
    return poll(&p, 1, static_cast<int>(left.count())) > 0;
  };
  const std::string req = "GET " + url.path + " HTTP/1.1\r\nHost: " + url.host + ":" + std::to_string(url.port) +
                          "\r\nConnection: close\r\n\r\n";
  bool ok = true;
  if (in_progress) {
    int err = 0;
    socklen_t len = sizeof err;
    ok = wait(POLLOUT) && getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0;
  }
  // With TLS the request waits in the engine until the handshake is done; `wire` is what goes to the socket.
  std::unique_ptr<tls::TlsStream> t = url.tls ? MakeTlsClient(url) : nullptr;
  std::vector<uint8_t> wire, body;
  bool request_written = false;
  if (!t) {
    wire.assign(req.begin(), req.end());
    request_written = true;
  } else if (ok) {
    ok = t->Start();
  }
  bool eof = false;
  while (ok && !eof) {
    if (t) {
      if (!request_written && t->state() == tls::TlsState::kConnected) {
        ok = t->Write(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(req.data()), req.size())) > 0;
        request_written = true;
      }
      const std::vector<uint8_t> records = t->TakeOutgoing();
      wire.insert(wire.end(), records.begin(), records.end());
    }
    while (ok && !wire.empty()) {
      const ssize_t n = send(fd, wire.data(), wire.size(), kSendFlags);
      if (n > 0) wire.erase(wire.begin(), wire.begin() + n);
      else if (n < 0 && (errno == EAGAIN || errno == EINTR)) ok = wait(POLLOUT);
      else ok = false;
    }
    if (!ok) break;
    uint8_t buf[4096];
    const ssize_t n = recv(fd, buf, sizeof buf, 0);
    if (n > 0) {
      if (!t) {
        body.insert(body.end(), buf, buf + n);
      } else {
        t->Feed(std::span<const uint8_t>(buf, static_cast<size_t>(n)));
        eof = !DrainTls(*t, body);  // close_notify ends the body as the TCP close does.
        ok = t->state() != tls::TlsState::kFailed;
      }
    } else if (n == 0) {
      eof = true;
    } else if (errno == EAGAIN || errno == EINTR) {
      ok = wait(POLLIN);
    } else {
      ok = false;
    }
  }
  close(fd);
  const std::string resp(body.begin(), body.end());
  const size_t end = resp.find("\r\n\r\n");
  if (!ok || end == std::string::npos || resp.compare(0, 7, "HTTP/1.") != 0 || resp.compare(9, 3, "200") != 0) {
    return std::nullopt;
  }
  std::string out = resp.substr(end + 4);  // Connection: close, so the body runs to the end.
  std::string te = HeaderValue(resp.substr(0, end + 2), "transfer-encoding");
  std::transform(te.begin(), te.end(), te.begin(), [](unsigned char c) { return std::tolower(c); });
  if (te == "chunked") {  // A proxy (nginx) may chunk it: join the chunks.
    std::string joined;
    size_t pos = 0;
    for (;;) {
      const size_t eol = out.find("\r\n", pos);
      if (eol == std::string::npos) return std::nullopt;
      const unsigned long size = std::strtoul(out.substr(pos, eol - pos).c_str(), nullptr, 16);
      if (size == 0) break;
      if (eol + 2 + size > out.size()) return std::nullopt;
      joined.append(out, eol + 2, size);
      pos = eol + 2 + size + 2;
    }
    out = std::move(joined);
  }
  return out;
}

}  // namespace sm::net
