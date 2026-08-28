// Atomic multiprocess stale-authority proof. Spawns a REAL coordinator process,
// two REAL worker processes, connects a client over framed TCP, and proves
// generation/epoch/worker-boot stale-authority rejection and fresh success in
// ONE scenario.
// Copyright 2026 Summon Software Labs. Apache License 2.0.

#include "test_util.hpp"

#include "graphcache/canonical.hpp"
#include "graphcache/protocol.hpp"
#include "graphcache/serialization.hpp"

#include <windows.h>
#include <process.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

void send_quiet(std::int64_t s, const gc::ProtocolMessage& m) { auto r = gc::send_frame(s, m); (void)r; }

struct Proc {
  HANDLE process{nullptr};
  DWORD pid{0};
};

Proc spawn(const std::string& exe, const std::string& args) {
  Proc p;
  std::string cmd = "\"" + exe + "\" " + args;
  STARTUPINFOA si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  std::vector<char> cmdline(cmd.begin(), cmd.end()); cmdline.push_back('\0');
  BOOL ok = CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, FALSE, CREATE_NEW_PROCESS_GROUP,
                           nullptr, nullptr, &si, &pi);
  if (!ok) { std::printf("gc_test: spawn failed (%lu) for %s\n", GetLastError(), cmd.c_str()); return p; }
  p.process = pi.hProcess; p.pid = pi.dwProcessId; CloseHandle(pi.hThread);
  return p;
}

void terminate(Proc& p) {
  if (p.process) { TerminateProcess(p.process, 1); WaitForSingleObject(p.process, 5000); CloseHandle(p.process); p.process = nullptr; }
}

std::string dir_of_exe() {
  char buf[MAX_PATH]; DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
  std::string s(buf, n); auto pos = s.find_last_of("\\/"); return s.substr(0, pos);
}
bool recv_msg(std::int64_t s, gc::ProtocolMessage& out) { auto r = gc::recv_frame(s); if (!r.ok()) return false; out = std::move(r.value()); return true; }

