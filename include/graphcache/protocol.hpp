#pragma once
// Framed-TCP control-plane protocol for Graph Cache authority.
// A frame is a 4-byte little-endian length prefix followed by the message body.
// Copyright 2026 Summon Software Labs. Apache License 2.0.

#include "graphcache/ids.hpp"
#include "graphcache/result.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gc {

constexpr std::uint32_t ProtocolVersion = 1;
constexpr std::uint32_t MaxFrameSize = 8u * 1024u * 1024u;  // 8 MiB hard max
constexpr std::uint32_t MaxPayloadSize = MaxFrameSize - 96u;

enum class ProtocolMessageType : std::uint8_t {
  Hello = 1,        // client -> coordinator
  Welcome = 2,      // coordinator -> client
  Register = 3,     // worker -> coordinator
  RegisterAck = 4,  // coordinator -> worker
  Lookup = 5,       // client -> coordinator
  LookupResult = 6, // coordinator -> client
  CaptureInstruct = 7,  // coordinator -> worker
  CaptureResult = 8,    // worker -> coordinator
  ReplayInstruct = 9,   // coordinator -> worker
  ReplayResult = 10,    // worker -> coordinator
  Invalidate = 11,      // client -> coordinator
  InvalidateAck = 12,   // coordinator -> client
  Shutdown = 13         // coordinator -> workers (stop)
};

// Structured status codes carried in a message.
enum class ProtocolStatus : std::uint16_t {
  Ok = 0,
  StaleEpoch = 1,
  StaleWorkerBoot = 2,
  StaleCacheGeneration = 3,
  StaleGraphGeneration = 4,
  StaleCaptureAttempt = 5,
  StaleReplayAttempt = 6,
  DuplicateCaptureCompletion = 7,
  CompletionAfterInvalidation = 8,
  WrongBaseGeneration = 9,
  InvalidRequest = 10,
  WorkerUnavailable = 11,
  LookupMiss = 12,
  CaptureFailed = 13,
  ReplayFailed = 14,
  UnknownMessage = 15,
  ProtocolVersionMismatch = 16,
  Malformed = 17,
  Internal = 18
};

struct ProtocolMessage {
  ProtocolMessageType type{ProtocolMessageType::Hello};
  std::uint8_t version{ProtocolVersion};
  std::uint16_t flags{0};
  std::uint64_t request_id{0};
  CoordinatorEpoch epoch;
  CacheGeneration cache_gen;
  WorkerId worker_id;
  WorkerBootId worker_boot;
  GraphGeneration graph_gen;
  CaptureAttemptId capture_attempt;
  ReplayAttemptId replay_attempt;
  ProtocolStatus status{ProtocolStatus::Ok};
  std::vector<std::uint8_t> payload;

  [[nodiscard]] ProtocolStatus status_code() const { return status; }
};

// Encode a message into canonical wire bytes (no frame prefix).
[[nodiscard]] std::vector<std::uint8_t> encode_message(const ProtocolMessage& m);
// Decode and strictly validate a message body. Malformed/unknown/mismatched
// version are rejected.
[[nodiscard]] Result<ProtocolMessage> decode_message(std::span<const std::uint8_t> bytes);

// TCP frame envelope: [u32 LE length][message bytes].
[[nodiscard]] Result<void> send_frame(std::int64_t socket, const ProtocolMessage& m);
[[nodiscard]] Result<ProtocolMessage> recv_frame(std::int64_t socket);

[[nodiscard]] const char* to_string(ProtocolMessageType) noexcept;
[[nodiscard]] const char* to_string(ProtocolStatus) noexcept;

// TCP helpers (Winsock on Windows).
struct TcpSocket {
  std::int64_t handle{-1};
  [[nodiscard]] bool valid() const { return handle != -1; }
};
[[nodiscard]] Result<TcpSocket> tcp_listen(std::uint16_t port);
[[nodiscard]] Result<TcpSocket> tcp_accept(const TcpSocket& listener);
[[nodiscard]] Result<TcpSocket> tcp_connect(const std::string& host, std::uint16_t port);
[[nodiscard]] Result<void> tcp_close(const TcpSocket& s);
// Best-effort: read exactly n bytes or fail.
[[nodiscard]] Result<void> tcp_read_exact(std::int64_t socket, std::uint8_t* buf, std::size_t n);
[[nodiscard]] Result<void> tcp_write_all(std::int64_t socket, const std::uint8_t* buf, std::size_t n);

} // namespace gc
