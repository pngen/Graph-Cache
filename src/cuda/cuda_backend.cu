// Real CUDA Graph backend. Graphs are constructed and instantiated through
// genuine CUDA Graph APIs (cudaGraphCreate/Add, cudaGraphInstantiate,
// cudaGraphExecKernelNodeSetParams / cudaGraphExecMemcpyNodeSetParams1D,
// cudaGraphLaunch, cudaGraphExecDestroy). The graph captures real H2D transfer,
// one or more kernels, and a D2H transfer with explicit dependencies.
// Copyright 2026 Summon Software Labs. Apache License 2.0.

#include "graphcache/backend.hpp"
#include "graphcache/topology.hpp"

#include <cuda_runtime.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace gc {
namespace {

int op_code(const std::string& name) {
  if (name == "scale") return 1;
  if (name == "addscalar") return 2;
  if (name == "linear") return 3;
  if (name == "square") return 4;
  if (name == "relu") return 5;
  return 0;
}

__global__ void gc_generic_kernel(const float* in, float* out, int n, float a, float b, int op) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) {
    float x = in[i];
    float y = 0.0f;
    switch (op) {
      case 0: y = x; break;
      case 1: y = x * a; break;
      case 2: y = x + a; break;
      case 3: y = x * b + a; break;
      case 4: y = x * x; break;
      case 5: y = x > 0.0f ? x : 0.0f; break;
    }
    out[i] = y;
  }
}

cudaMemcpy3DParms make_1d(void* dst, const void* src, std::size_t bytes, cudaMemcpyKind kind) {
  cudaMemcpy3DParms p{};
  p.srcPtr = make_cudaPitchedPtr(const_cast<void*>(src), bytes, bytes, 1);
  p.dstPtr = make_cudaPitchedPtr(dst, bytes, bytes, 1);
  p.extent = make_cudaExtent(bytes, 1, 1);
  p.kind = kind;
  return p;
}

struct CudaGraphState {
  cudaGraphExec_t exec{nullptr};
  cudaGraph_t graph{nullptr};
  cudaStream_t stream{nullptr};
  std::vector<void*> buffers;              // num_kernels+1 device buffers
  cudaGraphNode_t h2d_node{nullptr};
  cudaGraphNode_t d2h_node{nullptr};
  std::vector<cudaGraphNode_t> kernel_nodes;
  std::vector<int> ops;
  std::vector<float> as;
  std::vector<float> bs;
  std::size_t bytes{0};
  int n{0};
  std::vector<float> h_in, h_out;
  std::mutex mtx;
};

void fill_pattern(float* p, std::size_t count, std::uint32_t seed) {
  std::uint32_t s = seed ? seed : 1u;
  for (std::size_t i = 0; i < count; ++i) {
    s ^= s << 13; s ^= s >> 7; s ^= s << 17;
    p[i] = static_cast<float>(static_cast<std::int32_t>(s)) * 0.0001f;
  }
}

} // namespace

class CudaBackend final : public GraphBackend {
 public:
  CudaBackend() = default;
  ~CudaBackend() override = default;

  BackendIdentity identity() const override {
    BackendIdentity id;
    id.kind = BackendKind::Cuda;
    id.backend_name = "cuda-graph";
    id.backend_version = 1;
    id.implementation_name = "graphcache-cuda-graph";
    return id;
  }