std::vector<std::uint8_t> encode_lookup(const gc::GraphDescriptor& desc, const gc::WorkloadIdentity& wl) {
  std::vector<std::uint8_t> dbytes;
  [[maybe_unused]] auto _sd = gc::serialize_descriptor(desc, dbytes);
  gc::CanonicalWriter w; w.put_bytes(1, dbytes); w.put_str(2, wl.logical_name);
  w.put_u32(3, wl.logical_version); w.put_str(4, wl.namespace_name); w.put_str(5, wl.model_operator_revision);
  return w.take();
}
std::vector<std::uint8_t> encode_replay(const std::string& artifact_hex, const std::vector<float>& in) {
  gc::CanonicalWriter w; w.put_str(1, artifact_hex); w.put_u32(2, static_cast<std::uint32_t>(in.size()));
  for (float v : in) w.put_f64(3, static_cast<double>(v)); return w.take();
}
bool decode_artifact(const gc::ProtocolMessage& m, std::string& hex) {
  gc::CanonicalReader r(m.payload); std::uint16_t tag; std::span<const std::uint8_t> p;
  if (!r.next(tag, p) || tag != 1) return false; return gc::CanonicalReader::decode_str(p, hex);
}
bool decode_str(const gc::ProtocolMessage& m, std::string& out) {
  gc::CanonicalReader r(m.payload); std::uint16_t tag; std::span<const std::uint8_t> p;
  if (!r.next(tag, p) || tag != 1) return false; return gc::CanonicalReader::decode_str(p, out);
}
bool decode_replay_output(const gc::ProtocolMessage& m, std::vector<float>& out) {
  gc::CanonicalReader r(m.payload); std::uint16_t tag; std::span<const std::uint8_t> p;
  if (!r.next(tag, p) || tag != 1) return false; std::uint32_t n;
  if (!gc::CanonicalReader::decode_u32(p, n) || n > (1u << 20)) return false;
  out.resize(n);
  for (std::uint32_t i = 0; i < n; ++i) {
    if (!r.next(tag, p) || tag != 2) return false;
    std::uint64_t bits; if (!gc::CanonicalReader::decode_u64(p, bits)) return false;
    double d; std::memcpy(&d, &bits, sizeof(d)); out[i] = static_cast<float>(d);
  }
  return true;
}
gc::GraphDescriptor make_cpu_desc(std::uint64_t bytes) {
  gc::GraphDescriptor desc;
  desc.backend.kind = gc::BackendKind::Cpu; desc.backend.backend_name = "cpu"; desc.backend.backend_version = 1;
  desc.runtime.runtime_version = 1; desc.runtime.graph_abi = 1; desc.runtime.kernel_abi = 1;
  desc.device.vendor = gc::AcceleratorVendor::Cpu; desc.device.architecture = "cpu";
  gc::GraphNodeDescriptor copy; copy.node_id = gc::GraphNodeId(1,1); copy.category = gc::NodeCategory::MemoryCopy;
  copy.name = "copy"; copy.memory_bytes = bytes; copy.reactive_input_indices = {0}; copy.reactive_output_indices = {0};
  copy.binding.binding_class = gc::BindingClass::ReplayMutableBinding; copy.binding.rebinding_eligible = true;
  gc::GraphNodeDescriptor k; k.node_id = gc::GraphNodeId(2,2); k.category = gc::NodeCategory::Kernel; k.name = "scale";
  k.kernel.name = "scale"; k.kernel.id_hi = 0x5000; k.kernel.id_lo = 1; k.kernel.generation = gc::GraphGeneration(1);
  k.kernel.abi = 1; k.kernel.content_digest = "k-scale-cpu"; k.kernel.contributes_to_correctness = true;
  k.memory_bytes = bytes; k.shape.dims = {static_cast<std::int64_t>(bytes / 4)};
  k.datatype = gc::Datatype::F32; k.layout = gc::TensorLayout::Contiguous;
  k.scalar.specialized = true; k.scalar.float_value = 2.0f;
  k.binding.binding_class = gc::BindingClass::ReplayMutableBinding; k.binding.rebinding_eligible = true;
  k.reactive_input_indices = {0}; k.reactive_output_indices = {0};
  gc::GraphEdgeDescriptor e; e.edge_id = gc::GraphEdgeId(1,1); e.from = copy.node_id; e.to = k.node_id; e.kind = gc::DependencyKind::Execution;
  desc.nodes = {copy, k}; desc.edges = {e}; desc.dependencies = {k.kernel};
  return desc;
}

} // namespace

