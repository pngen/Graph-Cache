// Graph Cache worker process. Registers with the coordinator over framed TCP and
// executes capture/replay instructions under coordinator authority.
// Copyright 2026 Summon Software Labs. Apache License 2.0.

#include "graphcache/backend.hpp"
#include "graphcache/canonical.hpp"
#include "graphcache/ids.hpp"
#include "graphcache/protocol.hpp"
#include "graphcache/serialization.hpp"
#include "graphcache/topology.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <process.h>
#include <span>
#include <string>
#include <vector>

namespace {

void send_quiet(std::int64_t s, const gc::ProtocolMessage& m) {
  auto r = gc::send_frame(s, m); (void)r;
}
void close_quiet(const gc::TcpSocket& sock) {
  auto r = gc::tcp_close(sock); (void)r;
}

struct WorkerState {
  std::unique_ptr<gc::GraphBackend> backend;
  std::map<gc::GraphArtifactId, gc::GraphDescriptor> descs;
  std::map<gc::GraphArtifactId, std::shared_ptr<void>> execs;
  std::uint64_t worker_id{0};
  std::uint64_t boot_id{0};
};

struct DecodedCapture {
  gc::GraphDescriptor descriptor;
  gc::WorkloadIdentity workload;
};

gc::Result<DecodedCapture> decode_capture(const std::vector<std::uint8_t>& payload) {
  gc::CanonicalReader r(payload);
  std::uint16_t tag;
  std::span<const std::uint8_t> p;
  if (!r.next(tag, p)) return gc::Result<DecodedCapture>::failure(gc::Error(gc::ErrorCode::ProtocolMalformed, "capture payload"));
  DecodedCapture dc;
  if (tag != 1) return gc::Result<DecodedCapture>::failure(gc::Error(gc::ErrorCode::ProtocolMalformed, "expected descriptor"));
  auto dres = gc::deserialize_descriptor(p);
  if (!dres.ok()) return gc::Result<DecodedCapture>::failure(dres.error());
  dc.descriptor = std::move(dres.value());
  if (!r.next(tag, p) || tag != 2) return gc::Result<DecodedCapture>::failure(gc::Error(gc::ErrorCode::ProtocolMalformed, "wl_name"));
  if (!gc::CanonicalReader::decode_str(p, dc.workload.logical_name)) return gc::Result<DecodedCapture>::failure(gc::Error(gc::ErrorCode::ProtocolMalformed, "wl_name"));
  if (!r.next(tag, p) || tag != 3) return gc::Result<DecodedCapture>::failure(gc::Error(gc::ErrorCode::ProtocolMalformed, "wl_ver"));
  { std::uint32_t v; if (!gc::CanonicalReader::decode_u32(p, v)) return gc::Result<DecodedCapture>::failure(gc::Error(gc::ErrorCode::ProtocolMalformed, "wl_ver")); dc.workload.logical_version = v; }
  if (!r.next(tag, p) || tag != 4) return gc::Result<DecodedCapture>::failure(gc::Error(gc::ErrorCode::ProtocolMalformed, "wl_ns"));
  if (!gc::CanonicalReader::decode_str(p, dc.workload.namespace_name)) return gc::Result<DecodedCapture>::failure(gc::Error(gc::ErrorCode::ProtocolMalformed, "wl_ns"));
  if (!r.next(tag, p) || tag != 5) return gc::Result<DecodedCapture>::failure(gc::Error(gc::ErrorCode::ProtocolMalformed, "wl_modelrev"));
  if (!gc::CanonicalReader::decode_str(p, dc.workload.model_operator_revision)) return gc::Result<DecodedCapture>::failure(gc::Error(gc::ErrorCode::ProtocolMalformed, "wl_modelrev"));
  return gc::Result<DecodedCapture>::success(std::move(dc));
}

std::vector<std::uint8_t> encode_replay_payload(const std::vector<float>& in) {
  gc::CanonicalWriter w;
  w.put_u32(1, static_cast<std::uint32_t>(in.size()));
  for (float v : in) w.put_f64(2, static_cast<double>(v));
  return w.take();
}

gc::Result<std::vector<float>> decode_replay_input(const std::vector<std::uint8_t>& payload) {
  gc::CanonicalReader r(payload);
  std::uint16_t tag;
  std::span<const std::uint8_t> p;
  std::vector<float> out;
  if (!r.next(tag, p) || tag != 1) return gc::Result<std::vector<float>>::failure(gc::Error(gc::ErrorCode::ProtocolMalformed, "replay count"));
  std::uint32_t n;
  if (!gc::CanonicalReader::decode_u32(p, n) || n > (1u << 20)) return gc::Result<std::vector<float>>::failure(gc::Error(gc::ErrorCode::ProtocolMalformed, "replay count decode"));
  out.reserve(n);
  for (std::uint32_t i = 0; i < n; ++i) {
    if (!r.next(tag, p) || tag != 2) return gc::Result<std::vector<float>>::failure(gc::Error(gc::ErrorCode::ProtocolMalformed, "replay element"));
    std::uint64_t bits;
    if (!gc::CanonicalReader::decode_u64(p, bits)) return gc::Result<std::vector<float>>::failure(gc::Error(gc::ErrorCode::ProtocolMalformed, "replay element decode"));
    double d; std::memcpy(&d, &bits, sizeof(d));
    out.push_back(static_cast<float>(d));
  }
  return gc::Result<std::vector<float>>::success(std::move(out));
}

std::vector<std::uint8_t> encode_replay_output(const std::vector<float>& out) {
  gc::CanonicalWriter w;
  w.put_u32(1, static_cast<std::uint32_t>(out.size()));
  for (float v : out) w.put_f64(2, static_cast<double>(v));
  return w.take();
}

// artifact_hex prefix tag for replay instruct (tag 1), then remaining payload.
gc::Result<std::pair<std::string, std::vector<float>>> decode_replay_instruct(const std::vector<std::uint8_t>& payload) {
  gc::CanonicalReader r(payload);
  std::uint16_t tag; std::span<const std::uint8_t> p;
  if (!r.next(tag, p) || tag != 1) return gc::Result<std::pair<std::string, std::vector<float>>>::failure(gc::Error(gc::ErrorCode::ProtocolMalformed, "artifact"));
  std::string artifact_hex;
  if (!gc::CanonicalReader::decode_str(p, artifact_hex)) return gc::Result<std::pair<std::string, std::vector<float>>>::failure(gc::Error(gc::ErrorCode::ProtocolMalformed, "artifact hex"));
  // layout: [1 artifact hex][2 count][3 ... floats]
  if (!r.next(tag, p) || tag != 2) return gc::Result<std::pair<std::string, std::vector<float>>>::failure(gc::Error(gc::ErrorCode::ProtocolMalformed, "replay count"));
  std::uint32_t n;
  if (!gc::CanonicalReader::decode_u32(p, n) || n > (1u << 20)) return gc::Result<std::pair<std::string, std::vector<float>>>::failure(gc::Error(gc::ErrorCode::ProtocolMalformed, "replay count decode"));
  std::vector<float> out; out.reserve(n);
  for (std::uint32_t i = 0; i < n; ++i) {
    if (!r.next(tag, p) || tag != 3) return gc::Result<std::pair<std::string, std::vector<float>>>::failure(gc::Error(gc::ErrorCode::ProtocolMalformed, "replay element"));
    std::uint64_t bits; if (!gc::CanonicalReader::decode_u64(p, bits)) return gc::Result<std::pair<std::string, std::vector<float>>>::failure(gc::Error(gc::ErrorCode::ProtocolMalformed, "replay element decode"));
    double d; std::memcpy(&d, &bits, sizeof(d)); out.push_back(static_cast<float>(d));
  }
  return gc::Result<std::pair<std::string, std::vector<float>>>::success(std::make_pair(artifact_hex, std::move(out)));
}

std::string artifact_id_hex_lookup(const gc::GraphArtifactId& id) {
  return id.to_string();
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: gc-worker <coordinator-port> <worker-id>\n");
    return 2;
  }
  std::uint16_t port = static_cast<std::uint16_t>(std::atoi(argv[1]));
  WorkerState st;
  st.worker_id = static_cast<std::uint64_t>(std::atoi(argv[2]));
  st.boot_id = (static_cast<std::uint64_t>(::_getpid()) << 1) ^ 0x9E3779B97F4A7C15ULL;
  st.backend = gc::create_backend(gc::BackendKind::Cpu);
  if (!st.backend) { std::fprintf(stderr, "worker: backend unavailable\n"); return 2; }

