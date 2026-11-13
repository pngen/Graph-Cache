// Coordinator regression scenarios over real processes and framed TCP. The test
// acts as clients and, where an exact interleaving is needed, as the workers
// themselves. Pass a test name to run only it.
// Copyright 2026 Summon Software Labs. Apache License 2.0.

#include "test_util.hpp"

#include "graphcache/canonical.hpp"
#include "graphcache/protocol.hpp"
#include "graphcache/serialization.hpp"

#include <windows.h>
#include <process.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace {

std::string dir_of_exe() {
  char buf[MAX_PATH];
  DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
  std::string s(buf, n);
  return s.substr(0, s.find_last_of("\\/"));
}

HANDLE spawn(const std::string& exe, const std::string& args) {
  std::string cmd = "\"" + exe + "\" " + args;
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  std::vector<char> cmdline(cmd.begin(), cmd.end());
  cmdline.push_back('\0');
  if (!CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, FALSE, CREATE_NEW_PROCESS_GROUP, nullptr,
                      nullptr, &si, &pi))
    return nullptr;
  CloseHandle(pi.hThread);
  return pi.hProcess;
}

bool exited(HANDLE h) { return WaitForSingleObject(h, 0) == WAIT_OBJECT_0; }

void kill_wait(HANDLE& h) {
  if (!h) return;
  TerminateProcess(h, 1);
  WaitForSingleObject(h, INFINITE);
  CloseHandle(h);
  h = nullptr;
}

// A coordinator process; terminated on scope exit together with any workers.
struct Cluster {
  HANDLE coord{nullptr};
  std::vector<HANDLE> workers;
  std::uint16_t port{0};
  ~Cluster() {
    for (auto& w : workers) kill_wait(w);
    kill_wait(coord);
  }
};

