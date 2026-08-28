#include "graphcache/backend.hpp"
#include "graphcache/cpu_backend.hpp"

#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace gc {

namespace {

std::mutex g_registry_mutex;
std::map<std::string, std::function<std::unique_ptr<GraphBackend>()>>& registry() {
  static std::map<std::string, std::function<std::unique_ptr<GraphBackend>()>> reg;
  return reg;
}

} // namespace

void register_backend_factory(std::string name,
                              std::function<std::unique_ptr<GraphBackend>()> factory) {
  std::lock_guard<std::mutex> lock(g_registry_mutex);
  registry()[std::move(name)] = std::move(factory);
}

std::unique_ptr<GraphBackend> create_backend(BackendKind kind) {
  if (kind == BackendKind::Cpu) {
    return std::make_unique<CpuBackend>();
  }
  if (kind == BackendKind::Cuda) {
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    auto it = registry().find("cuda-graph");
    if (it != registry().end()) return it->second();
    return nullptr;
  }
  return nullptr;
}

#if !defined(GC_HAS_CUDA)

bool cuda_available() { return false; }
std::string cuda_device_name() { return {}; }
std::uint32_t cuda_compute_major() { return 0; }
std::uint32_t cuda_compute_minor() { return 0; }
Result<void> cuda_require_capability(std::uint32_t /*major*/, std::uint32_t /*minor*/) {
  return Result<void>::failure(Error(ErrorCode::BackendUnavailable, "CUDA is not available in this build"));
}

#endif

} // namespace gc