GC_TEST(atomic_multiprocess_stale_authority) {
  const std::uint16_t port = static_cast<std::uint16_t>(41000 + (::_getpid() % 2000));
  const std::string dir = dir_of_exe();
  const std::string coord = dir + "\\..\\tools\\gc-coordinator.exe";
  const std::string worker = dir + "\\..\\tools\\gc-worker.exe";

  Proc c = spawn(coord, std::to_string(port));
  REQUIRE(c.process != nullptr);
  ::Sleep(700);
  Proc w1 = spawn(worker, std::to_string(port) + " 1");
  Proc w2 = spawn(worker, std::to_string(port) + " 2");
  REQUIRE(w1.process != nullptr);
  REQUIRE(w2.process != nullptr);
  ::Sleep(700);

  auto conn = gc::tcp_connect("127.0.0.1", port);
  REQUIRE(conn.ok());
  gc::TcpSocket client = conn.value();
  const std::int64_t s = client.handle;
  std::printf("dbg: connected, sending Hello\n");
  gc::ProtocolMessage hello; hello.type = gc::ProtocolMessageType::Hello; hello.request_id = 1;
  send_quiet(s, hello);
  gc::ProtocolMessage wel; REQUIRE(recv_msg(s, wel));
  CHECK(wel.type == gc::ProtocolMessageType::Welcome);
  CHECK(wel.status == gc::ProtocolStatus::Ok);

  const std::uint64_t bytes = 64;
  auto descA = make_cpu_desc(bytes);
  gc::WorkloadIdentity wlA; wlA.logical_name = "A"; wlA.logical_version = 1;

  // Lookup A -> capture -> hit.
  gc::ProtocolMessage look1; look1.type = gc::ProtocolMessageType::Lookup; look1.request_id = 11;
  look1.epoch = gc::CoordinatorEpoch(1); look1.cache_gen = gc::CacheGeneration(1);
  look1.payload = encode_lookup(descA, wlA);
  send_quiet(s, look1);
  gc::ProtocolMessage r1; REQUIRE(recv_msg(s, r1));
  CHECK(r1.type == gc::ProtocolMessageType::LookupResult);
  CHECK(r1.status == gc::ProtocolStatus::Ok);
  std::string aidA; REQUIRE(decode_artifact(r1, aidA));
  CHECK(!aidA.empty());
  gc::GraphGeneration genA = r1.graph_gen;

  // Second same-key hit.
  gc::ProtocolMessage look2; look2.type = gc::ProtocolMessageType::Lookup; look2.request_id = 12;
  look2.epoch = gc::CoordinatorEpoch(1); look2.cache_gen = gc::CacheGeneration(1);
  look2.payload = encode_lookup(descA, wlA);
  send_quiet(s, look2);
  gc::ProtocolMessage r2; REQUIRE(recv_msg(s, r2));
  CHECK(r2.status == gc::ProtocolStatus::Ok);
  CHECK(r2.graph_gen == genA);

  // Replay through the worker path.
  std::vector<float> in(16, 1.0f);
  gc::ProtocolMessage rep; rep.type = gc::ProtocolMessageType::ReplayInstruct; rep.request_id = 13;
  rep.epoch = gc::CoordinatorEpoch(1); rep.cache_gen = gc::CacheGeneration(1); rep.graph_gen = genA;
  rep.replay_attempt = gc::ReplayAttemptId(21); rep.payload = encode_replay(aidA, in);
  send_quiet(s, rep);
  gc::ProtocolMessage rep_res; REQUIRE(recv_msg(s, rep_res));
  CHECK(rep_res.status == gc::ProtocolStatus::Ok);
  std::vector<float> out; REQUIRE(decode_replay_output(rep_res, out));
  CHECK(out.size() == in.size());
  bool ok_out = true; for (std::size_t i = 0; i < out.size(); ++i) if (std::fabs(out[i] - 2.0f) > 1e-4f) ok_out = false;
  CHECK(ok_out);

  // Kill worker 1 (real process termination).
  terminate(w1);

  // Restart worker 1 as a NEW process (new boot id).
  Proc w1b = spawn(worker, std::to_string(port) + " 1");
  REQUIRE(w1b.process != nullptr);
  ::Sleep(900);

  // Roll epoch + cache generation via invalidation with flags bit0.
  gc::ProtocolMessage inv; inv.type = gc::ProtocolMessageType::Invalidate; inv.request_id = 14;
  inv.epoch = gc::CoordinatorEpoch(1); inv.cache_gen = gc::CacheGeneration(1); inv.flags = 0x01u;  // roll epoch
  send_quiet(s, inv);
  gc::ProtocolMessage ack; REQUIRE(recv_msg(s, ack));
  CHECK(ack.type == gc::ProtocolMessageType::InvalidateAck);
  CHECK(ack.status == gc::ProtocolStatus::Ok);

  // Stale epoch rejection.
  gc::ProtocolMessage repE; repE.type = gc::ProtocolMessageType::Lookup; repE.request_id = 20;
  repE.epoch = gc::CoordinatorEpoch(1); repE.cache_gen = gc::CacheGeneration(1); repE.payload = encode_lookup(descA, wlA);
  send_quiet(s, repE);
  gc::ProtocolMessage rrE; REQUIRE(recv_msg(s, rrE)); CHECK(rrE.status == gc::ProtocolStatus::StaleEpoch);

  // Stale worker boot (current epoch, old boot).
  gc::ProtocolMessage repB; repB.type = gc::ProtocolMessageType::ReplayInstruct; repB.request_id = 21;
  repB.epoch = gc::CoordinatorEpoch(2); repB.cache_gen = gc::CacheGeneration(2);
  repB.worker_id = gc::WorkerId(1); repB.worker_boot = gc::WorkerBootId(0xDEADBEEF);
  repB.graph_gen = genA; repB.replay_attempt = gc::ReplayAttemptId(22); repB.payload = encode_replay(aidA, in);
  send_quiet(s, repB);
  gc::ProtocolMessage rrB; REQUIRE(recv_msg(s, rrB)); CHECK(rrB.status == gc::ProtocolStatus::StaleWorkerBoot);

  // Stale cache generation.
  gc::ProtocolMessage repC; repC.type = gc::ProtocolMessageType::Lookup; repC.request_id = 22;
  repC.epoch = gc::CoordinatorEpoch(2); repC.cache_gen = gc::CacheGeneration(1); repC.payload = encode_lookup(descA, wlA);
  send_quiet(s, repC);
  gc::ProtocolMessage rrC; REQUIRE(recv_msg(s, rrC)); CHECK(rrC.status == gc::ProtocolStatus::StaleCacheGeneration);

  // Stale graph generation.
  gc::ProtocolMessage repG; repG.type = gc::ProtocolMessageType::ReplayInstruct; repG.request_id = 23;
  repG.epoch = gc::CoordinatorEpoch(2); repG.cache_gen = gc::CacheGeneration(2);
  repG.graph_gen = gc::GraphGeneration(genA.value + 999);
  repG.replay_attempt = gc::ReplayAttemptId(23); repG.payload = encode_replay(aidA, in);
  send_quiet(s, repG);
  gc::ProtocolMessage rrG; REQUIRE(recv_msg(s, rrG)); CHECK(rrG.status == gc::ProtocolStatus::StaleGraphGeneration);

  // Fresh lookup under current epoch succeeds (hard-assert).
  gc::ProtocolMessage lookF; lookF.type = gc::ProtocolMessageType::Lookup; lookF.request_id = 30;
  lookF.epoch = gc::CoordinatorEpoch(2); lookF.cache_gen = gc::CacheGeneration(2); lookF.payload = encode_lookup(descA, wlA);
  send_quiet(s, lookF);
  gc::ProtocolMessage rf; REQUIRE(recv_msg(s, rf));
  CHECK(rf.type == gc::ProtocolMessageType::LookupResult);
  CHECK(rf.status == gc::ProtocolStatus::Ok);
  gc::GraphGeneration newGen = rf.graph_gen;

  // Second exact hit under current authority.
  gc::ProtocolMessage lookF2; lookF2.type = gc::ProtocolMessageType::Lookup; lookF2.request_id = 31;
  lookF2.epoch = gc::CoordinatorEpoch(2); lookF2.cache_gen = gc::CacheGeneration(2); lookF2.payload = encode_lookup(descA, wlA);
  send_quiet(s, lookF2);
  gc::ProtocolMessage rf2; REQUIRE(recv_msg(s, rf2));
  CHECK(rf2.status == gc::ProtocolStatus::Ok);
  CHECK(rf2.graph_gen == newGen);

  // Replay under current authority.
  gc::ProtocolMessage repF; repF.type = gc::ProtocolMessageType::ReplayInstruct; repF.request_id = 32;
  repF.epoch = gc::CoordinatorEpoch(2); repF.cache_gen = gc::CacheGeneration(2); repF.graph_gen = newGen;
  repF.replay_attempt = gc::ReplayAttemptId(33); repF.payload = encode_replay(aidA, in);
  send_quiet(s, repF);
  gc::ProtocolMessage rrep; REQUIRE(recv_msg(s, rrep));
  CHECK(rrep.type == gc::ProtocolMessageType::ReplayResult);
  CHECK(rrep.status == gc::ProtocolStatus::Ok);

  // Fresh publish advanced the generation past the stale one.
  CHECK(newGen.value > genA.value);

  std::printf("dbg: done scenario\n");
  terminate(w1b); terminate(w2); terminate(c);
  [[maybe_unused]] auto _tc = gc::tcp_close(client);
}

GC_TEST_MAIN