void start_coordinator(Cluster& cl) {
  static unsigned next = static_cast<unsigned>(::_getpid()) % 997u;
  const std::string exe = dir_of_exe() + "\\..\\tools\\gc-coordinator.exe";
  for (;;) {
    cl.port = static_cast<std::uint16_t>(44000 + (next++ % 4000u));
    cl.coord = spawn(exe, std::to_string(cl.port));
    if (!cl.coord) return;
    for (;;) {
      if (exited(cl.coord)) break;   // listen failed: retry on another port
      auto probe = gc::tcp_connect("127.0.0.1", cl.port);
      if (probe.ok()) { (void)gc::tcp_close(probe.value()); return; }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CloseHandle(cl.coord);
    cl.coord = nullptr;
  }
}

std::int64_t connect_to(const Cluster& cl) {
  auto c = gc::tcp_connect("127.0.0.1", cl.port);
  return c.ok() ? c.value().handle : -1;
}

bool send_msg(std::int64_t s, const gc::ProtocolMessage& m) { return gc::send_frame(s, m).ok(); }
bool recv_msg(std::int64_t s, gc::ProtocolMessage& out) {
  auto r = gc::recv_frame(s);
  if (!r.ok()) return false;
  out = std::move(r.value());
  return true;
}

// Read frames until a Welcome answers the Hello just sent; the first frame read
// is returned through `first`.
bool hello_roundtrip(std::int64_t s, std::uint64_t request_id, gc::ProtocolMessage* first = nullptr) {
  gc::ProtocolMessage h;
  h.type = gc::ProtocolMessageType::Hello;
  h.request_id = request_id;
  if (!send_msg(s, h)) return false;
  bool got_first = false;
  for (;;) {
    gc::ProtocolMessage m;
    if (!recv_msg(s, m)) return false;
    if (first && !got_first) { *first = m; got_first = true; }
    if (m.type == gc::ProtocolMessageType::Welcome) return true;
  }
}

// Register the test itself as a worker.
std::int64_t fake_worker(const Cluster& cl, std::uint64_t wid, std::uint64_t boot) {
  std::int64_t s = connect_to(cl);
  if (s < 0) return -1;
  gc::ProtocolMessage reg;
  reg.type = gc::ProtocolMessageType::Register;
  reg.worker_id = gc::WorkerId(wid);
  reg.worker_boot = gc::WorkerBootId(boot);
  gc::ProtocolMessage ack;
  if (!send_msg(s, reg) || !recv_msg(s, ack) || ack.type != gc::ProtocolMessageType::RegisterAck ||
      ack.status != gc::ProtocolStatus::Ok)
    return -1;
  return s;
}

gc::GraphDescriptor cpu_desc() {
  auto d = test_util::make_cpu_graph("dist", 64, true);
  return d;
}

gc::ProtocolMessage lookup_msg(std::uint64_t request_id, const std::string& workload, std::uint64_t epoch,
                               std::uint64_t cache_gen) {
  gc::ProtocolMessage m;
  m.type = gc::ProtocolMessageType::Lookup;
  m.request_id = request_id;
  m.epoch = gc::CoordinatorEpoch(epoch);
  m.cache_gen = gc::CacheGeneration(cache_gen);
  std::vector<std::uint8_t> dbytes;
  (void)gc::serialize_descriptor(cpu_desc(), dbytes);
  gc::CanonicalWriter w;
  w.put_bytes(1, dbytes);
  w.put_str(2, workload);
  w.put_u32(3, 1);
  w.put_str(4, "");
  w.put_str(5, "");
  m.payload = w.take();
  return m;
}

gc::ProtocolMessage capture_result(const gc::ProtocolMessage& inst, std::uint64_t wid, std::uint64_t boot,
                                   const std::string& artifact_hex) {
  gc::ProtocolMessage r;
  r.type = gc::ProtocolMessageType::CaptureResult;
  r.request_id = inst.request_id;
  r.epoch = inst.epoch;
  r.cache_gen = inst.cache_gen;
  r.worker_id = gc::WorkerId(wid);
  r.worker_boot = gc::WorkerBootId(boot);
  r.capture_attempt = inst.capture_attempt;
  r.status = gc::ProtocolStatus::Ok;
  gc::CanonicalWriter w;
  w.put_str(1, artifact_hex);
  w.put_u32(2, 2);
  r.payload = w.take();
  return r;
}

gc::ProtocolMessage replay_msg(std::uint64_t request_id, const std::string& artifact_hex, float value) {
  gc::ProtocolMessage m;
  m.type = gc::ProtocolMessageType::ReplayInstruct;
  m.request_id = request_id;
  m.epoch = gc::CoordinatorEpoch(1);
  m.cache_gen = gc::CacheGeneration(1);
  m.replay_attempt = gc::ReplayAttemptId(request_id);
  gc::CanonicalWriter w;
  w.put_str(1, artifact_hex);
  w.put_u32(2, 1);
  w.put_f64(3, static_cast<double>(value));
  m.payload = w.take();
  return m;
}

std::string artifact_of(const gc::ProtocolMessage& m) {
  gc::CanonicalReader r(m.payload);
  std::uint16_t tag;
  std::span<const std::uint8_t> p;
  std::string hex;
  if (!r.next(tag, p) || tag != 1 || !gc::CanonicalReader::decode_str(p, hex)) return {};
  return hex;
}

const std::string kArtA = "00000000000000aa:00000000000000aa";
const std::string kArtB = "00000000000000bb:00000000000000bb";

}  // namespace

// Only the worker a capture was dispatched to can complete it.
GC_TEST(capture_completed_only_by_dispatched_worker) {
  Cluster cl;
  start_coordinator(cl);
  REQUIRE(cl.coord != nullptr);
  const std::int64_t a = fake_worker(cl, 1, 101);
  const std::int64_t b = fake_worker(cl, 2, 202);
  const std::int64_t c = connect_to(cl);
  REQUIRE(a >= 0 && b >= 0 && c >= 0);
  REQUIRE(hello_roundtrip(c, 1));
  REQUIRE(send_msg(c, lookup_msg(100, "A", 1, 1)));
  gc::ProtocolMessage inst;
  REQUIRE(recv_msg(a, inst));
  REQUIRE(inst.type == gc::ProtocolMessageType::CaptureInstruct);
  // Worker 2 forges the completion of worker 1's attempt.
  REQUIRE(send_msg(b, capture_result(inst, 2, 202, kArtB)));
  gc::ProtocolMessage first;
  REQUIRE(hello_roundtrip(b, 9, &first));
  CHECK(first.type == gc::ProtocolMessageType::LookupResult);
  CHECK(first.status == gc::ProtocolStatus::StaleCaptureAttempt);
  // Worker 1 completes its own attempt; the requester sees worker 1's artifact.
  REQUIRE(send_msg(a, capture_result(inst, 1, 101, kArtA)));
  gc::ProtocolMessage res;
  REQUIRE(recv_msg(c, res));
  CHECK(res.request_id == 100);
  CHECK(res.status == gc::ProtocolStatus::Ok);
  CHECK(artifact_of(res) == kArtA);
}

