#define _CRT_SECURE_NO_WARNINGS
// Graph Cache coordinator process. Owns generation authority (epoch, cache
// generation, graph generation) and routes capture/replay to workers over real
// framed TCP, rejecting stale authority.
// Copyright 2026 Summon Software Labs. Apache License 2.0.

#include "graphcache/canonical.hpp"
#include "graphcache/compatibility.hpp"
#include "graphcache/ids.hpp"
#include "graphcache/protocol.hpp"
#include "graphcache/serialization.hpp"
#include "graphcache/topology.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>

namespace {

struct Conn {
  std::int64_t sock{-1};
  bool is_worker{false};
  bool is_client{false};
  std::uint64_t worker_id{0};
  std::uint64_t worker_boot{0};
  std::vector<std::uint8_t> buf;
};

struct Entry {
  gc::GraphArtifactId artifact;
  gc::GraphGeneration generation;
  std::uint64_t worker_id{0};
  std::uint64_t cache_gen_at_publish{0};
};

struct PendingCapture {
  std::int64_t client_sock{-1};
  std::uint64_t worker_id{0};
  std::uint64_t request_id{0};
  std::string key;
};

struct State {
  gc::CoordinatorEpoch epoch{1};
  gc::CacheGeneration cache_gen{1};
  std::uint64_t graph_gen_counter{1};
  std::uint64_t capture_counter{0};
  std::map<std::uint64_t, std::uint64_t> worker_boot;   // worker_id -> latest boot
  std::set<std::uint64_t> worker_connected;
  std::map<std::uint64_t, std::int64_t> worker_sock;
  std::map<std::string, Entry> graphs;                  // compat key hex -> entry
  std::set<std::string> invalidated;
  std::map<std::uint64_t, PendingCapture> pending;      // capture_attempt -> pending
  std::map<std::uint64_t, std::int64_t> pending_replays;  // request_id -> client socket
};

void send_quiet(std::int64_t s, const gc::ProtocolMessage& m) { auto r = gc::send_frame(s, m); (void)r; }
void close_quiet(const gc::TcpSocket& sock) { auto r = gc::tcp_close(sock); (void)r; }

void fill_conn(std::map<std::int64_t, Conn>& conns, std::int64_t s) {
  Conn c; c.sock = s; conns[s] = c;
}

} // namespace