  Result<std::shared_ptr<void>> capture(const GraphDescriptor& desc, CaptureAttemptId) override {
    std::vector<const GraphNodeDescriptor*> kernels;
    std::size_t bytes = 0;
    for (const auto& n : desc.nodes) {
      if (n.category == NodeCategory::Kernel) {
        kernels.push_back(&n);
        if (n.memory_bytes > bytes) bytes = n.memory_bytes;
      }
    }
    if (kernels.empty()) {
      return Result<std::shared_ptr<void>>::failure(
          Error(ErrorCode::InvalidArgument, "CUDA graph requires at least one kernel node"));
    }
    if (bytes == 0) bytes = 64;
    const std::size_t n = bytes / sizeof(float);
    if (n == 0) {
      return Result<std::shared_ptr<void>>::failure(
          Error(ErrorCode::InvalidArgument, "CUDA graph workload has zero elements"));
    }

    auto st = std::make_shared<CudaGraphState>();
    st->bytes = bytes;
    st->n = static_cast<int>(n);
    st->h_in.resize(n);
    st->h_out.resize(n);
    fill_pattern(st->h_in.data(), n, 0xC0FFEEu);

    const std::size_t nk = kernels.size();
    st->ops.reserve(nk);
    st->as.reserve(nk);
    st->bs.reserve(nk);
    for (const auto* k : kernels) {
      st->ops.push_back(op_code(k->kernel.name));
      st->as.push_back(static_cast<float>(k->scalar.float_value));
      st->bs.push_back(static_cast<float>(k->scalar.int_value));
    }

    if (cudaStreamCreate(&st->stream) != cudaSuccess) {
      return Result<std::shared_ptr<void>>::failure(Error(ErrorCode::DeviceUnavailable, "cudaStreamCreate failed"));
    }
    st->buffers.reserve(nk + 1);
    for (std::size_t i = 0; i < nk + 1; ++i) {
      void* p = nullptr;
      if (cudaMalloc(&p, bytes) != cudaSuccess) {
        return Result<std::shared_ptr<void>>::failure(Error(ErrorCode::OutOfMemory, "cudaMalloc failed"));
      }
      st->buffers.push_back(p);
    }

    if (cudaGraphCreate(&st->graph, 0) != cudaSuccess) {
      return Result<std::shared_ptr<void>>::failure(Error(ErrorCode::Internal, "cudaGraphCreate failed"));
    }

    auto h2d_parms = make_1d(st->buffers[0], st->h_in.data(), bytes, cudaMemcpyHostToDevice);
    if (cudaGraphAddMemcpyNode(&st->h2d_node, st->graph, nullptr, 0, &h2d_parms) != cudaSuccess) {
      return Result<std::shared_ptr<void>>::failure(Error(ErrorCode::Internal, "cudaGraphAddMemcpyNode H2D failed"));
    }

    st->kernel_nodes.reserve(nk);
    for (std::size_t i = 0; i < nk; ++i) {
      const int blocks = (n + 255) / 256;
      void* inDev = st->buffers[i];
      void* outDev = st->buffers[i + 1];
      int nI = st->n;
      float av = st->as[i];
      float bv = st->bs[i];
      int ov = st->ops[i];
      void* args[6] = {&inDev, &outDev, &nI, &av, &bv, &ov};
      cudaKernelNodeParams kp{};
      kp.func = reinterpret_cast<void*>(gc_generic_kernel);
      kp.gridDim = dim3(blocks, 1, 1);
      kp.blockDim = dim3(256, 1, 1);
      kp.sharedMemBytes = 0;
      kp.kernelParams = args;
      kp.extra = nullptr;
      cudaGraphNode_t dep[1] = {i == 0 ? st->h2d_node : st->kernel_nodes[i - 1]};
      cudaGraphNode_t knode = nullptr;
      if (cudaGraphAddKernelNode(&knode, st->graph, dep, 1, &kp) != cudaSuccess) {
        return Result<std::shared_ptr<void>>::failure(Error(ErrorCode::Internal, "cudaGraphAddKernelNode failed"));
      }
      st->kernel_nodes.push_back(knode);
    }

    auto d2h_parms = make_1d(st->h_out.data(), st->buffers[nk], bytes, cudaMemcpyDeviceToHost);
    cudaGraphNode_t dep[1] = {st->kernel_nodes[nk - 1]};
    if (cudaGraphAddMemcpyNode(&st->d2h_node, st->graph, dep, 1, &d2h_parms) != cudaSuccess) {
      return Result<std::shared_ptr<void>>::failure(Error(ErrorCode::Internal, "cudaGraphAddMemcpyNode D2H failed"));
    }

    if (cudaGraphInstantiate(&st->exec, st->graph, 0) != cudaSuccess) {
      return Result<std::shared_ptr<void>>::failure(Error(ErrorCode::Internal, "cudaGraphInstantiate failed"));
    }
    return Result<std::shared_ptr<void>>::success(std::static_pointer_cast<void>(st));
  }