// A capture completion rejected after an invalidation still answers the lookup
// that is waiting on it.
GC_TEST(stale_completion_answers_requester) {
  Cluster cl;
  start_coordinator(cl);
  REQUIRE(cl.coord != nullptr);
  const std::int64_t a = fake_worker(cl, 1, 101);
  const std::int64_t c = connect_to(cl);
  REQUIRE(a >= 0 && c >= 0);
  REQUIRE(hello_roundtrip(c, 1));
  REQUIRE(send_msg(c, lookup_msg(100, "A", 1, 1)));
  gc::ProtocolMessage inst;
  REQUIRE(recv_msg(a, inst));
  gc::ProtocolMessage inv;
  inv.type = gc::ProtocolMessageType::Invalidate;
  inv.request_id = 101;
  inv.epoch = gc::CoordinatorEpoch(1);
  inv.cache_gen = gc::CacheGeneration(1);
  REQUIRE(send_msg(c, inv));
  gc::ProtocolMessage ack;
  REQUIRE(recv_msg(c, ack));
  REQUIRE(ack.type == gc::ProtocolMessageType::InvalidateAck);
  // The completion carries the pre-invalidation cache generation.
  REQUIRE(send_msg(a, capture_result(inst, 1, 101, kArtA)));
  REQUIRE(hello_roundtrip(a, 9));
  gc::ProtocolMessage first;
  REQUIRE(hello_roundtrip(c, 2, &first));
  CHECK(first.type == gc::ProtocolMessageType::LookupResult);
  CHECK(first.request_id == 100);
  CHECK(first.status == gc::ProtocolStatus::CompletionAfterInvalidation);
}

// When a worker's connection closes, requests waiting on it are answered and
// later replays of its graphs are refused instead of being sent to its socket.
GC_TEST(dead_worker_answers_requesters) {
  Cluster cl;
  start_coordinator(cl);
  REQUIRE(cl.coord != nullptr);
  const std::int64_t a = fake_worker(cl, 1, 101);
  const std::int64_t c = connect_to(cl);
  REQUIRE(a >= 0 && c >= 0);
  REQUIRE(hello_roundtrip(c, 1));
  REQUIRE(send_msg(c, lookup_msg(100, "A", 1, 1)));
  gc::ProtocolMessage inst;
  REQUIRE(recv_msg(a, inst));
  REQUIRE(send_msg(a, capture_result(inst, 1, 101, kArtA)));
  gc::ProtocolMessage done;
  REQUIRE(recv_msg(c, done));
  REQUIRE(done.status == gc::ProtocolStatus::Ok);
  // A second capture is in flight on worker 1 when it goes away.
  REQUIRE(send_msg(c, lookup_msg(101, "B", 1, 1)));
  gc::ProtocolMessage inst2;
  REQUIRE(recv_msg(a, inst2));
  (void)gc::tcp_close(gc::TcpSocket{a});
  gc::ProtocolMessage gone;
  REQUIRE(recv_msg(c, gone));
  CHECK(gone.request_id == 101);
  CHECK(gone.status == gc::ProtocolStatus::WorkerUnavailable);
  REQUIRE(send_msg(c, replay_msg(102, kArtA, 1.0f)));
  gc::ProtocolMessage rep;
  REQUIRE(recv_msg(c, rep));
  CHECK(rep.request_id == 102);
  CHECK(rep.status == gc::ProtocolStatus::WorkerUnavailable);
}