  auto conn = gc::tcp_connect("127.0.0.1", port);
  if (!conn.ok()) { std::fprintf(stderr, "worker: connect failed\n"); return 2; }
  gc::TcpSocket sock = conn.value();
  const std::int64_t s = sock.handle;

  gc::ProtocolMessage reg;
  reg.type = gc::ProtocolMessageType::Register;
  reg.worker_id = gc::WorkerId(st.worker_id);
  reg.worker_boot = gc::WorkerBootId(st.boot_id);
  reg.epoch = gc::CoordinatorEpoch(1);
  if (!gc::send_frame(s, reg).ok()) { close_quiet(sock); return 2; }

  auto ack = gc::recv_frame(s);
  if (!ack.ok() || ack->type != gc::ProtocolMessageType::RegisterAck) { close_quiet(sock); return 2; }
  if (ack->status != gc::ProtocolStatus::Ok) { close_quiet(sock); return 1; }

  while (true) {
    auto msg = gc::recv_frame(s);
    if (!msg.ok()) break;
    const auto& m = msg.value();
    if (m.type == gc::ProtocolMessageType::Shutdown) break;

    gc::ProtocolMessage resp;
    resp.type = m.type == gc::ProtocolMessageType::CaptureInstruct ? gc::ProtocolMessageType::CaptureResult
                                                                   : gc::ProtocolMessageType::ReplayResult;
    resp.request_id = m.request_id;
    resp.epoch = m.epoch;          // echo the coordinator's current authority
    resp.cache_gen = m.cache_gen;  // from the instruction, not the stale register-time value
    resp.worker_id = gc::WorkerId(st.worker_id);
    resp.worker_boot = gc::WorkerBootId(st.boot_id);
    resp.capture_attempt = m.capture_attempt;
    resp.replay_attempt = m.replay_attempt;
    resp.graph_gen = m.graph_gen;
    resp.status = gc::ProtocolStatus::Ok;

    if (m.type == gc::ProtocolMessageType::CaptureInstruct) {
      auto dc = decode_capture(m.payload);
      if (!dc.ok()) { resp.status = gc::ProtocolStatus::CaptureFailed; send_quiet(s, resp); continue; }
      auto desc = dc->descriptor;
      if (!desc.finalize().ok()) { resp.status = gc::ProtocolStatus::CaptureFailed; send_quiet(s, resp); continue; }
      auto cap = st.backend->capture(desc, gc::CaptureAttemptId(m.capture_attempt.value));
      if (!cap.ok()) { resp.status = gc::ProtocolStatus::CaptureFailed; send_quiet(s, resp); continue; }
      if (!st.backend->validate(desc, cap.value()).ok()) { resp.status = gc::ProtocolStatus::CaptureFailed; send_quiet(s, resp); continue; }
      st.descs[desc.artifact_id] = desc;
      st.execs[desc.artifact_id] = cap.value();
      gc::CanonicalWriter w;
      w.put_str(1, artifact_id_hex_lookup(desc.artifact_id));
      w.put_u32(2, static_cast<std::uint32_t>(desc.nodes.size()));
      resp.payload = w.take();
      send_quiet(s, resp);
    } else if (m.type == gc::ProtocolMessageType::ReplayInstruct) {
      auto dc = decode_replay_instruct(m.payload);
      if (!dc.ok()) { resp.status = gc::ProtocolStatus::ReplayFailed; send_quiet(s, resp); continue; }
      const std::string& hex = dc->first;
      gc::GraphArtifactId target;
      bool found = false;
      for (const auto& [aid, _] : st.descs) {
        if (aid.to_string() == hex) { target = aid; found = true; break; }
      }
      if (!found || st.execs.find(target) == st.execs.end()) { std::fprintf(stderr, "worker: replay target not found (%s)\n", hex.c_str()); resp.status = gc::ProtocolStatus::ReplayFailed; send_quiet(s, resp); continue; }
      std::vector<float> out(dc->second.size(), 0.0f);
      gc::ReplayBuffers bufs;
      bufs.inputs = {dc->second.data()};
      bufs.outputs = {out.data()};
      bufs.input_bytes = {dc->second.size() * sizeof(float)};
      bufs.output_bytes = {out.size() * sizeof(float)};
      gc::ReplayDescriptor rd;
      rd.replay_attempt = gc::ReplayAttemptId(m.replay_attempt.value);
      auto replay = st.backend->replay(st.descs[target], st.execs[target], bufs, rd);
      if (!replay.ok()) { std::fprintf(stderr, "worker: replay failed: %s\n", replay.error().message.c_str()); resp.status = gc::ProtocolStatus::ReplayFailed; send_quiet(s, resp); continue; }
      resp.payload = encode_replay_output(out);
      send_quiet(s, resp);
    }
  }
  close_quiet(sock);
  return 0;
}