  Result<void> load(const GraphDescriptor& desc, std::shared_ptr<void>& exec) override {
    (void)desc;
    (void)exec;
    return Result<void>::success();
  }

  Result<void> validate(const GraphDescriptor& desc, const std::shared_ptr<void>& exec) override {
    auto st = std::static_pointer_cast<CudaGraphState>(exec);
    if (!st) return Result<void>::failure(Error(ErrorCode::InvalidArgument, "null cuda graph handle"));
    if (cudaGraphLaunch(st->exec, st->stream) != cudaSuccess) {
      return Result<void>::failure(Error(ErrorCode::Internal, "cudaGraphLaunch failed"));
    }
    if (cudaStreamSynchronize(st->stream) != cudaSuccess) {
      return Result<void>::failure(Error(ErrorCode::Internal, "cudaStreamSynchronize failed"));
    }
    (void)desc;
    return Result<void>::success();
  }

  Result<bool> can_rebind(const GraphDescriptor& desc) const override {
    for (const auto& n : desc.nodes) {
      if (n.category == NodeCategory::Kernel &&
          (n.binding.binding_class == BindingClass::RecaptureRequiredBinding ||
           n.binding.binding_class == BindingClass::ImmutableBinding)) {
        return Result<bool>::success(false);
      }
    }
    return Result<bool>::success(true);
  }

  Result<GraphReplayResult> replay(const GraphDescriptor& desc, const std::shared_ptr<void>& exec,
                                   const ReplayBuffers& buffers, const ReplayDescriptor& rd) override {
    auto st = std::static_pointer_cast<CudaGraphState>(exec);
    if (!st) return Result<GraphReplayResult>::failure(Error(ErrorCode::InvalidArgument, "null cuda graph handle"));
    if (buffers.inputs.empty() || buffers.outputs.empty()) {
      return Result<GraphReplayResult>::failure(Error(ErrorCode::InvalidArgument, "cuda replay requires input and output buffers"));
    }
    std::lock_guard<std::mutex> lock(st->mtx);
    const void* hsrc = buffers.inputs[0];
    void* hdst = buffers.outputs[0];
    // Legal parameter rebind: point the H2D/D2H memcpy nodes at the caller buffers.
    if (cudaGraphExecMemcpyNodeSetParams1D(st->exec, st->h2d_node, st->buffers[0], hsrc, st->bytes,
                                           cudaMemcpyHostToDevice) != cudaSuccess) {
      return Result<GraphReplayResult>::failure(Error(ErrorCode::Internal, "rebind h2d failed"));
    }
    if (cudaGraphExecMemcpyNodeSetParams1D(st->exec, st->d2h_node, hdst,
                                           st->buffers[st->kernel_nodes.size()], st->bytes,
                                           cudaMemcpyDeviceToHost) != cudaSuccess) {
      return Result<GraphReplayResult>::failure(Error(ErrorCode::Internal, "rebind d2h failed"));
    }
    const auto t0 = std::chrono::steady_clock::now();
    if (cudaGraphLaunch(st->exec, st->stream) != cudaSuccess) {
      return Result<GraphReplayResult>::failure(Error(ErrorCode::Internal, "cudaGraphLaunch failed"));
    }
    if (cudaStreamSynchronize(st->stream) != cudaSuccess) {
      return Result<GraphReplayResult>::failure(Error(ErrorCode::Internal, "cudaStreamSynchronize failed"));
    }
    const auto t1 = std::chrono::steady_clock::now();
    GraphReplayResult r;
    r.ok = true;
    r.replay_attempt = rd.replay_attempt.value;
    r.replayed_nodes = st->kernel_nodes.size() + 2;
    r.latency_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
    auto digest = Sha256::compute(hdst, st->bytes);
    r.output_digest = digest;
    r.output_digest_hex = Sha256::hex(digest);
    (void)desc;
    return Result<GraphReplayResult>::success(std::move(r));
  }

