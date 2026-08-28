#pragma once
// Deterministic CPU graph backend. It is NOT a metadata store: it represents a
// real operation DAG and executes deterministic computation through that DAG.
// Copyright 2026 Summon Software Labs. Apache License 2.0.

#include "graphcache/backend.hpp"

namespace gc {

class CpuBackend final : public GraphBackend {
 public:
  CpuBackend() = default;
  ~CpuBackend() override = default;

  BackendIdentity identity() const override;

  Result<std::shared_ptr<void>> capture(const GraphDescriptor& desc,
                                        CaptureAttemptId attempt) override;
  Result<void> validate(const GraphDescriptor& desc, const std::shared_ptr<void>& exec) override;
  Result<void> load(const GraphDescriptor& desc, std::shared_ptr<void>& exec) override;
  Result<GraphReplayResult> replay(const GraphDescriptor& desc, const std::shared_ptr<void>& exec,
                                   const ReplayBuffers& buffers, const ReplayDescriptor& rd) override;
  Result<void> rebind(const GraphDescriptor& desc, const std::shared_ptr<void>& exec,
                      const RebindSpec& spec) override;
  Result<bool> can_rebind(const GraphDescriptor& desc) const override;
  Result<void> unload(const GraphDescriptor& desc, std::shared_ptr<void>& exec) override;
  std::uint64_t backend_resident_bytes(const GraphDescriptor& desc,
                                       const std::shared_ptr<void>& exec) const override;
};

} // namespace gc
