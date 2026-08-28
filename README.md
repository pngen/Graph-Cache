# Graph Cache

Graph Cache is an open-source, vendor-neutral runtime for caching, validating,
reusing, invalidating, persisting, and replaying executable AI execution graphs
with compatibility-safe lookup and accelerator-aware lifecycle management.

Its governing systems question is:

> Which previously captured execution graph may be safely replayed for this
> workload, under what compatibility and generation constraints, when must it be
> recaptured or invalidated, where should it reside, and how can graph reuse
> accelerate execution without allowing stale, incompatible, or corrupt
> execution state to become authoritative?

Graph Cache is **not** a generic object cache, DAG library, build cache, kernel
cache, workflow engine, CUDA Graph toy, benchmark wrapper, metadata registry, or
graph visualization library. It is the runtime boundary for reusable executable
execution graphs inside AI inference and accelerator infrastructure.

- C++20, Windows x64, MSVC, CMake + Ninja.
- CUDA 13.1 on NVIDIA GeForce RTX 5090 (compute capability 12.0 / sm_120).
- CPU-only functionality remains valid where CUDA is unavailable.
- Vendor-neutral architecture; CUDA Graphs are one proven backend and do not
  define the abstract semantics of Graph Cache.

## Building

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

A downstream project can discover and link the installed package normally:

```cmake
find_package(GraphCache CONFIG REQUIRED)
target_link_libraries(mytarget PRIVATE GraphCache::GraphCache)
```

## What Graph Cache proves

- **Typed, deterministic compatibility.** A graph cache hit is a correctness
  decision. `GraphCompatibilityKey` encodes all semantics that can make replay
  unsafe and digests them with SHA-256 over canonical typed encodings.
- **Real CPU graph backend.** It executes a real operation DAG (copy, kernels,
  memset) in deterministic topological order over host buffers.
- **Real CUDA Graph backend.** Genuine `cudaGraphCreate`/`cudaGraphAdd*`,
  `cudaGraphInstantiate`, `cudaGraphExec*NodeSetParams` rebinding,
  `cudaGraphLaunch`, and teardown. Proven on an NVIDIA GeForce RTX 5090 (sm_120).
- **Single-flight capture**, guarded lifecycle, generation authority, leases,
  eviction, residency tiers, persistence with checksummed atomic writes, and
  recovery.
- **Distributed authority** over real framed TCP with a coordinator process and
  worker processes.

See `docs/` for architecture, lifecycle, compatibility, validation, and
limitations. See `examples/` for runnable programs.

## Layout

- `include/graphcache/` — public headers.
- `src/` — core library.
- `src/cuda/` — CUDA Graph backend.
- `tests/` — unit, adversarial, concurrency, persistence, property, CUDA, and
  atomic multiprocess proof.
- `examples/` — runnable examples.
- `benchmarks/` — measured benchmarks.
- `cli/` — command-line tool.
- `tools/` — distributed coordinator and worker binaries.
- `docs/` — documentation.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.
