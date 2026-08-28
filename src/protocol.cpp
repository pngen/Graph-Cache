// Framed TCP protocol: wire encoding/decoding and Winsock TCP helpers.
// Copyright 2026 Summon Software Labs. Apache License 2.0.

#include "graphcache/protocol.hpp"

#include <cstring>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

namespace gc {

namespace {
constexpr std::size_t kHeaderMin = 1 + 1 + 2 + 8 * 8 + 2 + 4;  // type+ver+flags+8x u64+status+plen

void put_u8(std::vector<std::uint8_t>& b, std::uint8_t v) { b.push_back(v); }
void put_u16_le(std::vector<std::uint8_t>& b, std::uint16_t v) {
  b.push_back(static_cast<std::uint8_t>(v & 0xff));
  b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
}
void put_u32_le(std::vector<std::uint8_t>& b, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff));
}
void put_u64_le(std::vector<std::uint8_t>& b, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff));
}

bool rd_u16_le(std::span<const std::uint8_t> data, std::size_t off, std::uint16_t& v) {
  if (off + 2 > data.size()) return false;
  v = static_cast<std::uint16_t>(data[off]) | (static_cast<std::uint16_t>(data[off + 1]) << 8);
  return true;
}
bool rd_u32_le(std::span<const std::uint8_t> data, std::size_t off, std::uint32_t& v) {
  if (off + 4 > data.size()) return false;
  v = 0;
  for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(data[off + i]) << (8 * i);
  return true;
}
bool rd_u64_le(std::span<const std::uint8_t> data, std::size_t off, std::uint64_t& v) {
  if (off + 8 > data.size()) return false;
  v = 0;
  for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(data[off + i]) << (8 * i);
  return true;
}

void winsock_init() {
  static bool done = false;
  if (!done) {
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
    done = true;
  }
}

bool valid_type(std::uint8_t t) { return t >= 1 && t <= 13; }
} // namespace

std::vector<std::uint8_t> encode_message(const ProtocolMessage& m) {
  std::vector<std::uint8_t> b;
  b.reserve(kHeaderMin + m.payload.size());
  put_u8(b, static_cast<std::uint8_t>(m.type));
  put_u8(b, m.version);
  put_u16_le(b, m.flags);
  put_u64_le(b, m.request_id);
  put_u64_le(b, m.epoch.value);
  put_u64_le(b, m.cache_gen.value);
  put_u64_le(b, m.worker_id.value);
  put_u64_le(b, m.worker_boot.value);
  put_u64_le(b, m.graph_gen.value);
  put_u64_le(b, m.capture_attempt.value);
  put_u64_le(b, m.replay_attempt.value);
  put_u16_le(b, static_cast<std::uint16_t>(m.status));
  put_u32_le(b, static_cast<std::uint32_t>(m.payload.size()));
  b.insert(b.end(), m.payload.begin(), m.payload.end());
  return b;
}

Result<ProtocolMessage> decode_message(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < kHeaderMin) {
    return Result<ProtocolMessage>::failure(Error(ErrorCode::ProtocolTruncated, "message header truncated"));
  }
  ProtocolMessage m;
  m.type = static_cast<ProtocolMessageType>(bytes[0]);
  m.version = bytes[1];
  if (!valid_type(static_cast<std::uint8_t>(m.type))) {
    return Result<ProtocolMessage>::failure(Error(ErrorCode::ProtocolUnknownMessage, "unknown message type"));
  }
  if (m.version != ProtocolVersion) {
    return Result<ProtocolMessage>::failure(Error(ErrorCode::ProtocolUnknownVersion, "protocol version mismatch"));
  }
  std::size_t off = 2;
  if (!rd_u16_le(bytes, off, m.flags)) return Result<ProtocolMessage>::failure(Error(ErrorCode::ProtocolTruncated, "flags"));
  off += 2;
  if (!rd_u64_le(bytes, off, m.request_id)) return Result<ProtocolMessage>::failure(Error(ErrorCode::ProtocolTruncated, "request_id"));
  off += 8;
  if (!rd_u64_le(bytes, off, m.epoch.value)) return Result<ProtocolMessage>::failure(Error(ErrorCode::ProtocolTruncated, "epoch"));
  off += 8;
  if (!rd_u64_le(bytes, off, m.cache_gen.value)) return Result<ProtocolMessage>::failure(Error(ErrorCode::ProtocolTruncated, "cache_gen"));
  off += 8;
  if (!rd_u64_le(bytes, off, m.worker_id.value)) return Result<ProtocolMessage>::failure(Error(ErrorCode::ProtocolTruncated, "worker_id"));
  off += 8;
  if (!rd_u64_le(bytes, off, m.worker_boot.value)) return Result<ProtocolMessage>::failure(Error(ErrorCode::ProtocolTruncated, "worker_boot"));
  off += 8;
  if (!rd_u64_le(bytes, off, m.graph_gen.value)) return Result<ProtocolMessage>::failure(Error(ErrorCode::ProtocolTruncated, "graph_gen"));
  off += 8;
  if (!rd_u64_le(bytes, off, m.capture_attempt.value)) return Result<ProtocolMessage>::failure(Error(ErrorCode::ProtocolTruncated, "capture_attempt"));
  off += 8;
  if (!rd_u64_le(bytes, off, m.replay_attempt.value)) return Result<ProtocolMessage>::failure(Error(ErrorCode::ProtocolTruncated, "replay_attempt"));
  off += 8;
  std::uint16_t status;
  if (!rd_u16_le(bytes, off, status)) return Result<ProtocolMessage>::failure(Error(ErrorCode::ProtocolTruncated, "status"));
  off += 2;
  m.status = static_cast<ProtocolStatus>(status);
  std::uint32_t plen;
  if (!rd_u32_le(bytes, off, plen)) return Result<ProtocolMessage>::failure(Error(ErrorCode::ProtocolTruncated, "payload_len"));
  off += 4;
  if (plen > MaxPayloadSize) {
    return Result<ProtocolMessage>::failure(Error(ErrorCode::ProtocolOverflow, "payload exceeds max frame size"));
  }
  if (off + plen != bytes.size()) {
    return Result<ProtocolMessage>::failure(Error(ErrorCode::ProtocolTruncated, "payload length mismatch"));
  }
  m.payload.assign(bytes.begin() + off, bytes.end());
  return Result<ProtocolMessage>::success(std::move(m));
}

