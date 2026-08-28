# Limitations

This section records the actual, proven capabilities and the boundaries that are
documented rather than oversold.

## CUDA Graph portability

CUDA Graph execution state (cudaGraphExec_t) is an opaque, process-local handle.
Graph Cache does not claim that a live CUDA graph executable survives a process
restart. Persistence stores the canonical graph description, compatibility data,
provenance, dependency identities, and capture inputs, but not a portable
serialized cudaGraphExec. On recovery, live CUDA graph handles are treated as
absent, and graph execution state is reconstructed or recaptured from persisted
authority. CUDA's runtime graph is authoritative executable state only while the
resolving process is alive.

On this target the CUDA path has been exercised on an NVIDIA GeForce RTX 5090
(compute capability 12.0 / sm_120) under CUDA 13.1 with real cudaGraphCreate /
cudaGraphAdd, cudaGraphInstantiate, cudaGraphExec node parameter updates,
cudaGraphLaunch, and teardown, for a 1024-element workload with two kernel nodes,
two memcpy nodes, and repeated replay.

## Backend coverage

The vendor-neutral architecture defines typed interfaces, but only the CPU DAG
backend and the CUDA Graph backend are implemented. No AMD or Intel backend is
implemented; the interfaces are not asserted for vendors that have not been
exercised.

## Distributed authority

The atomic stale-authority proof runs across real OS processes over framed TCP
and passes. It proves register, capture, hit, replay, worker kill, worker restart
with a new WorkerBootId, coordinator epoch rollover, and the deterministic
rejection of stale epoch, stale worker boot, stale cache generation, and stale
graph generation, followed by hard-asserted fresh success and a second exact
hit/replay under the restarted worker and current epoch.

## Persistence checksum and corruption detection

Persistence uses a versioned, checksummed envelope written atomically (temp-write
then rename). Corruption, truncation, unknown version, and trailing garbage are
detected and rejected by the PersistenceStore. These are proven by the
persistence test suite.

## Documentation diagrams

Any Mermaid diagrams in this documentation use conservative GitHub-compatible
syntax with quoted labels. They have been statically validated; no renderer was
run locally.