int main(int argc, char** argv) {
  std::uint16_t port = argc >= 2 ? static_cast<std::uint16_t>(std::atoi(argv[1])) : 41000;
  auto listener = gc::tcp_listen(port);
  if (!listener.ok()) {
    std::fprintf(stderr, "coordinator: listen failed\n");
    return 2;
  }
  State st;
  std::map<std::int64_t, Conn> conns;
  conns[listener->handle] = [&] { Conn c; c.sock = listener->handle; return c; }();

  std::fprintf(stderr, "coordinator: listening on %u; epoch=%llu cache_gen=%llu\n",
              port, st.epoch.value, st.cache_gen.value);

  auto send_status = [&](std::int64_t sock, gc::ProtocolMessageType type, std::uint64_t request_id,
                         gc::ProtocolStatus status, gc::CoordinatorEpoch epoch,
                         gc::CacheGeneration cg = gc::CacheGeneration()) {
    gc::ProtocolMessage m;
    m.type = type;
    m.request_id = request_id;
    m.epoch = epoch;
    m.cache_gen = cg.valid() ? cg : st.cache_gen;
    m.status = status;
    send_quiet(sock, m);
  };

  bool running = true;
  while (running) {
    fd_set fds;
    FD_ZERO(&fds);
    int maxfd = 0;
    for (auto& [s, c] : conns) {
      if (s >= 0) {
        FD_SET(static_cast<SOCKET>(s), &fds);
        if (s > maxfd) maxfd = static_cast<int>(s);
      }
    }
    timeval tv{1, 0};
    int rc = select(maxfd + 1, &fds, nullptr, nullptr, &tv);
    if (rc == SOCKET_ERROR) { break; }
    if (rc == 0) continue;

    // Accept new connections.
    if (FD_ISSET(static_cast<SOCKET>(listener->handle), &fds)) {
      auto acc = gc::tcp_accept(listener.value());
      if (acc.ok()) {
        fill_conn(conns, acc->handle);
        std::fprintf(stderr, "coordinator: accepted connection\n");
      }
    }

    std::vector<std::int64_t> to_close;
    for (auto& [s, c] : conns) {
      if (s == listener->handle) continue;
      if (!FD_ISSET(static_cast<SOCKET>(s), &fds)) continue;
      // Read available bytes.
      std::uint8_t tmp[16384];
      int nb = recv(static_cast<SOCKET>(s), reinterpret_cast<char*>(tmp), sizeof(tmp), 0);
      if (nb == 0 || nb == SOCKET_ERROR) { to_close.push_back(s); continue; }
      c.buf.insert(c.buf.end(), tmp, tmp + nb);

      // Extract complete frames.
      bool progress = true;
      while (progress && c.buf.size() >= 4) {
        std::uint32_t len = static_cast<std::uint32_t>(c.buf[0]) |
                            (static_cast<std::uint32_t>(c.buf[1]) << 8) |
                            (static_cast<std::uint32_t>(c.buf[2]) << 16) |
                            (static_cast<std::uint32_t>(c.buf[3]) << 24);
        if (len == 0) { c.buf.erase(c.buf.begin(), c.buf.begin() + 4); progress = true; continue; }
        if (len > gc::MaxFrameSize) { to_close.push_back(s); progress = false; break; }
        if (c.buf.size() < 4 + len) { progress = false; break; }
        std::vector<std::uint8_t> body(c.buf.begin() + 4, c.buf.begin() + 4 + len);
        c.buf.erase(c.buf.begin(), c.buf.begin() + 4 + len);
        auto msg = gc::decode_message(body);
        if (!msg.ok()) {
          send_status(s, gc::ProtocolMessageType::LookupResult, 0, gc::ProtocolStatus::Malformed, st.epoch);
          continue;
        }
        const auto& m = msg.value();

        auto is_stale_epoch = m.epoch.valid() && m.epoch != st.epoch;
        auto boot_for = [&](std::uint64_t wid) -> std::uint64_t {
          auto it = st.worker_boot.find(wid);
          return it == st.worker_boot.end() ? 0 : it->second;
        };

        if (m.type == gc::ProtocolMessageType::Register) {
          if (is_stale_epoch) {
            send_status(s, gc::ProtocolMessageType::RegisterAck, m.request_id, gc::ProtocolStatus::StaleEpoch, st.epoch);
            continue;
          }
          std::uint64_t wid = m.worker_id.value;
          std::uint64_t boot = m.worker_boot.value;
          // If this worker is currently connected with a different (known) boot, a
          // register with a stale boot is rejected.
          if (st.worker_connected.count(wid) && boot_for(wid) != 0 && boot_for(wid) != boot) {
            send_status(s, gc::ProtocolMessageType::RegisterAck, m.request_id, gc::ProtocolStatus::StaleWorkerBoot, st.epoch);
            continue;
          }
          c.is_worker = true;
          c.worker_id = wid;
          c.worker_boot = boot;
          st.worker_boot[wid] = boot;
          st.worker_connected.insert(wid);
          st.worker_sock[wid] = s;
          std::fprintf(stderr, "coordinator: worker %llu registered boot=%llu\n", wid, boot);
          send_status(s, gc::ProtocolMessageType::RegisterAck, m.request_id, gc::ProtocolStatus::Ok, st.epoch, st.cache_gen);
          continue;
        }

        if (m.type == gc::ProtocolMessageType::Hello) {
          std::fprintf(stderr, "coordinator: Hello from client\n");
          c.is_client = true;
          if (is_stale_epoch) {
            send_status(s, gc::ProtocolMessageType::Welcome, m.request_id, gc::ProtocolStatus::StaleEpoch, st.epoch);
          } else {
            send_status(s, gc::ProtocolMessageType::Welcome, m.request_id, gc::ProtocolStatus::Ok, st.epoch, st.cache_gen);
          }
          continue;
        }

        // Authority checks for all other messages.
        if (is_stale_epoch) {
          send_status(s, m.type == gc::ProtocolMessageType::Lookup ? gc::ProtocolMessageType::LookupResult : (
                      m.type == gc::ProtocolMessageType::ReplayInstruct ? gc::ProtocolMessageType::ReplayResult : gc::ProtocolMessageType::LookupResult),
                      m.request_id, gc::ProtocolStatus::StaleEpoch, st.epoch);
          continue;
        }
        if (m.cache_gen.valid() && m.cache_gen != st.cache_gen) {
          send_status(s, gc::ProtocolMessageType::LookupResult, m.request_id, gc::ProtocolStatus::StaleCacheGeneration, st.epoch);
          continue;
        }
        if (m.worker_id.valid() && boot_for(m.worker_id.value) != 0 &&
            m.worker_boot.value != 0 && m.worker_boot.value != boot_for(m.worker_id.value)) {
          // A worker-sourced message carrying a stale boot is rejected.
          send_status(s, gc::ProtocolMessageType::LookupResult, m.request_id, gc::ProtocolStatus::StaleWorkerBoot, st.epoch);
          continue;
        }

        if (m.type == gc::ProtocolMessageType::Lookup) {
          std::fprintf(stderr, "coordinator: Lookup received\n");
          // Decode descriptor + workload from payload.
          gc::CanonicalReader r(m.payload);
          std::uint16_t tag; std::span<const std::uint8_t> p;
          if (!r.next(tag, p) || tag != 1) { send_status(s, gc::ProtocolMessageType::LookupResult, m.request_id, gc::ProtocolStatus::InvalidRequest, st.epoch); continue; }
          auto dres = gc::deserialize_descriptor(p);
          if (!dres.ok()) { send_status(s, gc::ProtocolMessageType::LookupResult, m.request_id, gc::ProtocolStatus::Malformed, st.epoch); continue; }
          auto desc = dres.value();
          gc::WorkloadIdentity wl;
          if (!r.next(tag, p) || tag != 2 || !gc::CanonicalReader::decode_str(p, wl.logical_name)) { send_status(s, gc::ProtocolMessageType::LookupResult, m.request_id, gc::ProtocolStatus::InvalidRequest, st.epoch); continue; }
          if (!r.next(tag, p) || tag != 3) { send_status(s, gc::ProtocolMessageType::LookupResult, m.request_id, gc::ProtocolStatus::InvalidRequest, st.epoch); continue; }
          { std::uint32_t v; if (!gc::CanonicalReader::decode_u32(p, v)) { send_status(s, gc::ProtocolMessageType::LookupResult, m.request_id, gc::ProtocolStatus::InvalidRequest, st.epoch); continue; } wl.logical_version = v; }
          if (!r.next(tag, p) || tag != 4) { send_status(s, gc::ProtocolMessageType::LookupResult, m.request_id, gc::ProtocolStatus::InvalidRequest, st.epoch); continue; }
          if (!gc::CanonicalReader::decode_str(p, wl.namespace_name)) { send_status(s, gc::ProtocolMessageType::LookupResult, m.request_id, gc::ProtocolStatus::InvalidRequest, st.epoch); continue; }
          if (!r.next(tag, p) || tag != 5) { send_status(s, gc::ProtocolMessageType::LookupResult, m.request_id, gc::ProtocolStatus::InvalidRequest, st.epoch); continue; }
          if (!gc::CanonicalReader::decode_str(p, wl.model_operator_revision)) { send_status(s, gc::ProtocolMessageType::LookupResult, m.request_id, gc::ProtocolStatus::InvalidRequest, st.epoch); continue; }

          gc::CompatibilityFacts facts;
          facts.workload = wl;
          facts.backend = desc.backend;
          facts.runtime = desc.runtime;
          facts.device = desc.device;
          facts.topology_canonical = gc::canonical_topology(desc.nodes, desc.edges);
          for (const auto& n : desc.nodes) {
            if (n.datatype != gc::Datatype::None) {
              facts.datatypes.push_back(n.datatype);
              facts.layouts.push_back(n.layout);
              if (n.scalar.specialized) facts.scalar = n.scalar;
            }
          }
          facts.dependencies = desc.dependencies;
          facts.model_operator_revision = wl.model_operator_revision;
          facts.policy_generation = wl.policy_generation;
          auto key = gc::GraphCompatibilityKey::build(facts);
          std::string keyhex = key.ok() ? key.value().digest_hex() : std::string();

          auto git = st.graphs.find(keyhex);
          if (git != st.graphs.end() && !st.invalidated.count(keyhex)) {
            // Exact hit under current authority.
            gc::ProtocolMessage resp;
            resp.type = gc::ProtocolMessageType::LookupResult;
            resp.request_id = m.request_id;
            resp.epoch = st.epoch;
            resp.cache_gen = st.cache_gen;
            resp.status = gc::ProtocolStatus::Ok;
            resp.graph_gen = git->second.generation;
            gc::CanonicalWriter w;
            w.put_str(1, git->second.artifact.to_string());
            resp.payload = w.take();
            send_quiet(s, resp);
            continue;
          }
          // Miss: dispatch a capture to an available worker.
          if (st.worker_connected.empty()) {
            send_status(s, gc::ProtocolMessageType::LookupResult, m.request_id, gc::ProtocolStatus::WorkerUnavailable, st.epoch);
            continue;
          }
          auto wids = st.worker_connected;
          std::uint64_t wid = *wids.begin();
          std::uint64_t cap_attempt = ++st.capture_counter;
          gc::ProtocolMessage inst;
          inst.type = gc::ProtocolMessageType::CaptureInstruct;
          inst.epoch = st.epoch;
          inst.cache_gen = st.cache_gen;
          inst.worker_id = gc::WorkerId(wid);
          inst.worker_boot = gc::WorkerBootId(st.worker_boot[wid]);
          inst.capture_attempt = gc::CaptureAttemptId(cap_attempt);
          gc::CanonicalWriter w;
          std::vector<std::uint8_t> desc_bytes;
          [[maybe_unused]] auto _sd = gc::serialize_descriptor(desc, desc_bytes);
          w.put_bytes(1, desc_bytes);
          w.put_str(2, wl.logical_name);
          w.put_u32(3, wl.logical_version);
          w.put_str(4, wl.namespace_name);
          w.put_str(5, wl.model_operator_revision);
          inst.payload = w.take();
          std::fprintf(stderr, "coordinator: dispatching capture attempt %llu to worker %llu\n", cap_attempt, wid);
          send_quiet(st.worker_sock[wid], inst);
          st.pending[cap_attempt] = PendingCapture{s, wid, m.request_id, keyhex};
          continue;
        }

        if (m.type == gc::ProtocolMessageType::CaptureResult) {
          std::fprintf(stderr, "coordinator: CaptureResult attempt=%llu status=%u\n", m.capture_attempt.value, static_cast<unsigned>(m.status));
          std::uint64_t cap_attempt = m.capture_attempt.value;
          auto pit = st.pending.find(cap_attempt);
          if (pit == st.pending.end()) {
            send_status(s, gc::ProtocolMessageType::LookupResult, m.request_id, gc::ProtocolStatus::DuplicateCaptureCompletion, st.epoch);
            continue;
          }
          PendingCapture pc = pit->second;
          st.pending.erase(pit);
          if (m.status != gc::ProtocolStatus::Ok) {
            send_status(pc.client_sock, gc::ProtocolMessageType::LookupResult, pc.request_id, gc::ProtocolStatus::CaptureFailed, st.epoch, st.cache_gen);
            continue;
          }
          // Assign a fresh graph generation under the current authority.
          gc::GraphGeneration gen(++st.graph_gen_counter);
          // Decode artifact hex.
          gc::CanonicalReader r(m.payload);
          std::uint16_t tag; std::span<const std::uint8_t> p;
          std::string artifact_hex;
          if (!r.next(tag, p) || tag != 1 || !gc::CanonicalReader::decode_str(p, artifact_hex)) {
            send_status(pc.client_sock, gc::ProtocolMessageType::LookupResult, pc.request_id, gc::ProtocolStatus::CaptureFailed, st.epoch, st.cache_gen);
            continue;
          }
          gc::GraphArtifactId aid;
          // Parse "HHHH:...." into hi/lo.
          std::uint64_t hi = 0, lo = 0;
          std::sscanf(artifact_hex.c_str(), "%16llx:%16llx", &hi, &lo);
          aid = gc::GraphArtifactId(hi, lo);
          // Store graph keyed by the key computed at lookup time; we recompute the key
          // by re-deriving facts from the capture descriptor. For this proof the
          // capture result does not carry the key, so the coordinator stores it by
          // worker's reported key. We retrieve the key stored in pending metadata.
          // (The key is not stored; we rely on the client's key being derivable at
          // replay. To keep authority simple, store the graph under the artifact.)
          st.graphs[std::string("artifact:") + artifact_hex] = Entry{aid, gen, pc.worker_id, st.cache_gen.value};
          if (!pc.key.empty()) {
            st.graphs[pc.key] = Entry{aid, gen, pc.worker_id, st.cache_gen.value};
            st.invalidated.erase(pc.key);
          }
          st.invalidated.erase(std::string("artifact:") + artifact_hex);
          gc::ProtocolMessage resp;
          resp.type = gc::ProtocolMessageType::LookupResult;
          resp.request_id = pc.request_id;
          resp.epoch = st.epoch;
          resp.cache_gen = st.cache_gen;
          resp.status = gc::ProtocolStatus::Ok;
          resp.graph_gen = gen;
          gc::CanonicalWriter w;
          w.put_str(1, artifact_hex);
          resp.payload = w.take();
          send_quiet(pc.client_sock, resp);
          continue;
        }

        if (m.type == gc::ProtocolMessageType::ReplayInstruct) {
          // Client asks to replay a captured graph.
          gc::CanonicalReader r(m.payload);
          std::uint16_t tag; std::span<const std::uint8_t> p;
          std::string artifact_hex;
          if (!r.next(tag, p) || tag != 1 || !gc::CanonicalReader::decode_str(p, artifact_hex)) {
            send_status(s, gc::ProtocolMessageType::ReplayResult, m.request_id, gc::ProtocolStatus::InvalidRequest, st.epoch);
            continue;
          }
          auto git = st.graphs.find(std::string("artifact:") + artifact_hex);
          if (git == st.graphs.end() || st.invalidated.count(std::string("artifact:") + artifact_hex)) {
            send_status(s, gc::ProtocolMessageType::ReplayResult, m.request_id, gc::ProtocolStatus::StaleGraphGeneration, st.epoch);
            continue;
          }
          if (m.graph_gen.valid() && m.graph_gen != git->second.generation) {
            send_status(s, gc::ProtocolMessageType::ReplayResult, m.request_id, gc::ProtocolStatus::StaleGraphGeneration, st.epoch);
            continue;
          }
          std::uint64_t wid = git->second.worker_id;
          if (st.worker_sock.find(wid) == st.worker_sock.end()) {
            send_status(s, gc::ProtocolMessageType::ReplayResult, m.request_id, gc::ProtocolStatus::WorkerUnavailable, st.epoch);
            continue;
          }
          gc::ProtocolMessage ri;
          ri.type = gc::ProtocolMessageType::ReplayInstruct;
          ri.request_id = m.request_id;
          ri.epoch = st.epoch;
          ri.cache_gen = st.cache_gen;
          ri.worker_id = gc::WorkerId(wid);
          ri.worker_boot = gc::WorkerBootId(st.worker_boot[wid]);
          ri.graph_gen = git->second.generation;
          ri.replay_attempt = m.replay_attempt;
          // Forward the artifact + input.
          ri.payload = m.payload;
          st.pending_replays[m.request_id] = s;
          send_quiet(st.worker_sock[wid], ri);
          continue;
        }

        if (m.type == gc::ProtocolMessageType::ReplayResult) {
          gc::ProtocolMessage resp;
          resp.type = gc::ProtocolMessageType::ReplayResult;
          resp.request_id = m.request_id;
          resp.epoch = st.epoch;
          resp.cache_gen = st.cache_gen;
          resp.status = m.status;
          resp.payload = m.payload;
          auto rit = st.pending_replays.find(m.request_id);
          std::int64_t target = rit != st.pending_replays.end() ? rit->second : s;
          if (rit != st.pending_replays.end()) st.pending_replays.erase(rit);
          send_quiet(target, resp);
          continue;
        }

        if (m.type == gc::ProtocolMessageType::Invalidate) {
          st.cache_gen = gc::CacheGeneration(st.cache_gen.value + 1);
          // Roll the coordinator epoch when the caller requests it (flags bit 0).
          if (m.flags & 0x01u) {
            st.epoch = gc::CoordinatorEpoch(st.epoch.value + 1);
          }
          // Invalidate all graphs (atomic proof uses whole-cache invalidation).
          for (auto& [k, e] : st.graphs) st.invalidated.insert(k);
          gc::ProtocolMessage resp;
          resp.type = gc::ProtocolMessageType::InvalidateAck;
          resp.request_id = m.request_id;
          resp.epoch = st.epoch;
          resp.cache_gen = st.cache_gen;
          resp.status = gc::ProtocolStatus::Ok;
          send_quiet(s, resp);
          continue;
        }

        if (m.type == gc::ProtocolMessageType::Shutdown) { running = false; }
      }
    }

    for (auto s : to_close) {
      auto it = conns.find(s);
      if (it != conns.end()) {
        if (it->second.is_worker) st.worker_connected.erase(it->second.worker_id);
        conns.erase(it);
      }
      close_quiet(gc::TcpSocket{s});
    }
  }
  return 0;
}