Result<void> send_frame(std::int64_t socket, const ProtocolMessage& m) {
  auto body = encode_message(m);
  if (body.size() > MaxFrameSize) {
    return Result<void>::failure(Error(ErrorCode::ProtocolOverflow, "frame exceeds max size"));
  }
  std::vector<std::uint8_t> frame;
  frame.reserve(4 + body.size());
  put_u32_le(frame, static_cast<std::uint32_t>(body.size()));
  frame.insert(frame.end(), body.begin(), body.end());
  return tcp_write_all(socket, frame.data(), frame.size());
}

Result<ProtocolMessage> recv_frame(std::int64_t socket) {
  std::uint8_t lenbuf[4];
  auto rr = tcp_read_exact(socket, lenbuf, 4);
  if (!rr.ok()) return Result<ProtocolMessage>::failure(rr.error());
  std::uint32_t len = static_cast<std::uint32_t>(lenbuf[0]) |
                      (static_cast<std::uint32_t>(lenbuf[1]) << 8) |
                      (static_cast<std::uint32_t>(lenbuf[2]) << 16) |
                      (static_cast<std::uint32_t>(lenbuf[3]) << 24);
  if (len == 0) return Result<ProtocolMessage>::failure(Error(ErrorCode::ProtocolZeroLength, "zero-length frame"));
  if (len > MaxFrameSize) return Result<ProtocolMessage>::failure(Error(ErrorCode::ProtocolOverflow, "frame exceeds max size"));
  std::vector<std::uint8_t> body(len);
  auto rb = tcp_read_exact(socket, body.data(), len);
  if (!rb.ok()) return Result<ProtocolMessage>::failure(Error(ErrorCode::ProtocolTruncated, "frame truncated"));
  return decode_message(body);
}

const char* to_string(ProtocolMessageType t) noexcept {
  switch (t) {
    case ProtocolMessageType::Hello: return "Hello";
    case ProtocolMessageType::Welcome: return "Welcome";
    case ProtocolMessageType::Register: return "Register";
    case ProtocolMessageType::RegisterAck: return "RegisterAck";
    case ProtocolMessageType::Lookup: return "Lookup";
    case ProtocolMessageType::LookupResult: return "LookupResult";
    case ProtocolMessageType::CaptureInstruct: return "CaptureInstruct";
    case ProtocolMessageType::CaptureResult: return "CaptureResult";
    case ProtocolMessageType::ReplayInstruct: return "ReplayInstruct";
    case ProtocolMessageType::ReplayResult: return "ReplayResult";
    case ProtocolMessageType::Invalidate: return "Invalidate";
    case ProtocolMessageType::InvalidateAck: return "InvalidateAck";
    case ProtocolMessageType::Shutdown: return "Shutdown";
  }
  return "Unknown";
}
const char* to_string(ProtocolStatus s) noexcept {
  switch (s) {
    case ProtocolStatus::Ok: return "Ok";
    case ProtocolStatus::StaleEpoch: return "StaleEpoch";
    case ProtocolStatus::StaleWorkerBoot: return "StaleWorkerBoot";
    case ProtocolStatus::StaleCacheGeneration: return "StaleCacheGeneration";
    case ProtocolStatus::StaleGraphGeneration: return "StaleGraphGeneration";
    case ProtocolStatus::StaleCaptureAttempt: return "StaleCaptureAttempt";
    case ProtocolStatus::StaleReplayAttempt: return "StaleReplayAttempt";
    case ProtocolStatus::DuplicateCaptureCompletion: return "DuplicateCaptureCompletion";
    case ProtocolStatus::CompletionAfterInvalidation: return "CompletionAfterInvalidation";
    case ProtocolStatus::WrongBaseGeneration: return "WrongBaseGeneration";
    case ProtocolStatus::InvalidRequest: return "InvalidRequest";
    case ProtocolStatus::WorkerUnavailable: return "WorkerUnavailable";
    case ProtocolStatus::LookupMiss: return "LookupMiss";
    case ProtocolStatus::CaptureFailed: return "CaptureFailed";
    case ProtocolStatus::ReplayFailed: return "ReplayFailed";
    case ProtocolStatus::UnknownMessage: return "UnknownMessage";
    case ProtocolStatus::ProtocolVersionMismatch: return "ProtocolVersionMismatch";
    case ProtocolStatus::Malformed: return "Malformed";
    case ProtocolStatus::Internal: return "Internal";
  }
  return "Internal";
}