// A worker started after an epoch roll can register.
GC_TEST(worker_registers_after_epoch_roll) {
  Cluster cl;
  start_coordinator(cl);
  REQUIRE(cl.coord != nullptr);
  const std::int64_t c = connect_to(cl);
  REQUIRE(c >= 0);
  REQUIRE(hello_roundtrip(c, 1));
  gc::ProtocolMessage inv;
  inv.type = gc::ProtocolMessageType::Invalidate;
  inv.request_id = 2;
  inv.epoch = gc::CoordinatorEpoch(1);
  inv.cache_gen = gc::CacheGeneration(1);
  inv.flags = 0x01u;   // roll the epoch
  REQUIRE(send_msg(c, inv));
  gc::ProtocolMessage ack;
  REQUIRE(recv_msg(c, ack));
  REQUIRE(ack.type == gc::ProtocolMessageType::InvalidateAck);
  HANDLE w = spawn(dir_of_exe() + "\\..\\tools\\gc-worker.exe", std::to_string(cl.port) + " 1");
  REQUIRE(w != nullptr);
  cl.workers.push_back(w);
  bool served = false;
  bool worker_exited = false;
  for (std::uint64_t rid = 10; !served && !worker_exited; ++rid) {
    REQUIRE(send_msg(c, lookup_msg(rid, "late", ack.epoch.value, ack.cache_gen.value)));
    gc::ProtocolMessage r;
    REQUIRE(recv_msg(c, r));
    if (r.status == gc::ProtocolStatus::Ok) {
      served = true;
    } else {
      REQUIRE(r.status == gc::ProtocolStatus::WorkerUnavailable);
      worker_exited = exited(w);
      if (!worker_exited) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  CHECK(served);
  CHECK(!worker_exited);
}

// Replay results are routed to the requester even when two clients use the
// same request id.
GC_TEST(replay_results_reach_their_requester) {
  Cluster cl;
  start_coordinator(cl);
  REQUIRE(cl.coord != nullptr);
  const std::int64_t a = fake_worker(cl, 1, 101);
  const std::int64_t c1 = connect_to(cl);
  const std::int64_t c2 = connect_to(cl);
  REQUIRE(a >= 0 && c1 >= 0 && c2 >= 0);
  REQUIRE(hello_roundtrip(c1, 1));
  REQUIRE(hello_roundtrip(c2, 1));
  REQUIRE(send_msg(c1, lookup_msg(100, "A", 1, 1)));
  gc::ProtocolMessage inst;
  REQUIRE(recv_msg(a, inst));
  REQUIRE(send_msg(a, capture_result(inst, 1, 101, kArtA)));
  gc::ProtocolMessage done;
  REQUIRE(recv_msg(c1, done));
  REQUIRE(done.status == gc::ProtocolStatus::Ok);
  const auto rep1 = replay_msg(7, kArtA, 1.0f);
  const auto rep2 = replay_msg(7, kArtA, 2.0f);
  REQUIRE(send_msg(c1, rep1));
  gc::ProtocolMessage ri1;
  REQUIRE(recv_msg(a, ri1));
  REQUIRE(send_msg(c2, rep2));
  gc::ProtocolMessage ri2;
  REQUIRE(recv_msg(a, ri2));
  // The worker answers each instruction with its own payload.
  for (const auto* ri : {&ri1, &ri2}) {
    gc::ProtocolMessage out;
    out.type = gc::ProtocolMessageType::ReplayResult;
    out.request_id = ri->request_id;
    out.epoch = ri->epoch;
    out.cache_gen = ri->cache_gen;
    out.worker_id = gc::WorkerId(1);
    out.worker_boot = gc::WorkerBootId(101);
    out.replay_attempt = ri->replay_attempt;
    out.status = gc::ProtocolStatus::Ok;
    out.payload = ri->payload;
    REQUIRE(send_msg(a, out));
  }
  gc::ProtocolMessage got2;
  REQUIRE(recv_msg(c2, got2));
  CHECK(got2.request_id == 7);
  REQUIRE(got2.payload == rep2.payload);   // a misrouted result means c1 never gets one
  gc::ProtocolMessage got1;
  REQUIRE(recv_msg(c1, got1));
  CHECK(got1.request_id == 7);
  CHECK(got1.payload == rep1.payload);
}

int main(int argc, char** argv) {
  int ran = 0;
  for (auto& t : ::gc_test::registry()) {
    if (argc > 1 && t.name != argv[1]) continue;
    ::gc_test::current() = t.name;
    t.fn();
    ++ran;
  }
  std::printf("gc_test: %d tests, %d checks, %d failures\n", ran, ::gc_test::total_assertions(),
              ::gc_test::failures());
  if (ran == 0) return 2;
  return ::gc_test::failures() == 0 ? 0 : 1;
}