  Result<void> rebind(const GraphDescriptor& desc, const std::shared_ptr<void>& exec,
                      const RebindSpec& spec) override {
    auto st = std::static_pointer_cast<CudaGraphState>(exec);
    if (!st) return Result<void>::failure(Error(ErrorCode::InvalidArgument, "null cuda graph handle"));
    std::lock_guard<std::mutex> lock(st->mtx);
    (void)desc;
    for (const auto& u : spec.updates) {
      if (u.role == RebindRole::InputBuffer) {
        cudaGraphExecMemcpyNodeSetParams1D(st->exec, st->h2d_node, st->buffers[0], u.address,
                                           st->bytes, cudaMemcpyHostToDevice);
      } else if (u.role == RebindRole::OutputBuffer) {
        cudaGraphExecMemcpyNodeSetParams1D(st->exec, st->d2h_node, const_cast<void*>(u.address),
                                           st->buffers[st->kernel_nodes.size()], st->bytes,
                                           cudaMemcpyDeviceToHost);
      }
    }
    return Result<void>::success();
  }

  Result<void> unload(const GraphDescriptor& desc, std::shared_ptr<void>& exec) override {
    auto st = std::static_pointer_cast<CudaGraphState>(exec);
    if (st) {
      std::lock_guard<std::mutex> lock(st->mtx);
      if (st->exec) cudaGraphExecDestroy(st->exec);
      if (st->graph) cudaGraphDestroy(st->graph);
      if (st->stream) cudaStreamDestroy(st->stream);
      for (void* p : st->buffers) {
        if (p) cudaFree(p);
      }
      st->exec = nullptr; st->graph = nullptr; st->stream = nullptr;
      st->buffers.clear();
    }
    exec.reset();
    (void)desc;
    return Result<void>::success();
  }

  std::uint64_t backend_resident_bytes(const GraphDescriptor& desc,
                                       const std::shared_ptr<void>& exec) const override {
    (void)desc;
    auto st = std::static_pointer_cast<CudaGraphState>(exec);
    if (!st) return 0;
    return st->bytes * st->buffers.size();
  }
};

namespace {
struct CudaRegistrar {
  CudaRegistrar() {
    register_backend_factory("cuda-graph", [] { return std::make_unique<CudaBackend>(); });
  }
};
CudaRegistrar g_cuda_registrar;
} // namespace

bool cuda_available() {
  int dev_count = 0;
  return cudaGetDeviceCount(&dev_count) == cudaSuccess && dev_count > 0;
}
std::string cuda_device_name() {
  cudaDeviceProp p{};
  if (cudaGetDeviceProperties(&p, 0) == cudaSuccess) return std::string(p.name);
  return {};
}
std::uint32_t cuda_compute_major() {
  cudaDeviceProp p{};
  if (cudaGetDeviceProperties(&p, 0) == cudaSuccess) return static_cast<std::uint32_t>(p.major);
  return 0;
}
std::uint32_t cuda_compute_minor() {
  cudaDeviceProp p{};
  if (cudaGetDeviceProperties(&p, 0) == cudaSuccess) return static_cast<std::uint32_t>(p.minor);
  return 0;
}
Result<void> cuda_require_capability(std::uint32_t major, std::uint32_t minor) {
  if (!cuda_available()) return Result<void>::failure(Error(ErrorCode::DeviceUnavailable, "no CUDA device"));
  cudaDeviceProp p{};
  if (cudaGetDeviceProperties(&p, 0) != cudaSuccess) {
    return Result<void>::failure(Error(ErrorCode::DeviceUnavailable, "cannot query CUDA device"));
  }
  if (p.major < static_cast<int>(major) || (p.major == static_cast<int>(major) && p.minor < static_cast<int>(minor))) {
    return Result<void>::failure(Error(ErrorCode::DeviceUnavailable,
                                      "CUDA device compute capability below required " +
                                      std::to_string(major) + "." + std::to_string(minor)));
  }
  return Result<void>::success();
}

} // namespace gc