// ---- TCP helpers ----
Result<TcpSocket> tcp_listen(std::uint16_t port) {
  winsock_init();
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return Result<TcpSocket>::failure(Error(ErrorCode::ProtocolIoError, "socket failed"));
  BOOL reuse = TRUE;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);
  if (bind(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
    closesocket(s);
    return Result<TcpSocket>::failure(Error(ErrorCode::ProtocolIoError, "bind failed"));
  }
  if (listen(s, SOMAXCONN) == SOCKET_ERROR) {
    closesocket(s);
    return Result<TcpSocket>::failure(Error(ErrorCode::ProtocolIoError, "listen failed"));
  }
  TcpSocket t;
  t.handle = static_cast<std::int64_t>(s);
  return Result<TcpSocket>::success(t);
}

Result<TcpSocket> tcp_accept(const TcpSocket& listener) {
  SOCKET s = accept(static_cast<SOCKET>(listener.handle), nullptr, nullptr);
  if (s == INVALID_SOCKET) return Result<TcpSocket>::failure(Error(ErrorCode::ProtocolIoError, "accept failed"));
  TcpSocket t;
  t.handle = static_cast<std::int64_t>(s);
  return Result<TcpSocket>::success(t);
}

Result<TcpSocket> tcp_connect(const std::string& host, std::uint16_t port) {
  winsock_init();
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return Result<TcpSocket>::failure(Error(ErrorCode::ProtocolIoError, "socket failed"));
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
    closesocket(s);
    return Result<TcpSocket>::failure(Error(ErrorCode::ProtocolIoError, "getaddrinfo failed"));
  }
  int rc = connect(s, res->ai_addr, static_cast<int>(res->ai_addrlen));
  freeaddrinfo(res);
  if (rc == SOCKET_ERROR) {
    closesocket(s);
    return Result<TcpSocket>::failure(Error(ErrorCode::ProtocolIoError, "connect failed"));
  }
  TcpSocket t;
  t.handle = static_cast<std::int64_t>(s);
  return Result<TcpSocket>::success(t);
}

Result<void> tcp_close(const TcpSocket& s) {
  if (s.handle != -1) closesocket(static_cast<SOCKET>(s.handle));
  return Result<void>::success();
}

Result<void> tcp_read_exact(std::int64_t socket, std::uint8_t* buf, std::size_t n) {
  std::size_t got = 0;
  while (got < n) {
    int rc = recv(static_cast<SOCKET>(socket), reinterpret_cast<char*>(buf + got),
                  static_cast<int>(n - got), 0);
    if (rc == 0) return Result<void>::failure(Error(ErrorCode::ProtocolTruncated, "peer closed"));
    if (rc == SOCKET_ERROR) return Result<void>::failure(Error(ErrorCode::ProtocolIoError, "recv failed"));
    got += static_cast<std::size_t>(rc);
  }
  return Result<void>::success();
}

Result<void> tcp_write_all(std::int64_t socket, const std::uint8_t* buf, std::size_t n) {
  std::size_t sent = 0;
  while (sent < n) {
    int rc = send(static_cast<SOCKET>(socket), reinterpret_cast<const char*>(buf + sent),
                  static_cast<int>(n - sent), 0);
    if (rc == SOCKET_ERROR) return Result<void>::failure(Error(ErrorCode::ProtocolIoError, "send failed"));
    sent += static_cast<std::size_t>(rc);
  }
  return Result<void>::success();
}

} // namespace gc
