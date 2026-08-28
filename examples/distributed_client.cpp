// Example: distributed coordinator/worker graph lookup and replay over framed TCP.
#include "graphcache/canonical.hpp"
#include "graphcache/protocol.hpp"
#include "graphcache/serialization.hpp"
#include "graphcache/topology.hpp"
#include <windows.h>
#include <process.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
namespace {
void send_quiet(std::int64_t s, const gc::ProtocolMessage& m) { auto r = gc::send_frame(s, m); (void)r; }
void close_quiet(const gc::TcpSocket& c) { auto r = gc::tcp_close(c); (void)r; }
struct Proc { HANDLE process{nullptr}; };
Proc spawn(const std::string& exe, const std::string& args) {
  Proc p; std::string cmd = "\"" + exe + "\" " + args;
  STARTUPINFOA si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  std::vector<char> cl(cmd.begin(), cmd.end()); cl.push_back('\0');
  if (!CreateProcessA(nullptr, cl.data(), nullptr, nullptr, FALSE, CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi)) return p;
  p.process = pi.hProcess; CloseHandle(pi.hThread); return p;
}
void terminate(Proc& p) { if (p.process) { TerminateProcess(p.process, 1); WaitForSingleObject(p.process, 5000); CloseHandle(p.process); p.process = nullptr; } }
std::string dir_of_exe() { char b[MAX_PATH]; DWORD n = GetModuleFileNameA(nullptr, b, MAX_PATH); std::string s(b, n); auto p = s.find_last_of("\\/"); return p == std::string::npos ? "" : s.substr(0, p); }
bool recv(std::int64_t s, gc::ProtocolMessage& out) { auto r = gc::recv_frame(s); if (!r.ok()) return false; out = std::move(r.value()); return true; }
gc::GraphDescriptor make_desc(std::uint64_t bytes) {
  gc::GraphDescriptor d;
  d.backend.kind = gc::BackendKind::Cpu; d.backend.backend_name = "cpu"; d.backend.backend_version = 1;
  d.runtime.runtime_version = 1; d.runtime.graph_abi = 1; d.runtime.kernel_abi = 1;
  d.device.vendor = gc::AcceleratorVendor::Cpu; d.device.architecture = "cpu";
  gc::GraphNodeDescriptor c; c.node_id = gc::GraphNodeId(1,1); c.category = gc::NodeCategory::MemoryCopy;
  c.name = "copy"; c.memory_bytes = bytes; c.reactive_input_indices = {0}; c.reactive_output_indices = {0}; c.binding.rebinding_eligible = true;
  gc::GraphNodeDescriptor k; k.node_id = gc::GraphNodeId(2,2); k.category = gc::NodeCategory::Kernel; k.name = "scale";
  k.kernel.name = "scale"; k.kernel.contributes_to_correctness = true; k.memory_bytes = bytes;
  k.shape.dims = {static_cast<std::int64_t>(bytes/4)}; k.datatype = gc::Datatype::F32; k.scalar.specialized = true; k.scalar.float_value = 2.0f;
  k.reactive_input_indices = {0}; k.reactive_output_indices = {0}; k.binding.rebinding_eligible = true;
  gc::GraphEdgeDescriptor e; e.edge_id = gc::GraphEdgeId(1,1); e.from = c.node_id; e.to = k.node_id; e.kind = gc::DependencyKind::Execution;
  d.nodes = {c, k}; d.edges = {e}; d.dependencies = {k.kernel}; return d;
}
std::vector<std::uint8_t> encode_lookup(const gc::GraphDescriptor& desc, const gc::WorkloadIdentity& wl) {
  std::vector<std::uint8_t> dbytes; [[maybe_unused]] auto _sd = gc::serialize_descriptor(desc, dbytes);
  gc::CanonicalWriter w; w.put_bytes(1, dbytes); w.put_str(2, wl.logical_name); w.put_u32(3, wl.logical_version);
  w.put_str(4, wl.namespace_name); w.put_str(5, wl.model_operator_revision); return w.take();
}
std::vector<std::uint8_t> encode_replay(const std::string& hex, const std::vector<float>& in) {
  gc::CanonicalWriter w; w.put_str(1, hex); w.put_u32(2, static_cast<std::uint32_t>(in.size()));
  for (float v : in) w.put_f64(3, static_cast<double>(v)); return w.take();
}
bool decode_artifact(const gc::ProtocolMessage& m, std::string& hex) {
  gc::CanonicalReader r(m.payload); std::uint16_t t; std::span<const std::uint8_t> p;
  if (!r.next(t, p) || t != 1) return false; return gc::CanonicalReader::decode_str(p, hex);
}
bool decode_output(const gc::ProtocolMessage& m, std::vector<float>& out) {
  gc::CanonicalReader r(m.payload); std::uint16_t t; std::span<const std::uint8_t> p;
  if (!r.next(t, p) || t != 1) return false; std::uint32_t n;
  if (!gc::CanonicalReader::decode_u32(p, n) || n > (1u<<20)) return false; out.resize(n);
  for (std::uint32_t i = 0; i < n; ++i) { if (!r.next(t, p) || t != 2) return false; std::uint64_t bits;
    if (!gc::CanonicalReader::decode_u64(p, bits)) return false; double d; std::memcpy(&d, &bits, sizeof(d)); out[i] = static_cast<float>(d); }
  return true;
}
} // namespace
int main() {
  const std::uint16_t port = static_cast<std::uint16_t>(41000 + (::_getpid() % 2000));
  const std::string dir = dir_of_exe();
  Proc coord = spawn(dir + "\\..\\tools\\gc-coordinator.exe", std::to_string(port));
  if (!coord.process) { std::printf("distributed example: cannot start coordinator\n"); return 2; }
  Proc w1 = spawn(dir + "\\..\\tools\\gc-worker.exe", std::to_string(port) + " 1");
  Proc w2 = spawn(dir + "\\..\\tools\\gc-worker.exe", std::to_string(port) + " 2");
  if (!w1.process || !w2.process) { std::printf("distributed example: cannot start workers\n"); return 2; }
  std::this_thread::sleep_for(std::chrono::milliseconds(700));
  auto conn = gc::tcp_connect("127.0.0.1", port);
  if (!conn.ok()) { terminate(w1); terminate(w2); terminate(coord); return 2; }
  gc::TcpSocket client = conn.value();
  const std::int64_t s = client.handle;
  gc::ProtocolMessage hello; hello.type = gc::ProtocolMessageType::Hello; hello.request_id = 1;
  send_quiet(s, hello);
  gc::ProtocolMessage wel; if (!recv(s, wel) || wel.status != gc::ProtocolStatus::Ok) { close_quiet(client); terminate(w1); terminate(w2); terminate(coord); return 1; }
  auto desc = make_desc(64);
  gc::WorkloadIdentity wl; wl.logical_name = "distributed"; wl.logical_version = 1;
  gc::ProtocolMessage look; look.type = gc::ProtocolMessageType::Lookup; look.request_id = 11;
  look.epoch = gc::CoordinatorEpoch(1); look.cache_gen = gc::CacheGeneration(1); look.payload = encode_lookup(desc, wl);
  send_quiet(s, look);
  gc::ProtocolMessage res; if (!recv(s, res) || res.type != gc::ProtocolMessageType::LookupResult || res.status != gc::ProtocolStatus::Ok) { close_quiet(client); terminate(w1); terminate(w2); terminate(coord); return 1; }
  std::string hex; if (!decode_artifact(res, hex)) { close_quiet(client); terminate(w1); terminate(w2); terminate(coord); return 1; }
  std::vector<float> in(16, 1.0f);
  gc::ProtocolMessage rep; rep.type = gc::ProtocolMessageType::ReplayInstruct; rep.request_id = 13;
  rep.epoch = gc::CoordinatorEpoch(1); rep.cache_gen = gc::CacheGeneration(1); rep.graph_gen = res.graph_gen;
  rep.replay_attempt = gc::ReplayAttemptId(21); rep.payload = encode_replay(hex, in);
  send_quiet(s, rep);
  gc::ProtocolMessage repres; bool ok = recv(s, repres) && repres.type == gc::ProtocolMessageType::ReplayResult && repres.status == gc::ProtocolStatus::Ok;
  std::vector<float> out; bool out_ok = false;
  if (ok && decode_output(repres, out)) { out_ok = out.size() == in.size(); for (std::size_t i = 0; i < out.size() && out_ok; ++i) if (std::fabs(out[i] - 2.0f) > 1e-4f) out_ok = false; }
  std::printf("distributed example: hit=1 artifact=%s replay_ok=%d\n", hex.c_str(), ok && out_ok ? 1 : 0);
  close_quiet(client);
  terminate(w1); terminate(w2); terminate(coord);
  return (ok && out_ok) ? 0 : 1;
}