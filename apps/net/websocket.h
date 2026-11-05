// The apps' signaling transport (#1103): a non-blocking WebSocket client (RFC 6455, text messages) over a POSIX TCP
// socket, the server side's handshake and framing (for the tests' signaling stub), and a blocking HTTP GET for
// `/turn`. ws:// and http://, and wss:// and https:// (#1121) through sm::tls's TlsStream (BoringSSL or Mbed TLS),
// with the server's certificate checked against the system's roots and the URL's host name.
//
// Like os_sockets.h, the caller's loop drives it: Poll() makes progress without waiting, and Fd() is what to wait on.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "sm/p2p/socket_address.h"

namespace sm::net {

std::string Base64Encode(std::span<const uint8_t> data);
// Sec-WebSocket-Accept for a Sec-WebSocket-Key: base64(SHA-1(key + RFC 6455's GUID)).
std::string WebSocketAcceptKey(std::string_view client_key);

enum WsOpcode : uint8_t { kWsContinuation = 0, kWsText = 1, kWsBinary = 2, kWsClose = 8, kWsPing = 9, kWsPong = 10 };
// One frame with FIN set; a client masks its frames (RFC 6455 5.3).
std::vector<uint8_t> WsEncodeFrame(uint8_t opcode, std::string_view payload, bool mask);
struct WsFrame {
  uint8_t opcode = 0;
  bool fin = true;
  std::string payload;  // Unmasked.
};
// Decodes the frame at the front of `buf`: the bytes it took, 0 if it is not complete yet, -1 if it is malformed.
long WsDecodeFrame(std::span<const uint8_t> buf, WsFrame* out);

// The server's 101 response to a client's upgrade request (the whole header block); empty if it is not one.
std::string WsServerHandshakeResponse(const std::string& request);

// "ws://host:port/path" or "http://host:port/path" (port 80 if absent); "wss://..." or "https://..." (port 443 if
// absent) set `tls`.
struct Url {
  std::string host;
  uint16_t port = 80;
  std::string path = "/";
  bool tls = false;
  // Certificates (DER) trusted besides the system's roots, for a server with its own CA or a self-signed one.
  std::vector<std::vector<uint8_t>> trusted_roots = {};
};
std::optional<Url> ParseUrl(std::string_view url);
// The host's first IPv4 address (dotted text or a name, getaddrinfo).
std::optional<p2p::IpAddress> ResolveIpv4(const std::string& host);

class WebSocketClient {
 public:
  WebSocketClient();
  ~WebSocketClient();
  WebSocketClient(const WebSocketClient&) = delete;
  WebSocketClient& operator=(const WebSocketClient&) = delete;

  // Starts connecting; false if the host doesn't resolve or the socket can't be made.
  bool Connect(const Url& url);
  // A text message; queued until the handshake is done.
  void Send(std::string_view text);
  // A binary message (LiveKit's protobuf signaling, C19); queued the same way. Received binary messages come out of
  // TakeMessages as their bytes, like text ones.
  void SendBinary(std::string_view data);
  // Connects, handshakes, reads and writes as far as it can without waiting; answers pings.
  void Poll();
  std::vector<std::string> TakeMessages();
  void Close();

  bool open() const { return state_ == State::kOpen; }
  // Failed, refused or closed by either side.
  bool closed() const { return state_ == State::kClosed; }
  int fd() const { return fd_; }
  // Whether Poll has something to write (wait for POLLOUT too).
  bool wants_write() const { return state_ == State::kConnecting || !wire_.empty(); }

 private:
  enum class State { kIdle, kConnecting, kHandshake, kOpen, kClosed };
  struct Tls;  // The TLS engine of a wss:// connection.
  void Fail();
  void SendFrame(uint8_t opcode, std::string_view payload);
  void Flush();
  void ReadAvailable();
  void ParseFrames();

  int fd_ = -1;
  State state_ = State::kIdle;
  std::string host_header_, path_, key_;
  std::vector<uint8_t> in_, out_;  // Plaintext.
  std::unique_ptr<Tls> tls_;
  std::vector<uint8_t> wire_;      // TLS records waiting for the socket.
  std::vector<std::pair<uint8_t, std::string>> pending_;  // Opcode and payload, sent before the handshake finished.
  std::string fragments_;
  std::vector<std::string> messages_;
};

// A blocking GET (connect, request, read to the end of the body); nullopt on an error, a non-200 status or after
// `timeout_ms`.
std::optional<std::string> HttpGet(const Url& url, int timeout_ms);

}  // namespace sm::net
